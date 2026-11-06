# ESP32 Admin Pocket Toolkit: automatic firmware + Cloudflare Pages

**Recommended address:** https://esp32-admin-toolkit.lepczynski.it/

This is a static website with no application backend or runtime API. GitHub Actions builds the firmware and publishes the compiled website and matching firmware to a `gh-pages` branch. Cloudflare Pages deploys that branch. ESP Web Tools is downloaded at BUILD time and served from Cloudflare with the rest of the static website; website users do not depend on unpkg.

## Configure GitHub (once)

1. Back up your current `crowpanel-Admin-Pocket-Toolkit` repo, ideally create a new branch before replacing anything.
2. Upload the **contents of the complete ZIP delivered for this repository** to `main`, including hidden `.github/`. The ZIP already contains your original `src/main.cpp`, `platformio.ini`, LICENSE and build workflows. It is not a patch or a generator.
3. In the repository, open **Settings > Actions > General > Workflow permissions** and enable **Read and write permissions**. If your organization restricts this setting, GitHub must grant the workflow write permission to `gh-pages`.
4. Open **Actions > Build firmware and publish ESP32 installer**. On the first run it will build C++ firmware with PlatformIO, assemble complete ESP32 flash images, verify checksums, download the real YouTube video cover, bundle ESP Web Tools, and publish `gh-pages`. No local toolchain or manual build is required.
5. Confirm that the run is green and the `gh-pages` branch exists before connecting Cloudflare.

## Configure Cloudflare Pages (once)

1. Cloudflare Dashboard > Workers & Pages > Create > Pages > Connect to Git.
2. Choose `lepczynski-cloud/crowpanel-Admin-Pocket-Toolkit`.
3. Production branch: **`gh-pages`**, NOT `main`.
4. Framework preset: **None**; Build command: **leave blank** (or use `exit 0` if the UI requires a command); Output directory: **`.`** (repository root of `gh-pages`). Root directory: default.
5. Once deployed, choose Custom domains > Add: **`esp32-admin-toolkit.lepczynski.it`**. Cloudflare manages the CNAME and certificate if the DNS zone is already in your account.
6. Confirm `/firmware/release.json`, `/firmware/manifest-factory.json` and `/assets/device-photo.jpg` return correctly.
7. Visit the HTTPS site from Chrome or Edge on a desktop and test a flash on the **correct hardware**.

## What happens on every source change?

`main` push -> PlatformIO compile -> firmware images and SHA-256 checks -> website and binary publish to `gh-pages` -> Cloudflare Pages Git deploy. The website's `release.json` has `source_commit` pointing to the build source commit. If CI compilation fails, `gh-pages` stays at the previous successful version instead of publishing incompatible firmware.

## Real image and video

The CI `tools/fetch_video_image.py` downloads the author's **real YouTube cover image** for `In7P1azFg-0` into `web/assets/device-photo.jpg` (served locally from Cloudflare). The embedded YouTube player is only loaded after a visitor clicks Play. If you want a different photograph than the real video cover, replace the downloaded asset source with a frame you own and adjust the workflow accordingly.

## Hardware support

Only the Elecrow CrowPanel **2.8-inch ESP32-WROOM-32-N4** with 320x240 display. This is a classic ESP32 (not ESP32-S3). Use a USB data cable. The installed toolkit is standalone; no PC application needed. The separate ESP32 PC Control Deck project *does* require a host companion app.

## Troubleshooting

- Build fails: read Actions logs. Original `platformio.ini` and original `src/main.cpp` must be present. Do not copy the kit alone into GitHub.
- `gh-pages` missing: check workflow write permissions; run workflow manually from Actions.
- Installer unavailable: `release.json` or compiled binaries not published yet, or unsupported browser/HTTP. Check the network panel for `/firmware/` errors.
- First website image missing: CI must download the actual video cover image before deployment.
- Empty Pages site: confirm production branch is `gh-pages` and the build output is the branch root (`.`), not `web`.
- Cloudflare does not detect automatic publish: inspect Cloudflare Git integration status for `gh-pages` pushes. A manual Cloudflare 'Retry deployment' is only for diagnosing the initial integration.

## Repository's About description (set manually in GitHub UI)

`Offline ESP32 Admin Pocket Toolkit: 8 sysadmin/networking tools on CrowPanel 2.8-inch. Install latest firmware from your browser without local builds. Open source.`

Website: https://esp32-admin-toolkit.lepczynski.it/

## What is included in the complete repository ZIP?

The complete source tree supplied by the repository owner, unchanged `src/main.cpp`, original PlatformIO configuration, both GitHub Actions workflows, static website, publish scripts and updated README. The built firmware binaries are deliberately not bundled: they will be compiled by GitHub Actions from this exact source and published to the `gh-pages` branch.

A successful CI workflow is required before the installer can flash anything. Hardware compatibility and a physical USB flashing test remain to be verified.
