# Firmware is generated at deployment time

This directory intentionally contains no `*.bin` or `*.json` artifacts in Git.
The Cloudflare Pages build command `bash build-cloudflare.sh` compiles the exact
ESP32 firmware and generates in `dist/firmware/`:

- `factory.bin` (complete merged binary flashed at 0x0)
- `firmware.bin` (application at 0x10000, for compatible existing devices)
- `manifest-factory.json`
- `manifest-update.json`
- `release.json` (source commit, checksums, sizes and device identity)

Cloudflare Pages publishes `dist/` atomically. No GitHub Actions or `gh-pages`
branch is involved.
