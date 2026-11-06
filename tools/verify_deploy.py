#!/usr/bin/env python3
"""Gate publication: fail deployment on inconsistent firmware or missing assets."""
import json, pathlib, hashlib, sys
root = pathlib.Path(__file__).resolve().parents[1]
base = root / 'web'
release = json.loads((base / 'firmware/release.json').read_text())
for manifest_path, bin_name, checksum_key in [
    ('manifest-factory.json', 'factory.bin', 'factory_sha256'),
    ('manifest-update.json', 'firmware.bin', 'firmware_sha256')]:
    manifest = json.loads((base / 'firmware' / manifest_path).read_text())
    builds = manifest.get('builds', [])
    assert len(builds) == 1 and builds[0]['chipFamily'] == 'ESP32', 'Wrong chip family'
    assert len(builds[0]['parts']) == 1 and builds[0]['parts'][0]['path'] == bin_name, 'Wrong binary path'
    p = base / 'firmware' / bin_name
    assert p.stat().st_size > 50000, f'Empty or small firmware: {p}'
    assert hashlib.sha256(p.read_bytes()).hexdigest() == release[checksum_key], 'Firmware checksum mismatch'
assert (base / 'assets/device-photo.jpg').stat().st_size > 10000, 'Missing video photo'
assert (base / 'vendor/esp-web-tools/install-button.js').stat().st_size > 1000, 'Missing locally hosted ESP Web Tools'
assert (base / 'index.html').is_file(), 'Missing website'
print('Verified: binaries, SHA-256, manifests, video image, ESP Web Tools vendor, website')
