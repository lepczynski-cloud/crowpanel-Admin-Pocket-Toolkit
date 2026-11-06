#!/usr/bin/env python3
"""Fail Cloudflare Pages deployment on missing or inconsistent generated artifacts."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--site-dir', default='dist')
parser.add_argument('--commit', default=None)
args = parser.parse_args()
SITE = ROOT / args.site_dir
RELEASE_DIR = SITE / 'firmware'
release = json.loads((RELEASE_DIR / 'release.json').read_text(encoding='utf-8'))
assert release['chip_family'] == 'ESP32', 'Wrong chip family'
assert release['environment'] == 'crowpanel_28', 'Wrong PlatformIO environment'
assert release['board'].startswith('Elecrow CrowPanel 2.8'), 'Wrong hardware model'
if args.commit:
    assert release['source_commit'] == args.commit, (
        f"Commit mismatch: firmware={release['source_commit']}, expected={args.commit}"
    )

for manifest_name, bin_name, hash_key, offset in [
    ('manifest-factory.json', 'factory.bin', 'factory_sha256', 0),
    ('manifest-update.json', 'firmware.bin', 'firmware_sha256', 0x10000),
]:
    obj = json.loads((RELEASE_DIR / manifest_name).read_text(encoding='utf-8'))
    assert obj['version'] == release['version'], 'Manifest version differs from release'
    builds = obj['builds']
    assert len(builds) == 1 and builds[0]['chipFamily'] == 'ESP32', 'Wrong chip family in manifest'
    assert builds[0]['parts'] == [{'path': bin_name, 'offset': offset}], 'Wrong flash map'
    binary = RELEASE_DIR / bin_name
    assert binary.is_file() and binary.stat().st_size > 50000, f'Firmware missing/short: {binary}'
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    assert digest == release[hash_key], f'SHA-256 mismatch: {bin_name}'
    assert binary.stat().st_size == release[bin_name.replace('.bin', '_size_bytes')], 'Binary size mismatch'

assert (SITE / 'index.html').is_file(), 'Site index missing'
assert (SITE / 'assets/main.js').is_file(), 'Site JavaScript missing'
assert (SITE / 'assets/style.css').is_file(), 'Site CSS missing'
assert (SITE / 'vendor/esp-web-tools/install-button.js').stat().st_size > 1000, 'Locally hosted ESP Web Tools missing'
assert not (SITE / '.github').exists(), 'GitHub workflows must not be published'
assert not (SITE / 'platformio.ini').exists(), 'Source repository accidentally published'
cover = SITE / 'assets/device-photo.jpg'
if cover.exists():
    data = cover.read_bytes()
    assert len(data) > 10000 and data[:2] == b'\xff\xd8' and data[-2:] == b'\xff\xd9', 'Invalid photo'
else:
    assert 'https://i.ytimg.com/vi/In7P1azFg-0/hqdefault.jpg' in (SITE / 'assets/main.js').read_text(encoding='utf-8'), 'Missing image fallback'
print(f'OK: Cloudflare site and ESP32 firmware verified from commit {release["source_commit"]}')
