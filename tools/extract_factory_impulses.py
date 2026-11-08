#!/usr/bin/env python3
"""Copy the three verified IR resources from the user's local, owned DLL.

This only reads bytes; it never loads or executes the original plugin. The
output is private runtime data, not part of the source or cloud build.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import wave

DLL_SHA256 = "e5618ec7cf1da65a21d17fbe1054792db6a2d8c99f6c1e0e08aeaa0df59ef533"
RESOURCES = {
    "Room.wav": (0x1b81260, 115442, 18892,
                 "f32926b50f9f140ee66ad5ba6002c3d2cf545a52d675234db05f744065407dcc", 48000),
    "Plate.wav": (0x14d8150, 612938, 101808,
                  "207fe7ae2e07ae3905c1d1c6accf46dfb4f10efee31d431073c7afd5a7b37872", 48000),
    "Drive.wav": (0x15ed790, 2298, 32,
                  "34f70f831ddef41e7d287beaa80adbae97d07df963e9fc55567f7f51aa340339", 96000),
}

def extract(source: Path, destination: Path):
    data = source.read_bytes()
    if hashlib.sha256(data).hexdigest() != DLL_SHA256:
        raise ValueError("Unsupported DLL: SHA-256 does not match the inspected version")
    resources = {}
    for name, (offset, size, frames, expected_hash, source_rate) in RESOURCES.items():
        blob = data[offset:offset + size]
        if hashlib.sha256(blob).hexdigest() != expected_hash:
            raise ValueError(f"Resource hash mismatch: {name}")
        with wave.open(io.BytesIO(blob), 'rb') as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(),
                    wav.getnframes(), wav.getcomptype()) != (2, 3, source_rate, frames, 'NONE'):
                raise ValueError(f"Unexpected WAV format: {name}")
        resources[name] = blob
    destination.mkdir(parents=True, exist_ok=True)
    if any(destination.iterdir()):
        raise ValueError("Destination must be empty; existing private assets are never overwritten")
    for name, blob in resources.items():
        with (destination / name).open('xb') as output:
            output.write(blob)
    manifest = {"source_sha256": DLL_SHA256,
                "channels": 2, "bits_per_sample": 24,
                "resources": {name: {"sha256": item[3], "frames": item[2], "bytes": item[1], "sample_rate": item[4]}
                              for name, item in RESOURCES.items()}}
    with (destination / 'manifest.json').open('x') as output:
        json.dump(manifest, output, indent=2)
        output.write('\n')
    return manifest

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    print(json.dumps(extract(args.dll, args.destination), indent=2))
