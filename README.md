# CrowPanel Admin Pocket Toolkit

A standalone, offline toolkit for the Elecrow CrowPanel 2.8-inch ESP32 Miner/HMI display.

## Included tools

- CIDR/subnet calculator
- Visual chmod calculator
- HTTP status and common TCP/UDP port reference

The project does not use Wi-Fi, a cloud account, a backend, a microSD card, extra buttons, or soldering.

## Target hardware

- Elecrow CrowPanel 2.8-inch ESP32 Miner/HMI
- ESP32-WROOM-32-N4
- ILI9341-compatible 320x240 TFT
- resistive touch controller

The pinout and default touch calibration are based on Elecrow's V2.1 PlatformIO example.

## Recommended IDE: JetBrains CLion + PlatformIO

1. Install the current CLion version using JetBrains Toolbox.
2. In CLion, check `Settings | Plugins` and make sure **PlatformIO for CLion** is enabled. In current CLion versions it is bundled.
3. Install PlatformIO Core on your computer:

   macOS/Linux:

   ```bash
   python3 -m pip install --user platformio
   ```

   Windows PowerShell:

   ```powershell
   py -m pip install --user platformio
   ```

4. Restart CLion.
5. Extract this project.
6. In CLion select `File | Open`, choose this directory or its `platformio.ini`, and open it as a project.
7. Wait for PlatformIO to download the ESP32 platform and TFT_eSPI dependency.
8. Connect the CrowPanel directly to the computer with a data-capable USB-C cable.
9. In the PlatformIO tool window select **Upload**, or right-click `platformio.ini` and choose `PlatformIO | Upload`.
10. If CLion cannot detect the serial port automatically, select it using the COM-port switcher in the PlatformIO tool window.

After a successful upload, the CrowPanel restarts and displays the toolkit.

## Terminal commands inside CLion

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

## Upload troubleshooting

### The device is not detected

- Use a USB-C cable that supports data, not only charging.
- Try another USB port and avoid an unpowered hub.
- Check whether a new serial device appears in `pio device list`.
- On Windows, install the USB-to-serial driver reported by Device Manager if required.

### Upload does not start

Elecrow V2.1 boards support automatic download mode. If your earlier hardware revision does not enter it automatically:

1. Hold **BOOT**.
2. Start Upload in CLion.
3. Release **BOOT** when the console shows `Connecting...`.
4. Press **RESET** after the upload if the application does not start automatically.

### Upload is unstable

Change this line in `platformio.ini`:

```ini
upload_speed = 115200
```

Then upload again.

## Touch troubleshooting

This version uses the same approach as Elecrow's factory firmware: TFT_eSPI's built-in XPT2046 driver. No separate touchscreen library or Git installation is required.

The configured touch pins are:

- SCLK: GPIO 14
- MOSI: GPIO 13
- MISO: GPIO 4
- CS: GPIO 33

The default calibration is Elecrow's factory value:

```cpp
uint16_t touchCalibration[5] = {189, 3416, 359, 3439, 1};
```

If touch works but is offset:

1. Open `src/main.cpp`.
2. Change `RUN_TOUCH_CALIBRATION` from `false` to `true`.
3. Upload, then touch the corners shown on the display.
4. Open Serial Monitor at 115200 baud.
5. Copy the five printed values into `touchCalibration`.
6. Change `RUN_TOUCH_CALIBRATION` back to `false` and upload again.

Each accepted touch is also printed in Serial Monitor as `Touch x=... y=...`.

## Arduino IDE fallback

The code uses the Arduino framework, but this package is arranged as a PlatformIO project because it gives CLion reliable dependency management and one-click upload. It can be converted to an Arduino sketch, but Arduino IDE requires separate ESP32 installation and TFT_eSPI configuration, so CLion + PlatformIO is the cleaner workflow for this project.

## Privacy

The firmware does not enable Wi-Fi and does not collect or display network addresses, SSIDs, credentials, cloud resources, or any other environment-specific data.
