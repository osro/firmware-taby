"""Build a bounded, memory-mappable animation pack from licensed Taby artwork.

TC6A clip: <4sHHHH magic,width,height,frames,loop; then for each frame:
<HI duration_ms,payload_bytes; 64 big-endian RGB565 palette entries; indexed RLE.
RLE token bit 7 selects a repeated index; low 7 bits + 1 give run/literal length.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
TARGET = ROOT / 'firmware/targets/c6-lcd-1.47'
IDS = ('startup', 'idle_01_loop', 'listening_loop', 'talking_default_loop',
       'working_loop', 'searching_loop', 'confirmation', 'busy_loop',
       'task_completed', 'task_created', 'disappointed', 'sleeping_loop',
       'building_loop', 'waiting_01', 'codex_loop', 'claude_loop')
WIDTH, HEIGHT = 172, 280


def encode_rle(pixels):
    if any(p >= 64 for p in pixels):
        raise ValueError('Only 64 palette entries are supported')
    result = bytearray()
    i = 0
    while i < len(pixels):
        run = 1
        while run < 128 and i + run < len(pixels) and pixels[i+run] == pixels[i]:
            run += 1
        if run >= 3:
            result.extend((128 | (run-1), pixels[i]))
            i += run
        else:
            start = i
            i += 1
            while i < len(pixels) and i-start < 128:
                if i+2 < len(pixels) and pixels[i] == pixels[i+1] == pixels[i+2]:
                    break
                i += 1
            result.append(i-start-1)
            result.extend(pixels[start:i])
    return bytes(result)


def encode_image(image):
    from PIL import Image
    scaled = image.convert('RGB').resize((WIDTH, HEIGHT), Image.Resampling.LANCZOS)
    indexed = scaled.quantize(colors=64, dither=Image.Dither.NONE)
    rgb = indexed.getpalette()
    rgb += [0] * max(0, 64*3-len(rgb))
    palette = bytearray()
    for i in range(64):
        r,g,b = rgb[i*3:i*3+3]
        palette.extend(struct.pack('>H', ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)))
    return bytes(palette) + encode_rle(indexed.tobytes()), indexed.convert('RGB')


def encode_gif(path, loop):
    from PIL import Image, ImageSequence
    selected, previews, starts = [], [], []
    elapsed, next_sample = 0, 0
    with Image.open(path) as gif:
        for frame in ImageSequence.Iterator(gif):
            duration = max(10, int(frame.info.get('duration', 40)))
            if elapsed >= next_sample:
                data, preview = encode_image(frame)
                selected.append(data)
                previews.append(preview)
                starts.append(elapsed)
                next_sample = elapsed + 80  # at most 12.5 FPS; preserve the source timeline
            elapsed += duration
    starts.append(elapsed)
    packed = bytearray(struct.pack('<4sHHHH', b'TC6A', WIDTH, HEIGHT, len(selected), bool(loop)))
    durations = []
    for i, data in enumerate(selected):
        duration = starts[i+1]-starts[i]
        if not 1 <= duration <= 65535:
            raise ValueError('Frame duration outside the pack format')
        durations.append(duration)
        packed.extend(struct.pack('<HI', duration, len(data)))
        packed.extend(data)
    return bytes(packed), previews, durations


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--sd-card', action='store_true', help='Build all clips for taby/ on an SD card; keep flash fallback unchanged')
    args = parser.parse_args()
    if args.output is None:
        args.output = ROOT/('dist/c6-lcd-1.47-sd/taby' if args.sd_card else 'dist/c6-lcd-1.47')
    source = ROOT/'assets/amoled-1.64'
    manifest = json.loads((source/'manifest.json').read_text())
    catalog = json.loads((source/'catalog.json').read_text())
    loop_policy = {a['id']: a['loop_policy']=='loop' for a in catalog['animations']}
    paths = {a['id']: source/a['relative_path'] for a in manifest['animations']}
    args.output.mkdir(parents=True, exist_ok=True)
    clips, pack = [], bytearray()
    for animation_id in (paths.keys() if args.sd_card else IDS):
        path = paths[animation_id]
        loop = loop_policy[animation_id] if args.sd_card else animation_id.endswith('_loop')
        data, frames, durations = encode_gif(path, loop)
        while len(pack) % 4:
            pack.append(0)
        clips.append({'id': animation_id, 'offset': len(pack), 'size': len(data),
                      'frames': len(frames), 'duration_ms': sum(durations),
                      'source_sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
        pack.extend(data)
        if animation_id in ('idle_01_loop', 'confirmation', 'talking_default_loop'):
            frames[0].save(args.output/f'{animation_id}.gif', save_all=True,
                           append_images=frames[1:], duration=durations, loop=0)
        print(f'{animation_id}: {len(frames)} frames, {len(data):,} bytes', flush=True)
    if not args.sd_card and len(pack) > 0x600000:
        raise SystemExit(f'Pack exceeds the 6 MB partition: {len(pack)}')
    (args.output/'assets.bin').write_bytes(pack)
    digest = hashlib.sha256(pack).hexdigest()
    metadata = {'board': 'c6-lcd-1.47', 'version': '0.2.1-sd' if args.sd_card else '0.1.0-dev',
                'width': WIDTH, 'height': HEIGHT, 'source_assets': manifest['version'],
                'bytes': len(pack), 'sha256': digest, 'crc32': zlib.crc32(pack), 'clips': clips}
    (args.output/'assets.json').write_text(json.dumps(metadata, indent=2)+'\n')
    generated = TARGET/'main/generated'
    generated.mkdir(parents=True, exist_ok=True)
    if args.sd_card:
        header = ['/* Generated full SD catalog; see root LICENSE for artwork terms. */',
                  '#pragma once', '#include "c6_catalog.h"',
                  f'#define C6_SD_ASSET_BYTES {len(pack)}u',
                  f'#define C6_SD_ASSET_CRC32 0x{zlib.crc32(pack):08x}u',
                  f'#define C6_SD_ASSET_SHA256 "{digest}"',
                  'static const c6_clip_t c6_sd_clips[] = {']
        header += [f'    {{"{c["id"]}", {c["offset"]}u, {c["size"]}u}},' for c in clips]
        header += ['};', '#define C6_SD_CLIP_COUNT (sizeof(c6_sd_clips)/sizeof(c6_sd_clips[0]))', '']
        (generated/'c6_sd_catalog.h').write_text('\n'.join(header))
        print(f'SD pack ready: {len(pack):,} bytes, SHA256 {digest}')
        return
    header = ['/* Generated by tools/c6_assets.py; artwork retains upstream LICENSE. */',
              '#pragma once', '#include <stdint.h>',
              'typedef struct { const char *id; uint32_t offset, size; } c6_clip_t;',
              f'#define C6_ASSET_BYTES {len(pack)}u',
              f'#define C6_ASSET_CRC32 0x{zlib.crc32(pack):08x}u',
              f'#define C6_ASSET_SHA256 "{digest}"',
              'static const c6_clip_t c6_clips[] = {']
    header += [f'    {{"{c["id"]}", {c["offset"]}u, {c["size"]}u}},' for c in clips]
    header += ['};', '#define C6_CLIP_COUNT (sizeof(c6_clips)/sizeof(c6_clips[0]))', '']
    (generated/'c6_catalog.h').write_text('\n'.join(header))
    print(f'Pack ready: {len(pack):,}/6,291,456 bytes, SHA256 {digest}')


if __name__ == '__main__':
    main()
