"""Exercise the development C6 port over one persistent USB session.

This checks hardware protocol/playback counters, not visual quality. Never
prints the chip-based device identity or modifies flash. Tap/hold gestures can
be supplied by the owner during the full-cycle animation run.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
import serial

ROOT = Path(__file__).resolve().parents[1]


class Session:
    def __init__(self, port):
        self.connection = serial.Serial(port=None, baudrate=115200, timeout=.1, write_timeout=2)
        # Keep both control lines asserted before opening. Clearing them on
        # this Mac triggers USB_UART_HPSYS while the SD card remains powered.
        self.connection.dtr = True
        self.connection.rts = True
        self.connection.port = port
        self.connection.open()
        self.pending = bytearray()
        # Installing the USB driver after a cold SD checksum can discard bytes
        # sent during boot. Retry only a harmless handshake, never a mutation.
        deadline=time.monotonic()+60
        next_ping=0
        try:
            while time.monotonic()<deadline:
                if time.monotonic()>=next_ping:
                    self.connection.write(b'PING\n');self.connection.flush()
                    next_ping=time.monotonic()+1
                self.pending.extend(self.connection.read(self.connection.in_waiting or 1))
                if len(self.pending)>65536:raise RuntimeError('Oversized boot output')
                ready=False
                while b'\n' in self.pending:
                    line,_,rest=self.pending.partition(b'\n');self.pending=bytearray(rest)
                    if line.strip()==b'TABY:PONG':ready=True;break
                if ready:break
            else:raise TimeoutError('Board did not become ready within 60 seconds')
        except BaseException:
            self.connection.close();raise

    def query(self, command, prefix):
        self.connection.write((command+'\n').encode('ascii'))
        self.connection.flush()
        deadline = time.monotonic()+40  # full SD-pack checksum can delay first boot
        while time.monotonic() < deadline:
            self.pending.extend(self.connection.read(self.connection.in_waiting or 1))
            if len(self.pending) > 65536:
                raise RuntimeError('Oversized device response')
            while b'\n' in self.pending:
                line, _, rest = self.pending.partition(b'\n')
                self.pending = bytearray(rest)
                text = line.decode(errors='replace').strip()
                if text.startswith(prefix):
                    return text[len(prefix):]
                if text.startswith('TABY:ERR'):
                    raise RuntimeError(text)
        raise TimeoutError(f'No response for {command[:64]}')

    def diag(self):
        return json.loads(self.query('DIAG', 'TABY:DIAG '))


def require(condition, description):
    if not condition:
        raise AssertionError(description)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--radios', action='store_true', help='Keep Wi-Fi setup and BLE active during playback checks')
    parser.add_argument('--expected-firmware', default='0.2.0-c6-dev.4')
    parser.add_argument('--output', type=Path, default=ROOT/'.local/c6-hardware-verification.json')
    args = parser.parse_args()
    metadata = json.loads((ROOT/'dist/c6-lcd-1.47/assets.json').read_text())
    session = Session(args.port)
    saved_transport = None
    report = {'timestamp_utc': datetime.now(timezone.utc).isoformat(),
              'visual_verification': 'requires_owner_feedback', 'clips': []}
    try:
        require(session.query('PING', 'TABY:') == 'PONG', 'PING failed')
        info = json.loads(session.query('INFO', 'TABY:INFO '))
        saved_transport=info.get('preferred_transport','usb')
        require(info['hardware_target'] == 'c6-lcd-1.47', 'Wrong target')
        require(info['firmware_version'] == args.expected_firmware, 'Wrong firmware version')
        if info.get('asset_storage')=='sd_card':
            metadata=json.loads((ROOT/'dist/c6-lcd-1.47-sd/taby/assets.json').read_text())
        require(info['assets_sha256'] == metadata['sha256'], 'Asset pack mismatch')
        require((info['display_width'], info['display_height']) == (172,320), 'Wrong dimensions')
        report['device'] = {k: info[k] for k in ('hardware_target','firmware_version','assets_version','assets_sha256')}
        if args.radios:
            session.query('TRANSPORT_MODE wifi','TABY:OK ')
            session.query('TRANSPORT_MODE ble','TABY:OK ')
            time.sleep(3)
        report['radios_active']=args.radios
        initial = session.diag()
        report['initial'] = initial
        for clip in metadata['clips']:
            before = session.diag()
            result = session.query(clip['id'], 'TABY:OK ')
            require('unsupported' not in result, f'Missing {clip["id"]}')
            time.sleep(.25)
            active = session.diag()
            require(active['animation'] == clip['id'], f'Did not select {clip["id"]}')
            require(active['rendered_frames'] > before['rendered_frames'], 'Playback stalled')
            time.sleep(clip['duration_ms']/1000*1.15+.25)
            after = session.diag()
            require(after['completed_cycles'] > before['completed_cycles'], f'Cycle stalled: {clip["id"]}')
            require(after['decode_errors'] == 0 and after['display_errors'] == 0, 'Rendering error')
            # Radio/HTTP packets can own short-lived buffers at a sample.
            # Require recovery over a bounded quiet window, retaining the raw
            # measurement so transient pressure remains visible in the record.
            recovered_heap=after['free_heap']
            if recovered_heap<initial['free_heap']-2048:
                for _ in range(5):
                    time.sleep(.1);recovered_heap=max(recovered_heap,session.diag()['free_heap'])
            require(recovered_heap >= initial['free_heap']-2048, 'Heap did not recover')
            report['clips'].append({'id': clip['id'], 'cycle_completed': True,
                                    'frames_rendered_since_selection': after['rendered_frames']-before['rendered_frames'],
                                    'free_heap': after['free_heap'], 'recovered_free_heap': recovered_heap})
            print(f'PASS full cycle: {clip["id"]}', flush=True)
        require(session.query('TRANSPORT_DEFAULT usb','TABY:OK ') == 'TRANSPORT_DEFAULT', 'Wrong default acknowledgement')
        require(session.query('TRANSPORT_MODE usb','TABY:OK ') == 'TRANSPORT_MODE', 'Wrong mode acknowledgement')
        require(session.query('TRANSPORT_MODE unsupported','TABY:ERR ') == 'unsupported_transport', 'Unsupported transport accepted')
        session.query('CLEAR','TABY:OK ')
        before = session.diag()
        unsupported = session.query('no_such_c6_animation','TABY:OK ')
        require('unsupported_animation no_such_c6_animation' in unsupported, 'Missing unsupported-animation response')
        require(session.diag()['animation'] == before['animation'], 'Unknown clip changed playback')
        require(session.query('x'*1100,'TABY:ERR ') == 'line_too_long', 'Oversized line accepted')
        require(session.query('PING','TABY:') == 'PONG', 'Oversized line broke later commands')
        session.query('confirmation>idle_01_loop','TABY:OK ')
        time.sleep(3)
        require(session.diag()['animation'] == 'idle_01_loop', 'Sequence did not advance')
        report['final'] = session.diag()
        require(report['final']['touch_samples'] > 0 and report['final']['touch_errors'] == 0, 'Touch controller unavailable')
        require(report['final']['decode_errors'] == 0 and report['final']['display_errors'] == 0, 'Final rendering error')
        report['touch_taps_observed'] = report['final']['touch_count']-initial['touch_count']
        report['gesture'] = json.loads(session.query('GESTURE_SIGNAL','TABY:GESTURE_SIGNAL '))
        report['passed'] = True
        print(json.dumps(report, indent=2))
    except Exception as error:
        report.update(passed=False, error=str(error))
        raise
    finally:
        if saved_transport is not None:
            try:session.query('TRANSPORT_DEFAULT '+saved_transport,'TABY:OK ')
            except Exception as error:report.update(passed=False,restore_error=str(error))
        session.connection.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__':
    main()
