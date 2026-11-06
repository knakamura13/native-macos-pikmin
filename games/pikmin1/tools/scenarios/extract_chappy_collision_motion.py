#!/usr/bin/env python3
"""Extract measured collision-driving joint channels for the Chappy experiment.

No meshes, textures, original motion names or raw disc binaries are emitted.
Only ancestors of the model's collision joints retain animation; the other
joints keep the MOD rest SRT. DCA samples and DCK time/value/tangent records
remain exact binary32 values, with unused block elements omitted and aliases
shared. Decode layouts: nectar_assets/src/{mod,bundle,anim}.cpp and the game's
AnimDca::read / AnimDck::read in src/sysCommon/shapeBase.cpp.

Usage: extract_chappy_collision_motion.py /path/to/install/assets --output file.inc
Optional --verify-payload-dir verifies files info00.bin ... info14.bin written
by the C++ animation writers, against source channels and evaluated matrices.
"""
import argparse
import hashlib
import math
import struct
from pathlib import Path

COLLISION_ANCESTORS = frozenset((0, 1, 3, 4, 7, 8, 10, 11, 13, 14, 15, 16))
INFO_FRAMES = (90, 75, 75, 75, 25, 25, 55, 75, 90, 80, 106, 45, 25, 25, 40)


class Reader:
    def __init__(self, data):
        self.data = data
        self.at = 0

    def unpack(self, fmt):
        values = struct.unpack_from('>' + fmt, self.data, self.at)
        self.at += struct.calcsize('>' + fmt)
        return values

    def integer(self):
        return self.unpack('i')[0]

    def floats(self, count):
        return self.unpack(str(count) + 'f')

    def string(self):
        count = self.integer()
        if count < 0 or self.at + count > len(self.data):
            raise ValueError('invalid string size')
        value = self.data[self.at:self.at + count]
        self.at += count
        return value.rstrip(b'\0').decode('ascii')

    def parms(self):
        values = {}
        while True:
            header = self.unpack('I')[0]
            if header == 0xffffffff:
                return values
            count = header & 255
            name = (header >> 8).to_bytes(3, 'big').decode('ascii')
            if name == 'a01':
                values[name] = self.string()
            else:
                values[name] = self.data[self.at:self.at + count]
                self.at += count


def model_rest(data):
    at = 0
    while at + 8 <= len(data):
        kind, length = struct.unpack_from('>II', data, at)
        end = at + 8 + length
        if end > len(data):
            raise ValueError('truncated MOD chunk')
        if kind == 0x60:
            r = Reader(data)
            r.at = at + 8
            count = r.integer()
            r.at = (r.at + 31) & ~31
            joints = []
            for _ in range(count):
                parent, _flags = r.unpack('iI')
                r.at += 28  # bounds and unused radius
                values = [r.floats(3) for _ in range(3)]
                polys = r.integer()
                r.at += polys * 4
                joints.append((parent, values))
            if r.at > end or count != 17:
                raise ValueError('unexpected joint chunk')
            return joints
        at = end
    raise ValueError('MOD has no joint chunk')


def payload(data, kind):
    r = Reader(data)
    count, frames = r.unpack('ii')
    blocks = [r.floats(r.integer()) for _ in range(3)]
    tracks = []
    for _ in range(count):
        group, parent = r.unpack('ii')
        channels = []
        for _block in range(3):
            axes = []
            for _axis in range(3):
                n, offset = r.unpack('ii')
                flags = r.integer() if kind == 3 else 0
                axes.append((n, offset, flags))
            channels.append(axes)
        tracks.append((group, parent, channels))
    if r.at != len(data) or kind not in (2, 3) or count != 17:
        raise ValueError('unexpected animation payload')
    return dict(kind=kind, frames=frames, blocks=blocks, tracks=tracks)


def bundle(data):
    r = Reader(data)
    motions, names = [], {}
    for index in range(r.integer()):
        kind, length = r.unpack('ii')
        name = r.string()
        motions.append(payload(data[r.at:r.at + length], kind))
        names[name] = index
        r.at += length
    if r.at != len(data):
        raise ValueError('bundle size mismatch')
    return motions, names


def info_mapping(data, names):
    r = Reader(data)
    version, count = r.unpack('ii')
    params = r.parms()
    base = params['a01']
    mapping = []
    for _ in range(count):
        name = r.string()
        mapping.append(names[base + '/' + name])
        r.parms()
        n = r.integer()
        r.at += 4 * n
        if version >= 1:
            n = r.integer()
            r.at += 8 * n
        n = r.integer()
        r.at += 8 * n
    if r.at != len(data) or count != 15:
        raise ValueError('unexpected Chappy AnimMgr')
    return mapping


def channel_values(motion, block_index, channel):
    count, offset, flags = channel
    width = 1 if motion['kind'] == 2 or count <= 1 else (3 if flags == 0 else 4)
    values = motion['blocks'][block_index][offset:offset + count * width]
    if len(values) != count * width:
        raise ValueError('channel exceeds its block')
    return values


def repack(motion, rest):
    blocks = [[], [], []]
    # Repeated static and sampled channels may share their compact numeric data.
    offsets = [{}, {}, {}]
    tracks = []
    for joint, (group, parent, channels) in enumerate(motion['tracks']):
        if rest[joint][0] != parent:
            raise ValueError('animation parent differs from model')
        axes_out = []
        for block_index, axes in enumerate(channels):
            out = []
            for axis, channel in enumerate(axes):
                if joint in COLLISION_ANCESTORS:
                    count, _offset, flags = channel
                    values = channel_values(motion, block_index, channel)
                else:
                    count, flags = 1, 0
                    values = (rest[joint][1][block_index][axis],)
                key = struct.pack('>' + str(len(values)) + 'f', *values)
                if key not in offsets[block_index]:
                    offsets[block_index][key] = len(blocks[block_index])
                    blocks[block_index].extend(values)
                out.append((count, offsets[block_index][key], flags))
            axes_out.append(out)
        tracks.append((group, parent, axes_out))
    result = dict(kind=motion['kind'], frames=motion['frames'], blocks=blocks, tracks=tracks)
    verify_channels(motion, result, rest)
    return result


def verify_channels(source, result, rest):
    if source['kind'] != result['kind'] or source['frames'] != result['frames']:
        raise ValueError('motion type/frame count changed')
    for joint, ((group, parent, channels), (new_group, new_parent, new_channels)) in enumerate(zip(source['tracks'], result['tracks'])):
        if (group, parent) != (new_group, new_parent):
            raise ValueError('joint identity/parent changed')
        for block, axes in enumerate(channels):
            for axis, original in enumerate(axes):
                actual = new_channels[block][axis]
                values = channel_values(result, block, actual)
                expected = channel_values(source, block, original) if joint in COLLISION_ANCESTORS else (rest[joint][1][block][axis],)
                shape = original[::2] if joint in COLLISION_ANCESTORS else (1, 0)
                if actual[::2] != shape or struct.pack('>' + str(len(values)) + 'f', *values) != struct.pack('>' + str(len(expected)) + 'f', *expected):
                    raise ValueError(f'channel changed: joint {joint}, block {block}, axis {axis}')


def value(motion, block, channel, time):
    count, _offset, flags = channel
    values = channel_values(motion, block, channel)
    if count == 0:
        return 0.0 if block == 1 else 1.0
    if count == 1:
        return values[0]
    if motion['kind'] == 2:
        i = int(time)
        last = count - 1
        if i >= last:
            return values[last]
        p1 = values[i]
        differences = [p1 - values[max(i - 1, 0)], values[i + 1] - p1, values[min(i + 2, last)] - values[i + 1]]
        if block == 1:
            differences = [d - 2 * math.pi if d > math.pi else d + 2 * math.pi if d < -math.pi else d for d in differences]
        d0, d1, d2 = differences
        p0, p2, p3 = p1 - d0, p1 + d1, p1 + d1 + d2
        t = time - i
        return 0.5 * (2*p1 + (p2-p0)*t + (2*p0-5*p1+4*p2-p3)*t*t + (3*p1-p0-3*p2+p3)*t*t*t)
    width = 3 if flags == 0 else 4
    for i in range(count - 1):
        a, b = values[i*width:(i+1)*width], values[(i+1)*width:(i+2)*width]
        if a[0] <= time <= b[0]:
            t = (time - a[0]) / 30.0
            d = 30.0 / (b[0] - a[0])
            return ((2*t*t*t*d*d*d - 3*t*t*d*d + 1)*a[1]
                    + (-2*t*t*t*d*d*d + 3*t*t*d*d)*b[1]
                    + (t*t*t*d*d - 2*t*t*d + t)*a[-1]
                    + (t*t*t*d*d - t*t*d)*b[2])
    return values[(count - 1)*width + 1]


def matrices(motion, time):
    result = []
    for _group, parent, channels in motion['tracks']:
        s, r, t = [[value(motion, block, channel, time) for channel in axes] for block, axes in enumerate(channels)]
        sx, sy, sz = map(math.sin, r)
        cx, cy, cz = map(math.cos, r)
        local = [[cy*cz*s[0], (sx*sy*cz-cx*sz)*s[1], (cx*cz*sy+sx*sz)*s[2], t[0]],
                 [cy*sz*s[0], (sx*sy*sz+cx*cz)*s[1], (cx*sy*sz-sx*cz)*s[2], t[1]],
                 [-sy*s[0], sx*cy*s[1], cx*cy*s[2], t[2]]]
        if parent >= 0:
            a = result[parent]
            local = [[sum(a[row][k]*local[k][column] for k in range(3)) + (a[row][3] if column == 3 else 0) for column in range(4)] for row in range(3)]
        result.append(local)
    return result


def verify_matrices(source, result):
    error = 0.0
    for half_frame in range(2 * source['frames'] - 1):
        time = half_frame * 0.5
        a, b = matrices(source, time), matrices(result, time)
        for joint in COLLISION_ANCESTORS:
            error = max(error, *(abs(x-y) for row_a, row_b in zip(a[joint], b[joint]) for x, y in zip(row_a, row_b)))
    if error > 1e-5:
        raise ValueError(f'evaluated collision matrix error {error}')
    return error


def literal(value):
    text = format(value, '.9g')
    if '.' not in text and 'e' not in text:
        text += '.0'
    if struct.pack('>f', float(text)) != struct.pack('>f', value):
        raise ValueError('decimal output failed binary32 round trip')
    return text + 'f'


def emit(motions, mapping, hashes):
    lines = ['// Generated by games/pikmin1/tools/scenarios/extract_chappy_collision_motion.py.',
             '// Numeric collision-joint channels only; no original motion names or raw assets.',
             '// MOD: chunk 0x60 rest SRT; ANM: bundle -> DCA/DCK payload -> repacked AnimParam channels.',
             '// Selected ancestor joints: ' + ', '.join(map(str, sorted(COLLISION_ANCESTORS))) + '.']
    lines += ['// SHA-256 ' + name + ': ' + digest for name, digest in hashes.items()]
    lines += ['', 'const ChappyMotion kChappyMotions[] = {']
    for index, motion in enumerate(motions):
        lines += [f'\t// Bundle entry {index}; aliases are listed below.', f'\t{{ {motion["kind"]}, {{ {motion["frames"]},']
        for block in motion['blocks']:
            lines.append('\t\t{')
            for at in range(0, len(block), 8):
                lines.append('\t\t\t' + ', '.join(literal(v) for v in block[at:at+8]) + ',')
            lines.append('\t\t},')
        lines.append('\t\t{')
        for group, parent, channels in motion['tracks']:
            axes = ', '.join('{ ' + ', '.join('{ ' + ', '.join(map(str, c)) + ' }' for c in block) + ' }' for block in channels)
            lines.append(f'\t\t\t{{ {group}, {parent}, {axes} }},')
        lines += ['\t\t}', '\t} },']
    lines += ['};', '', 'constexpr int kChappyInfoMotion[] = { ' + ', '.join(map(str, mapping)) + ' };', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('assets', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--verify-payload-dir', type=Path)
    args = parser.parse_args()
    paths = ['tekis/chappy/chappy.mod', 'tekis/chappy/chappy.anm', 'tekikeys/chappy.key']
    raw = [(args.assets / 'dataDir' / name).read_bytes() for name in paths]
    rest = model_rest(raw[0])
    for joint in COLLISION_ANCESTORS:
        parent = rest[joint][0]
        if parent >= 0 and parent not in COLLISION_ANCESTORS:
            raise ValueError('selected joints are not ancestor-closed')
    source, names = bundle(raw[1])
    mapping = info_mapping(raw[2], names)
    if tuple(source[index]['frames'] for index in mapping) != INFO_FRAMES:
        raise ValueError('disc frames differ from measured object profiles')
    compact = [repack(motion, rest) for motion in source]
    max_error = max(verify_matrices(a, b) for a, b in zip(source, compact))
    hashes = {name: hashlib.sha256(data).hexdigest() for name, data in zip(paths, raw)}
    text = emit(compact, mapping, hashes)
    args.output.write_text(text)
    if args.verify_payload_dir:
        for info, index in enumerate(mapping):
            result = payload((args.verify_payload_dir / f'info{info:02d}.bin').read_bytes(), source[index]['kind'])
            verify_channels(source[index], result, rest)
            max_error = max(max_error, verify_matrices(source[index], result))
    print(f'{len(source)} shared payloads, {len(mapping)} infos, {sum(len(b) for m in compact for b in m["blocks"])} retained floats; '
          f'{len(text.encode())} source bytes; selected channels binary32-exact; max evaluated matrix error {max_error:g}')


if __name__ == '__main__':
    main()
