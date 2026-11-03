#!/usr/bin/env python3
"""Setup helper for the ESP32 Govee hub. Run `python3 scripts/hub.py --help`.

Every command auto-detects the ESP32's USB serial port; pass --port to override.
Secrets live in include/secrets.h (git-ignored) and are never printed.
"""

import argparse
import hashlib
import importlib
import json
import re
import secrets as token_gen
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SECRETS = ROOT / "include" / "secrets.h"
SECRETS_EXAMPLE = ROOT / "include" / "secrets.example.h"
DATA_DIR = ROOT / "data"
WEB_DIR = ROOT / "web"

# Must match the `spiffs` partition in no_ota.csv (see platformio.ini).
FS_OFFSET = 0x210000
FS_SIZE = 0x1E0000

# USB vendor IDs of the serial chips found on ESP32 dev boards: Silicon Labs CP210x, WCH CH340/CH9102, FTDI, Espressif.
ESP_USB_VENDORS = {0x10C4, 0x1A86, 0x0403, 0x303A}

KNOWN_SECRETS = ["WIFI_SSID", "WIFI_PASSWORD", "GOVEE_API_KEY", "SUPABASE_URL", "SUPABASE_KEY", "DEVICE_TOKEN"]
REQUIRED_SECRETS = ["WIFI_SSID", "WIFI_PASSWORD", "GOVEE_API_KEY"]


def fail(message):
    print(f"ERROR: {message}", file=sys.stderr)
    sys.exit(1)


def require_module(name, pip_name=None):
    try:
        return importlib.import_module(name)
    except ImportError:
        fail(f"Python package '{pip_name or name}' is missing. Run: {sys.executable} -m pip install -r requirements.txt")


# ---------- serial port ----------

def find_port(explicit=None):
    if explicit:
        return explicit
    list_ports = require_module("serial.tools.list_ports", "pyserial")
    candidates = [p for p in list_ports.comports() if p.vid in ESP_USB_VENDORS]
    if not candidates:
        fail(
            "No ESP32 found on USB. Check: the board is plugged in, the cable carries data (not charge-only), "
            "and on Windows the CP210x/CH340 driver is installed."
        )
    if len(candidates) > 1:
        listing = ", ".join(p.device for p in candidates)
        fail(f"More than one possible board found ({listing}). Unplug the others or pass --port.")
    return candidates[0].device


# ---------- secrets.h ----------

def read_secrets():
    if not SECRETS.exists():
        return {}
    values = {}
    for match in re.finditer(r'^#define\s+(\w+)\s+"((?:[^"\\]|\\.)*)"', SECRETS.read_text(), re.M):
        values[match.group(1)] = re.sub(r"\\(.)", r"\1", match.group(2))
    return values


def write_secret(name, value):
    if not SECRETS.exists():
        shutil.copy(SECRETS_EXAMPLE, SECRETS)
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    text = SECRETS.read_text()
    pattern = re.compile(rf'^#define\s+{name}\s+"(?:[^"\\]|\\.)*".*$', re.M)
    line = f'#define {name} "{escaped}"'
    if pattern.search(text):
        text = pattern.sub(lambda _: line, text, count=1)
    else:
        text = text.rstrip("\n") + "\n" + line + "\n"
    SECRETS.write_text(text)


# ---------- commands ----------

def cmd_doctor(args):
    print(f"Python: {sys.version.split()[0]} ({sys.executable})")
    for module, pip_name in [("serial", "pyserial"), ("esptool", "esptool"), ("platformio", "platformio"), ("littlefs", "littlefs-python")]:
        try:
            __import__(module)
            print(f"  ok   {pip_name}")
        except ImportError:
            print(f"  MISSING {pip_name}  ->  {sys.executable} -m pip install -r requirements.txt")
    print(f"Node.js: {shutil.which('node') or 'not found (only needed to rebuild the web panel)'}")

    port = find_port(args.port)
    print(f"Board port: {port}")
    result = subprocess.run([sys.executable, "-m", "esptool", "--port", port, "chip_id"], capture_output=True, text=True)
    chip = re.search(r"Chip is (.+)", result.stdout)
    mac = re.search(r"MAC: ([0-9a-f:]+)", result.stdout)
    if not chip:
        fail("Found a serial port but couldn't talk to the chip. Try holding the BOOT button while this runs.\n" + result.stdout[-800:])
    print(f"Chip: {chip.group(1)}")
    if mac:
        print(f"MAC: {mac.group(1)}")


def cmd_set(args):
    if args.name not in KNOWN_SECRETS:
        fail(f"Unknown setting {args.name}. Expected one of: {', '.join(KNOWN_SECRETS)}")
    write_secret(args.name, args.value)
    print(f"Saved {args.name} to include/secrets.h")


def cmd_check(args):
    values = read_secrets()
    if not values:
        fail("include/secrets.h doesn't exist yet. Set values with: python3 scripts/hub.py set WIFI_SSID \"...\"")
    placeholder = re.compile(r"^your-")
    missing = []
    for name in KNOWN_SECRETS:
        value = values.get(name, "")
        filled = bool(value) and not placeholder.match(value)
        status = "set" if filled else ("optional, empty" if name not in REQUIRED_SECRETS else "MISSING")
        if name in REQUIRED_SECRETS and not filled:
            missing.append(name)
        print(f"  {name:14} {status}")
    if values.get("GOVEE_API_KEY") and len(values["GOVEE_API_KEY"]) != 36:
        print("  warning: GOVEE_API_KEY is usually 36 characters; double-check it was pasted fully")
    cloud = [values.get(n) for n in ("SUPABASE_URL", "SUPABASE_KEY", "DEVICE_TOKEN")]
    if any(cloud) and not all(cloud):
        print("  warning: Lovable/Supabase sync needs all of SUPABASE_URL, SUPABASE_KEY and DEVICE_TOKEN")
    if missing:
        sys.exit(1)


def cmd_flash(args):
    cmd_check(argparse.Namespace())
    port = find_port(args.port)
    subprocess.run([sys.executable, "-m", "platformio", "run", "-t", "upload", "--upload-port", port], cwd=ROOT, check=True)


def cmd_upload_web(args):
    littlefs = require_module("littlefs", "littlefs-python")
    if args.build or not DATA_DIR.is_dir():
        npm = shutil.which("npm")
        if not npm:
            fail("Node.js/npm is needed to build the web panel (https://nodejs.org).")
        if not (WEB_DIR / "node_modules").is_dir():
            subprocess.run([npm, "ci"], cwd=WEB_DIR, check=True)
        subprocess.run([npm, "run", "build"], cwd=WEB_DIR, check=True)

    fs = littlefs.LittleFS(
        block_size=4096, block_count=FS_SIZE // 4096, read_size=128, prog_size=128,
        lookahead_size=128, block_cycles=512, name_max=64, disk_version=0x00020000,
    )
    for path in sorted(DATA_DIR.rglob("*")):
        target = "/" + path.relative_to(DATA_DIR).as_posix()
        if path.is_dir():
            fs.makedirs(target, exist_ok=True)
        else:
            with fs.open(target, "wb") as f:
                f.write(path.read_bytes())
            print(f"  {target} ({path.stat().st_size} bytes)")

    image = ROOT / ".pio" / "littlefs.bin"
    image.parent.mkdir(exist_ok=True)
    image.write_bytes(fs.context.buffer)

    port = find_port(args.port)
    subprocess.run([sys.executable, "-m", "esptool", "--port", port, "--baud", "921600",
                    "write_flash", hex(FS_OFFSET), str(image)], check=True)


def cmd_monitor(args):
    serial = require_module("serial", "pyserial")
    port = find_port(args.port)
    conn = serial.Serial(port, 115200, timeout=0.3)
    if args.reset:
        conn.dtr = False
        conn.rts = True
        time.sleep(0.1)
        conn.rts = False

    deadline = time.time() + args.seconds
    buffer = b""
    while time.time() < deadline:
        buffer += conn.read(1024)
        while b"\n" in buffer:
            line, buffer = buffer.split(b"\n", 1)
            text = line.decode(errors="replace").rstrip()
            print(text, flush=True)
            if args.until and args.until in text:
                return
    if args.until:
        fail(f'Timed out after {args.seconds}s waiting for "{args.until}"')


def lovable_prompt(token_hash):
    return f"""Register my ESP32 hub so it can sync with this app. Run this as a data insert, once:

insert into public.devices (name, token_hash)
values ('ESP32 hub', '{token_hash}');"""


def cmd_token(args):
    values = read_secrets()
    token = values.get("DEVICE_TOKEN", "")
    if not token or args.rotate:
        token = token_gen.token_hex(32)
        write_secret("DEVICE_TOKEN", token)
        print("Generated a new DEVICE_TOKEN and saved it to include/secrets.h (reflash to apply).")
    token_hash = hashlib.sha256(token.encode()).hexdigest()
    print("\nHub code (safe to share; the token itself stays secret):\n")
    print(f"  {token_hash}\n")
    print('Paste it into the app\'s "Set up your hub" page and press Register.')
    print("If the app has no such page, paste this into the Lovable project's chat instead:\n")
    print(lovable_prompt(token_hash))


def cmd_verify_cloud(args):
    values = read_secrets()
    url, key, token = (values.get(n, "") for n in ("SUPABASE_URL", "SUPABASE_KEY", "DEVICE_TOKEN"))
    if not (url and key and token):
        fail("Set SUPABASE_URL, SUPABASE_KEY and DEVICE_TOKEN first (see AGENTS.md, step 7).")
    request = urllib.request.Request(
        url.rstrip("/") + "/rest/v1/rpc/device_sync",
        data=json.dumps({"p_token": token}).encode(),
        headers={"apikey": key, "Content-Type": "application/json", "User-Agent": "esp32-govee-hub-setup/1.0"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(request, timeout=15) as response:
            body = json.loads(response.read())
            print(f"OK: backend accepted this hub's token ({len(body.get('commands', []))} queued command(s) claimed).")
    except urllib.error.HTTPError as error:
        detail = error.read().decode(errors="replace")
        if error.code == 403 and "invalid device token" in detail:
            fail("The backend is reachable but doesn't know this hub. Paste the prompt from `hub.py token` into Lovable.")
        if error.code == 404:
            fail("device_sync() doesn't exist in this backend. The Lovable project is missing supabase/setup.sql.")
        fail(f"HTTP {error.code}: {detail}")
    except urllib.error.URLError as error:
        fail(f"Couldn't reach {url}: {error.reason}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", help="serial port (auto-detected if omitted)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("doctor", help="check tools and find the board")

    p = sub.add_parser("set", help="save a value in include/secrets.h")
    p.add_argument("name", choices=KNOWN_SECRETS)
    p.add_argument("value")

    sub.add_parser("check", help="show which settings are filled in (never prints values)")
    sub.add_parser("flash", help="build and upload the firmware")

    p = sub.add_parser("upload-web", help="upload the phone control panel to the board")
    p.add_argument("--build", action="store_true", help="rebuild web/ first (needs Node.js)")

    p = sub.add_parser("monitor", help="print the board's serial output")
    p.add_argument("--seconds", type=int, default=30)
    p.add_argument("--until", help="stop once a line contains this text")
    p.add_argument("--reset", action="store_true", help="restart the board first to see its boot log")

    p = sub.add_parser("token", help="create the hub's device token and print the Lovable registration prompt")
    p.add_argument("--rotate", action="store_true", help="replace an existing token")

    sub.add_parser("verify-cloud", help="check the Lovable/Supabase backend accepts this hub")

    args = parser.parse_args()
    handlers = {
        "doctor": cmd_doctor, "set": cmd_set, "check": cmd_check, "flash": cmd_flash,
        "upload-web": cmd_upload_web, "monitor": cmd_monitor, "token": cmd_token, "verify-cloud": cmd_verify_cloud,
    }
    handlers[args.command](args)


if __name__ == "__main__":
    main()
