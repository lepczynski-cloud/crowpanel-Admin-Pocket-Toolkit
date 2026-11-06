#!/usr/bin/env bash
# Cloudflare Pages: build the firmware and website from the same Git checkout.
# Settings: production=main, command=bash build-cloudflare.sh, output=dist.
set -Eeuo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

# Get the complete commit ID once. Git is authoritative for this checkout.
# Cloudflare may not expose CF_PAGES_COMMIT_SHA during some build contexts.
SOURCE_SHA="$(git rev-parse --verify HEAD 2>/dev/null || true)"
if [[ ! "$SOURCE_SHA" =~ ^[0-9a-fA-F]{40}$ ]]; then
  SOURCE_SHA="${CF_PAGES_COMMIT_SHA:-}"
fi
if [[ ! "$SOURCE_SHA" =~ ^[0-9a-fA-F]{40}$ ]]; then
  echo "Build failed: no complete source Git SHA available" >&2
  exit 1
fi
SOURCE_SHA="${SOURCE_SHA,,}"
echo "==> Building CrowPanel firmware and website from commit $SOURCE_SHA"
if [[ -n "${CF_PAGES_COMMIT_SHA:-}" && "${CF_PAGES_COMMIT_SHA,,}" != "$SOURCE_SHA" ]]; then
  echo "Note: Cloudflare commit metadata differs from Git checkout; using checkout SHA ($SOURCE_SHA) for all artifacts."
fi

# Keep Node.js and Python on Cloudflare's maintained runtimes.
# The firmware hardware platform and libraries remain pinned in platformio.ini.
command -v python3 >/dev/null
command -v node >/dev/null
command -v npm >/dev/null
python3 --version
node --version

rm -rf dist
mkdir -p .cf-build
python3 -m venv .cf-build/venv
# shellcheck source=/dev/null
source .cf-build/venv/bin/activate
python -m pip install --disable-pip-version-check --no-input 'platformio>=6.1,<7' 'esptool>=4.8,<5'

python -m platformio run -e crowpanel_28
mkdir -p dist
cp -a web/. dist/

# Explicit --commit ensures release.json, manifest versions, and final validation
# all use the same full SHA even when Cloudflare's environment variable is absent.
python tools/publish_web_firmware.py --environment crowpanel_28 --output dist/firmware --commit "$SOURCE_SHA"

# Vendored at build time, not loaded from a CDN when users flash their device.
npm install --prefix .cf-build/npm --no-save --no-audit --no-fund --ignore-scripts --no-package-lock 'esp-web-tools@10.4.0'
VENDOR='.cf-build/npm/node_modules/esp-web-tools'
test -s "$VENDOR/dist/web/install-button.js" || { echo 'ESP Web Tools missing' >&2; exit 1; }
mkdir -p dist/vendor/esp-web-tools
cp -a "$VENDOR/dist/web/." dist/vendor/esp-web-tools/
if [[ -f "$VENDOR/LICENSE" ]]; then cp "$VENDOR/LICENSE" dist/vendor/esp-web-tools/LICENSE; fi

# The image comes from the actual project video; failure only affects the photo.
python tools/fetch_video_image.py --output dist/assets/device-photo.jpg
python tools/stamp_site.py --site-dir dist --commit "$SOURCE_SHA"
python tools/verify_deploy.py --site-dir dist --commit "$SOURCE_SHA"

echo "==> Cloudflare Pages output ready: dist/"
echo "==> Verified firmware + website from Git commit $SOURCE_SHA"
