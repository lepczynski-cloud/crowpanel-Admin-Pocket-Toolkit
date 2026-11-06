# CrowPanel Admin Pocket Toolkit

<!-- ESP32-WEB-INSTALL-START -->

## Install the latest ESP32 firmware from your browser

**[ESP32 Admin Pocket Toolkit - official device page and installer](https://esp32-admin-toolkit.lepczynski.it/)**

Use the Elecrow **2.8-inch CrowPanel ESP32-WROOM-32** and a USB data cable. Open the website over HTTPS in a supported desktop browser (Chrome or Edge). Choose the USB serial port and use **Full install**. You do **not** need to install PlatformIO, CLion or another local build environment.

The website serves firmware that Cloudflare Pages **automatically compiles from the current `main` source commit**. The firmware binary, checksums, manifest and site are published together only when the build succeeds. If you want to customize the firmware, the existing PlatformIO instructions below are still available.

**Deployment note:** Cloudflare Pages uses the `main` branch, build command `bash build-cloudflare.sh`, and output directory `dist`. Firmware, checksums and manifests are generated during the Cloudflare build and deployed atomically with the static site. There are no Cloudflare Pages. See [DEPLOYMENT.md](DEPLOYMENT.md) for the exact setup.

**[Watch the complete real-hardware demonstration](https://www.youtube.com/watch?v=In7P1azFg-0)** · **[Online browser-only Admin Toolkit](https://admintoolkit.lepczynski.it/)**

Do not use this firmware on a different ESP32 board or display variant. Flashing can overwrite existing software and settings.

<!-- ESP32-WEB-INSTALL-END -->

Offline admin utilities for the Elecrow CrowPanel 2.8-inch ESP32 Miner/HMI display.

This project turns the small ESP32 touchscreen into a pocket-sized toolkit for everyday administration, networking, time-zone checks, cron schedules, storage planning, backups and quick wireless field checks.

## Demo and related links

v2 - 8 apps

[![CrowPanel Admin Pocket Toolkit demo v2 ](https://img.youtube.com/vi/In7P1azFg-0/hqdefault.jpg)](https://www.youtube.com/watch?v=In7P1azFg-0)

Earlier v1 (3 tools): [watch on YouTube](https://www.youtube.com/watch?v=lrTHDVkG1Hk)


- Online toolkit: <https://admintoolkit.lepczynski.it/>
- Blog: <https://lepczynski.it/>
- Repository: <https://github.com/lepczynski-cloud/crowpanel-Admin-Pocket-Toolkit>

GitHub does not render embedded YouTube iframes in README files, so the image above works as a clickable video preview.

## UX goals in this version

- all main tools fit on the first screen;
- larger touch targets for the 320 x 240 resistive display;
- warm dark color palette instead of blue highlights and bright white text;
- shared numeric keypad for values that are hard to adjust with small plus/minus buttons;
- Storage and Backup are combined under one **Capacity** tool;
- list-based selectors replace ambiguous left/right cycling for cron presets and backup policies;
- Wi-Fi and BLE radios stay off until explicitly enabled from the touchscreen.

## Included tools

### CIDR Calculator

- editable IPv4 address;
- editable prefix;
- prefix can be changed with dedicated `-` and `+` buttons below the prefix value, or typed with the numeric keypad;
- network address, broadcast address, first host, last host, subnet mask, usable hosts and total addresses.

### VLSM Planner

- uses the current CIDR network as the base network;
- four touch-friendly LAN rows with `-`, value and `+` controls;
- tapping the host value opens the numeric keypad;
- `EDIT CIDR` opens the shared CIDR editor and returns directly to VLSM when finished;
- allocates the largest networks first and proposes prefixes for each LAN.

### World Clock / Time Converter

- fixed offline zones: CEST, HKT and Zulu/UTC;
- one custom extra clock with adjustable UTC offset;
- tapping a zone selects it for editing;
- `SET HHMM` opens the numeric keypad;
- changing the selected zone updates all other zones;
- the screen updates only the time rows, so it does not blink every second;
- starts at 12:00 Zulu after boot because the firmware does not use NTP or a battery-backed RTC.

### Cron Generator & Parser

- common cron presets;
- five visibly separated Unix cron fields with matching `MIN`, `HOUR`, `DOM`, `MON` and `DOW` labels;
- plain-English explanation of the selected schedule;
- tapping the preset opens a full-screen preset selector;
- tapping any expression field opens a touch editor for digits and the `*`, `/`, `-`, `,` symbols;
- touch rows remain available for preset parameters such as interval, hour and minute.

### Capacity Planner

Storage tab:

- daily data volume;
- retention period;
- compression factor;
- number of copies;
- free-space headroom;
- required capacity and raw capacity before spare space.

Backup tab:

- full backup size;
- average incremental backup size;
- backup frequency;
- copy count;
- backup frequency and retention are selected from full-screen lists instead of being cycled blindly;
- ready-made retention schemes:
  - 7 daily / 4 weekly / 12 monthly;
  - 14 daily / 8 weekly / 12 monthly;
  - Grandfather-Father-Son;
- custom daily, weekly and monthly restore-point counts with direct numeric entry.

### chmod Calculator

- visual owner/group/other permissions;
- numeric and symbolic output;
- common presets;
- tapping the `chmod` result opens the numeric keypad for direct mode entry.

### Wireless Scanner

- Wi-Fi network scan without entering passwords;
- SSID, signal quality, dBm, channel and security type;
- signal bars and labels: Great, Good, Fair, Weak;
- 2.4 GHz channel analysis;
- BLE advertisement scanner with signal bars;
- active and passive scan modes for both Wi-Fi and BLE;
- tapping a Wi-Fi network opens a dedicated signal monitor that repeatedly checks only the selected channel and BSSID;
- large signal-quality bar, percentage, dBm value, quality label and short history graph in monitor mode;
- Wi-Fi and BLE are off by default and can be enabled from the touchscreen only when needed.

### Quick Reference

- HTTP status codes;
- common TCP/UDP ports.

## Target hardware

- Elecrow CrowPanel 2.8-inch ESP32 Miner/HMI display;
- ESP32-WROOM-32-N4;
- ILI9341-compatible 320 x 240 TFT;
- resistive touch controller;
- built-in Wi-Fi and Bluetooth/BLE.

The pinout and default touch calibration are based on Elecrow's PlatformIO example for this device family.

## Privacy and offline behavior

Most tools are fully offline calculators and do not need networking.

The wireless scanner does not connect to Wi-Fi networks and does not ask for SSIDs or passwords. It scans visible Wi-Fi networks and nearby BLE advertisements. Wi-Fi and BLE are disabled by default to reduce power usage and unnecessary radio activity.

Active scanning may send normal discovery requests to obtain more complete information. Passive mode only listens and can be selected directly on the Wireless screen.

The clock tool is also offline. It does not call NTP and does not know the real time after power loss. It starts at 12:00 Zulu after boot, then keeps counting using the ESP32 runtime clock. You can adjust any displayed time manually.

## Recommended IDE: JetBrains CLion + PlatformIO

1. Install the current CLion version using JetBrains Toolbox.
2. In CLion, check `Settings | Plugins` and make sure **PlatformIO for CLion** is enabled.
3. Install PlatformIO Core on your computer.

macOS/Linux:

```bash
python3 -m pip install --user platformio
```

Windows PowerShell:

```powershell
py -m pip install --user platformio
```

4. Restart CLion.
5. Clone or download this repository.
6. In CLion select `File | Open`, choose this directory or its `platformio.ini`, and open it as a project.
7. Wait for PlatformIO to download the ESP32 platform, TFT_eSPI and the pinned NimBLE-Arduino dependency.
8. Connect the CrowPanel directly to the computer with a data-capable USB-C cable.
9. In the PlatformIO tool window select **Upload**, or right-click `platformio.ini` and choose `PlatformIO | Upload`.
10. If CLion cannot detect the serial port automatically, select it using the COM-port switcher in the PlatformIO tool window.

After a successful upload, the CrowPanel restarts and displays the toolkit.

## Terminal commands

Build:

```bash
pio run
```

Upload:

```bash
pio run --target upload
```

Serial monitor:

```bash
pio device monitor
```

List detected USB serial ports:

```bash
pio device list
```

Cloudflare Pages validates pull requests and, on each push to `main`, builds and publishes the latest successful version together with the static web installer. See [DEPLOYMENT.md](DEPLOYMENT.md).

## Upload troubleshooting

### The device is not detected

- Use a USB-C cable that supports data, not only charging.
- Try another USB port and avoid an unpowered hub.
- Check whether a new serial device appears in `pio device list`.
- On Windows, install the USB-to-serial driver reported by Device Manager if required.

### Upload does not start

Elecrow V2.1 boards usually support automatic download mode. If your hardware revision does not enter it automatically:

1. Hold **BOOT**.
2. Start Upload in CLion or PlatformIO.
3. Release **BOOT** when the console shows `Connecting...`.
4. Press **RESET** after the upload if the application does not start automatically.

### Upload is unstable

Change this line in `platformio.ini`:

```ini
upload_speed = 115200
```

Then upload again.

## Touch troubleshooting

This firmware uses TFT_eSPI's built-in XPT2046 touch support. No separate touchscreen library or Git dependency is required.

Configured touch pins:

- SCLK: GPIO 14
- MOSI: GPIO 13
- MISO: GPIO 4
- CS: GPIO 33

Default calibration:

```cpp
uint16_t touchCalibration[5] = {189, 3416, 359, 3439, 1};
```

If touch works but is offset:

1. Open `src/main.cpp`.
2. Change `RUN_TOUCH_CALIBRATION` from `false` to `true`.
3. Upload the firmware.
4. Touch the corners shown on the display.
5. Open Serial Monitor at 115200 baud.
6. Copy the five printed values into `touchCalibration`.
7. Change `RUN_TOUCH_CALIBRATION` back to `false` and upload again.

Each accepted touch is also printed in Serial Monitor as `Touch x=... y=...`.

## Notes and limitations

- The small 320 x 240 display is best for quick estimates and field checks, not for long text input.
- The numeric keypad is intentionally simple: digits, delete, clear, min, max, OK and cancel.
- The Cron editor works field by field rather than through a full keyboard, which keeps arbitrary five-field expressions practical on a resistive touchscreen.
- The Backup tab is a planning calculator. It gives an estimate, not a replacement for testing backup software with real retention policies.
- The wireless scanner does not connect to Wi-Fi networks and does not authenticate to BLE devices. Active and passive discovery modes are both available.
- Wi-Fi and BLE are treated as separate modes. Enabling one radio view disables the other to keep memory usage and power draw predictable.

## License and attribution

This project is released under the MIT License. You can use, modify and share it.

Attribution is not required beyond the license terms, but it would be appreciated if public forks, videos or derivative projects mention Wojciech Lepczynski as the original author and link to the blog or the online toolkit.


### Website photo

The installer website uses a real image from the project's [YouTube demonstration](https://www.youtube.com/watch?v=In7P1azFg-0). During publishing, Cloudflare Pages tries to cache the video cover as `web/assets/device-photo.jpg`. You can also add your own real hardware photo at that path to ship it entirely with the static website.

If no local image is available, the browser uses YouTube's official cover image instead of showing a missing-photo placeholder. This only affects the website image; installation and firmware remain completely independent.
