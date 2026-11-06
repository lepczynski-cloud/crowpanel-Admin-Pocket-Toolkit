#!/usr/bin/env bash
# One Cloudflare Pages build produces both the static site and its matching firmware.
# No GitHub Actions, gh-pages branch, firmware server, or manual local compilation.
set -Eeuo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

if [[ "${CF_PAGES:-}" == "1" ]]; then
  echo "Cloudflare Pages build: branch=${CF_PAGES_BRANCH:-unknown} commit=${CF_PAGES_COMMIT_SHA:-unknown}"
fi

command -v python3 >/dev/null || { echo 'Python 3 is required' >&2; exit 1; }
command -v npm >/dev/null || { echo 'Node.js/npm is required to vendor ESP Web Tools' >&2; exit 1; }

# In Cloudflare Pages, .node-version selects Node.js 24. This guard catches
# accidental builds on the obsolete Node.js 20 runtime.
NODE_MAJOR="$(node -p 'Number(process.versions.node.split(".")[0])')"
if (( NODE_MAJOR < 24 )); then
  echo "Node.js 24+ required for the Cloudflare build (detected $(node --version)). Set NODE_VERSION=24." >&2
  exit 1
fi

# No dependency is installed into the user's computer. All of this runs
# inside Cloudflare Pages' build container on a Git push.
mkdir -p .cf-build
python3 -m venv .cf-build/venv
# shellcheck source=/dev/null
source .cf-build/venv/bin/activate
python -m pip install --disable-pip-version-check -r tools/requirements-web-publish.txt

echo '==> Compile exact device firmware with PlatformIO'
pio run -e crowpanel_28

# Copy only static site assets; firmware is created inside dist, not checked
# into Git and never independently deployed from another branch.
rm -rf dist
mkdir -p dist
cp -a web/. dist/
mkdir -p dist/firmware
python tools/publish_web_firmware.py --environment crowpanel_28 --output dist/firmware

echo '==> Vendor pinned ESP Web Tools into the same static deployment'
mkdir -p .cf-build/npm
npm install --prefix .cf-build/npm --no-save --no-audit --no-fund --ignore-scripts --no-package-lock esp-web-tools@10.4.0
VENDOR=.cf-build/npm/node_modules/esp-web-tools
test -s "$VENDOR/dist/web/install-button.js" || { echo 'ESP Web Tools distribution missing' >&2; exit 1; }
mkdir -p dist/vendor/esp-web-tools
cp -a "$VENDOR/dist/web/." dist/vendor/esp-web-tools/
if [[ -f "$VENDOR/LICENSE" ]]; then
  cp "$VENDOR/LICENSE" dist/vendor/esp-web-tools/LICENSE
fi

# Prefer a user-supplied real photo. When absent, cache the real YouTube cover
# inside this deployment. The page also has a thumbnail fallback.
python tools/fetch_video_image.py --output dist/assets/device-photo.jpg

# Cloudflare exposes the exact source revision as CF_PAGES_COMMIT_SHA.
# Fallback to local Git only for developers intentionally running this locally.
SOURCE_SHA="${CF_PAGES_COMMIT_SHA:-$(git rev-parse HEAD 2>/dev/null || true)}"
if [[ -z "$SOURCE_SHA" ]]; then
  echo 'Cannot determine source commit SHA' >&2
  exit 1
fi
python tools/stamp_site.py --site-dir dist --commit "$SOURCE_SHA"
python tools/verify_deploy.py --site-dir dist --commit "$SOURCE_SHA"

echo '==> Verified website + firmware for the same source commit:'
echo "    $SOURCE_SHA"
echo '    Pages output directory: dist'
