#!/usr/bin/env python3
"""Make each deployed HTML page reference the JS/CSS from the exact build."""
from pathlib import Path
import argparse
import re
p = argparse.ArgumentParser()
p.add_argument('--commit', required=True)
p.add_argument('--site-dir', default='web')
args = p.parse_args()
sha = args.commit.strip()[:12]
if not re.fullmatch(r'[0-9a-fA-F]{10,12}', sha):
    raise SystemExit('Expected a Git commit SHA')
root = Path(__file__).resolve().parent.parent
index = root / args.site_dir / 'index.html'
text = index.read_text(encoding='utf-8')
for basename in ('style.css', 'main.js', 'device-photo.jpg'):
    regex = rf'(/assets/{re.escape(basename)})(?:\?v=[A-Za-z0-9_.-]+)?'
    text, replacements = re.subn(regex, rf'\g<1>?v={sha}', text)
    assert replacements >= 1, f'No link to /assets/{basename}'
index.write_text(text, encoding='utf-8')
print(f'Stamped JS/CSS with source commit {sha}')
