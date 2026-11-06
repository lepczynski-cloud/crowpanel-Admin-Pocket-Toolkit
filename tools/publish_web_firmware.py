#!/usr/bin/env python3
"""Package the PlatformIO build for an ESP Web Tools factory install and app update.

The script deliberately does NOT compile firmware or guess the target board.
Call it only after a successful `pio run -e ENV` for the exact ESP32-WROOM-32-N4.
It refuses to publish missing or invalid build artifacts.
"""
from __future__ import annotations

import argparse
import configparser
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
EXPECTED_CHIP = "ESP32"
FIRMWARE_OFFSET = 0x10000
BOOTLOADER_OFFSET = 0x1000
PARTITIONS_OFFSET = 0x8000
OTA_BOOT_OFFSET = 0xE000


def default_environment(config_path: Path) -> str:
    config = configparser.ConfigParser(interpolation=None, strict=False)
    if not config.read(config_path):
        raise RuntimeError(f"Missing configuration: {config_path}")
    envs = config.get("platformio", "default_envs", fallback="").split()
    envs = [item.strip(" ,") for item in envs if item.strip(" ,")]
    if not envs:
        envs = [name[4:] for name in config.sections() if name.startswith("env:")]
    if len(envs) != 1:
        raise RuntimeError("Expected one default PlatformIO env. Set PIO_ENV explicitly in the GitHub workflow.")
    return envs[0]


def require_bin(path: Path, *, image: bool = False) -> Path:
    if not path.is_file():
        raise RuntimeError(f"Required build output missing: {path}")
    if path.stat().st_size < 512:
        raise RuntimeError(f"Unexpectedly short binary: {path}")
    if image and path.open("rb").read(1) != b"\xe9":
        raise RuntimeError(f"Expected Espressif image header 0xE9: {path}")
    return path


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def optional_boot_app0() -> Path | None:
    base = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32"
    if base.exists():
        files = list(base.rglob("boot_app0.bin"))
        if files:
            return sorted(files, key=lambda p: len(p.parts))[0]
    return None


def install_manifest(version: str, filename: str, offset: int) -> dict:
    return {
        "name": "Admin Pocket Toolkit",
        "version": version,
        "new_install_prompt_erase": bool(offset == 0),
        "new_install_improv_wait_time": 0,
        "builds": [{"chipFamily": EXPECTED_CHIP, "parts": [{"path": filename, "offset": offset}]}],
    }


def git_rev() -> str:
    try:
        return subprocess.check_output(["git", "rev-parse", "--short=10", "HEAD"], cwd=ROOT, text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        return os.getenv("GITHUB_SHA", "local")[:10]


def run(args: argparse.Namespace) -> None:
    if args.chip != EXPECTED_CHIP:
        raise RuntimeError("This package is for ESP32-WROOM-32-N4 only, not ESP32-S3.")
    env = args.environment or default_environment(ROOT / "platformio.ini")
    build = ROOT / ".pio" / "build" / env
    app = require_bin(build / "firmware.bin", image=True)
    boot = require_bin(build / "bootloader.bin", image=True)
    partitions = require_bin(build / "partitions.bin")
    flash_bytes = 4 * 1024 * 1024
    output = ROOT / args.output
    output.mkdir(parents=True, exist_ok=True)
    tmp_factory = output / "factory.bin.tmp"
    app_destination = output / "firmware.bin"
    shutil.copyfile(app, app_destination)
    pieces = ["0x1000", str(boot), "0x8000", str(partitions)]
    ota = optional_boot_app0()
    if ota:
        pieces += ["0xe000", str(ota)]
    else:
        print("Warning: boot_app0.bin not present, factory image will keep OTADATA erased (0xFF).", file=sys.stderr)
    pieces += ["0x10000", str(app)]
    command = [sys.executable, "-m", "esptool", "--chip", "esp32", "merge_bin", "-o", str(tmp_factory), "--flash_mode", "dio", "--flash_freq", "40m", "--flash_size", "4MB", *pieces]
    subprocess.run(command, check=True, cwd=ROOT)
    if not tmp_factory.is_file() or tmp_factory.stat().st_size < FIRMWARE_OFFSET + app.stat().st_size:
        raise RuntimeError("esptool did not create a complete merged binary")
    if tmp_factory.stat().st_size > flash_bytes:
        raise RuntimeError("Merged factory image exceeds the 4 MB device flash")
    tmp_factory.replace(output / "factory.bin")
    version = f"{datetime.now(timezone.utc):%Y.%m.%d}+{git_rev()}"
    release = {
        "name": "Admin Pocket Toolkit",
        "version": version,
        "chip_family": "ESP32",
        "board": "Elecrow CrowPanel 2.8 inch ESP32-WROOM-32-N4",
        "environment": env,
        "source_commit": os.getenv("GITHUB_SHA", git_rev()),
        "build_time_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "flash_size_bytes": flash_bytes,
        "firmware_sha256": sha256(app_destination),
        "factory_sha256": sha256(output / "factory.bin"),
        "firmware_size_bytes": app_destination.stat().st_size,
        "factory_size_bytes": (output / "factory.bin").stat().st_size,
        "boot_app0_included": bool(ota),
    }
    for filename, obj in (("manifest-factory.json", install_manifest(version, "factory.bin", 0)),
                          ("manifest-update.json", install_manifest(version, "firmware.bin", FIRMWARE_OFFSET)),
                          ("release.json", release)):
        (output / filename).write_text(json.dumps(obj, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"Published firmware for {env}: {version} factory={release['factory_size_bytes']} bytes")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", "-e", default=None)
    parser.add_argument("--chip", default="ESP32")
    parser.add_argument("--output", default="web/firmware")
    args = parser.parse_args()
    try:
        run(args)
    except (RuntimeError, OSError, subprocess.CalledProcessError) as exc:
        print(f"FIRMWARE PUBLISH FAILED: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
