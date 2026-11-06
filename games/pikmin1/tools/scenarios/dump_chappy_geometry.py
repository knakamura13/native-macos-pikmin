#!/usr/bin/env python3
"""Read-only disc evidence: MOD joint SRT and bundle frame counts, never mesh/curve data.

Usage: python3 dump_chappy_geometry.py /path/to/install/assets
Decode follows nectar_assets/src/mod.cpp (chunk 0x60) and bundle.cpp.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def dump(assets):
    model = assets / "dataDir/tekis/chappy/chappy.mod"
    data = model.read_bytes()
    joints = []
    pos = 0
    while pos + 8 <= len(data):
        kind, length = struct.unpack_from(">II", data, pos)
        end = pos + 8 + length
        if end > len(data):
            raise ValueError("truncated MOD chunk")
        if kind == 0x60:
            count = struct.unpack_from(">i", data, pos + 8)[0]
            at = (pos + 12 + 31) & ~31
            for _ in range(count):
                parent, _flags = struct.unpack_from(">iI", data, at)
                at += 8 + 24 + 4  # bounds and unused radius
                scale, rotation, translation = [struct.unpack_from(">3f", data, at + k * 12) for k in range(3)]
                at += 36
                polys = struct.unpack_from(">i", data, at)[0]
                at += 4 + polys * 4
                joints.append(dict(parent=parent, scale=scale, rotation=rotation, translation=translation))
            if at > end:
                raise ValueError("joint records exceed MOD chunk")
        if kind == 0xffff:
            break
        pos = end
    if len(joints) != 17:
        raise ValueError("expected Chappy's 17 joints")

    bundle = assets / "dataDir/tekis/chappy/chappy.anm"
    data = bundle.read_bytes()
    count = struct.unpack_from(">I", data)[0]
    at = 4
    motions = []
    for index in range(count):
        kind, length, name_length = struct.unpack_from(">iii", data, at)
        at += 12 + name_length  # no original animation names retained
        joint_count, frame_count = struct.unpack_from(">ii", data, at)
        motions.append(dict(entry=index, type=kind, joints=joint_count, frames=frame_count))
        at += length
    if at != len(data):
        raise ValueError("bundle size mismatch")
    paths = [model, bundle, assets / "dataDir/tekikeys/chappy.key",
             assets / "dataDir/pikis/animMgr.bin", assets / "dataDir/pikis/bluModel.anm"]
    return dict(sha256={str(p.relative_to(assets)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
                joints=joints, bundle_entries=motions)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("assets", type=Path)
    print(json.dumps(dump(parser.parse_args().assets), indent=2))
