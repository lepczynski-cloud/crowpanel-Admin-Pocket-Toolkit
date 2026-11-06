#!/usr/bin/env python3
"""Publish the real YouTube cover image, never an AI-created hardware mockup.
The source is the author's video In7P1azFg-0. During CI the image is copied
into our own static website and served entirely from Cloudflare Pages.
"""
import pathlib, urllib.request, sys
ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / 'web/assets/device-photo.jpg'
VIDEO = 'In7P1azFg-0'
# A manually supplied real photo, if present and valid, takes priority.
if DEST.is_file():
    current = DEST.read_bytes()
    if len(current) >= 10000 and current.startswith(b'\xff\xd8') and current.endswith(b'\xff\xd9'):
        print(f'Using existing real device photo: {DEST}')
        sys.exit(0)
URLS = [f'https://i.ytimg.com/vi/{VIDEO}/maxresdefault.jpg', f'https://i.ytimg.com/vi/{VIDEO}/hqdefault.jpg']
for url in URLS:
    try:
        req = urllib.request.Request(url, headers={'User-Agent':'Mozilla/5.0 ESP32-Toolkit-Web-Publisher/1.0'})
        with urllib.request.urlopen(req, timeout=12) as response:
            data = response.read(3 * 1024 * 1024)
        if len(data) < 10000 or not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
            raise ValueError('Image validation failed')
        DEST.write_bytes(data)
        print(f'Published real video cover to {DEST} ({len(data)} bytes) from {url}')
        sys.exit(0)
    except Exception as e:
        print(f'Could not retrieve {url}: {e}', file=sys.stderr)
print('No verifiable real image is available. Stopping rather than creating a fake product picture.', file=sys.stderr)
sys.exit(1)
