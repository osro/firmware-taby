"""Check C6 card decoration with full SD and both radios; close Taby first.

Verifies protocol, card lifetime, replacement, heap and driver counters. Visual
quality requires separately watching the device or the real-LVGL host frames.
No resets, card writes, saved-network changes or personal identifiers are used.
"""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import time
from c6_verify import Session, require

ROOT = Path(__file__).resolve().parents[1]
EFFECTS = ('shooting_stars', 'firework_dots', 'heart_burst', 'gold_rings',
           'square_confetti', 'diamond_burst', 'comet_burst', 'ribbon_sweep',
           'ember_drift', 'star_twinkle', 'bubble_pop', 'pixel_burst',
           'glow_wave', 'sunbeam_lines', 'prism_shards', 'heart_rain',
           'confetti_rain', 'orbit_dots', 'flare_fan', 'petal_swirl',
           'neon_arc', 'spiral_sparks', 'checker_burst', 'aurora_sweep')
FAMILIES = ('shooting_stars', 'comet_burst', 'firework_dots', 'heart_burst',
            'heart_rain', 'gold_rings', 'ribbon_sweep', 'aurora_sweep')


def restore_device(s, old_orientation, old_transport):
    commands = [('CLEAR', 'TABY:OK ')]
    if old_orientation is not None:
        commands.append(('DISPLAY_ORIENTATION '+old_orientation, 'TABY:DISPLAY_ORIENTATION '))
    if old_transport is not None:
        commands.append(('TRANSPORT_MODE '+old_transport, 'TABY:OK '))
    failed = []
    for command, prefix in commands:
        try:
            s.query(command, prefix)
        except Exception:
            failed.append(command.split()[0])
    if failed:
        raise RuntimeError('Device restoration failed: '+', '.join(failed))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port', required=True)
    p.add_argument('--demo-seconds', type=int, default=0,
                   help='Show each family this many seconds for owner inspection (0 disables)')
    p.add_argument('--output', type=Path, default=ROOT/'.local/c6-decor-verification.json')
    a = p.parse_args()
    require(0 <= a.demo_seconds <= 30, 'Demo duration must be 0..30 seconds')
    s = Session(a.port)
    r = {'timestamp_utc': datetime.now(timezone.utc).isoformat(), 'passed': False,
         'scope': 'Device protocol/lifetime/heap/driver checks; no automatic visual-quality assertion',
         'effects': [], 'families_expired': [], 'visual_feedback': 'pending'}
    old_orientation = old_transport = None
    try:
        info = json.loads(s.query('INFO', 'TABY:INFO '))
        require(info['hardware_target'] == 'c6-lcd-1.47', 'Wrong board')
        require(info['animation_count'] == 84 and info['asset_storage'] == 'sd_card',
                'Full SD pack not mounted; cold power-cycle before testing')
        r['firmware_version'] = info['firmware_version']
        old_transport = info['transport_mode']
        old_orientation = json.loads(s.query('DISPLAY_ORIENTATION?', 'TABY:DISPLAY_ORIENTATION '))['mode']
        s.query('TRANSPORT_MODE wifi', 'TABY:OK ')
        s.query('TRANSPORT_MODE ble', 'TABY:OK ')
        time.sleep(2)
        s.query('CLEAR', 'TABY:OK ')
        r['initial'] = s.diag()
        for orientation in ('left', '90'):
            s.query('DISPLAY_ORIENTATION '+orientation, 'TABY:DISPLAY_ORIENTATION ')
            for effect in EFFECTS:
                s.query(f'UI/title&{effect}/1?decor-check:CELEBRATE', 'TABY:OK ')
                time.sleep(.13)
                require(s.diag()['state'] == 'UI_DECOR_CHECK', 'Decor card not active')
                r['effects'].append({'orientation': orientation, 'effect': effect, 'accepted': True})
        s.query('DISPLAY_ORIENTATION left', 'TABY:DISPLAY_ORIENTATION ')
        for effect in FAMILIES:
            s.query(f'UI/text&{effect}/1?decor-expiry:CELEBRATE|Card stays when effect ends', 'TABY:OK ')
            time.sleep(1.15)
            require(s.diag()['state'] == 'UI_DECOR_EXPIRY', 'Effect expiry removed card')
            r['families_expired'].append(effect)
        # Repeated allocation/free on the actual custom-malloc LVGL build.
        s.query('CLEAR', 'TABY:OK ')
        r['before_churn'] = s.diag()
        for n in range(120):
            effect = EFFECTS[n % len(EFFECTS)]
            s.query(f'UI/title&{effect}/1:CELEBRATE', 'TABY:OK ')
            s.query('CLEAR', 'TABY:OK ')
        time.sleep(.15)
        r['after_churn'] = s.diag()
        require(r['after_churn']['free_heap'] >= r['before_churn']['free_heap']-2048,
                'Heap loss after repeated decor replacement')
        s.query('UI/title?preserve:KEEP ME', 'TABY:OK ')
        s.query('UI/title&not_an_effect:INVALID', 'TABY:ERR ')
        require(s.diag()['state'] == 'UI_PRESERVE', 'Invalid decor replaced card')
        s.query('UI/title!greeting*:UNSUPPORTED CLIP', 'TABY:ERR ')
        require(s.diag()['state'] == 'UI_PRESERVE', 'Unsupported clip replaced card')
        s.query('UI/title&heart_burst/2:VOICE NEXT', 'TABY:OK ')
        s.query('VOICE_MOTION 830 0 0 '+'K_A'*60, 'TABY:OK ')
        time.sleep(.06)
        require(s.diag()['voice_active'], 'Voice failed to replace decorated card')
        s.query('UI/title&gold_rings/1:BACK TO CARD', 'TABY:OK ')
        require(not s.diag()['voice_active'], 'Decorated card failed to cancel voice')
        r['voice_decor_ownership'] = True
        s.query('UI/timer:FOCUS|1|1||run|1', 'TABY:OK ')
        time.sleep(1.3)
        require(s.diag()['state'] == 'IDLE', 'Timer no longer resumes idle')
        if a.demo_seconds:
            for effect in FAMILIES:
                label = effect.replace('_', ' ').upper()
                print('Showing '+label, flush=True)
                s.query(f'UI/text&{effect}/{a.demo_seconds}:CELEBRATE|{label}', 'TABY:OK ')
                time.sleep(a.demo_seconds)
            r['demo_family_seconds'] = a.demo_seconds
        s.query('CLEAR', 'TABY:OK ')
        before = s.diag()['rendered_frames']
        # Calm/still deliberately hold the first idle frame for up to 7/9s.
        # Keep the owner's setting and wait through its maximum hold.
        deadline = time.monotonic()+10.5
        while True:
            r['final'] = s.diag()
            if r['final']['rendered_frames'] > before or time.monotonic() >= deadline:
                break
            time.sleep(.15)
        require(r['final']['rendered_frames'] > before, 'SD playback did not resume')
        require(all(r['final'][k] == 0 for k in ('decode_errors', 'display_errors', 'touch_errors')),
                'Driver errors during decor verification')
        final_info = json.loads(s.query('INFO', 'TABY:INFO '))
        r['radios'] = {k: final_info[k] for k in ('wifi_started', 'ble_started', 'connected')}
        require(all(r['radios'][k] for k in ('wifi_started', 'ble_started')), 'Both radios must be active')
        require(final_info['animation_count'] == 84 and final_info['asset_storage'] == 'sd_card', 'SD pack lost')
        r['passed'] = True
    except BaseException as e:
        r['error'] = type(e).__name__+': '+str(e)
        raise
    finally:
        cleanup_error = None
        try:
            restore_device(s, old_orientation, old_transport)
        except Exception as e:
            cleanup_error = e
            r['restore_error'] = str(e)
            r['passed'] = False
        s.connection.close()
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(r, indent=2)+'\n')
        if cleanup_error is not None and 'error' not in r:
            raise cleanup_error


if __name__ == '__main__':
    main()
