# ESP32 Govee Hub

Turn a ~$10 ESP32 board into a hub that controls your Govee lights: from a phone page on your home WiFi, and
from your own Lovable app anywhere in the world.

![How it works](docs/how-it-works.png)

## What you need

- **A classic ESP32 dev board with USB-C** ("ESP32 DevKit", ESP32-WROOM-32, 4 MB flash). Other ESP32 variants
  (S3, C3, ...) need changes to `platformio.ini` and aren't covered here.
- **A USB-C cable that carries data**, not a charge-only cable. This is the #1 reason a board isn't detected.
- **Govee WiFi lights**, set up in the Govee Home app. Bluetooth-only Govee lights won't work.
- **A Govee API key**: Govee Home app → Profile → Settings → Apply for API Key (arrives by email).
- **A 2.4 GHz WiFi network** (most routers have one).
- **A computer** with [Cursor](https://cursor.com) or [Claude Code](https://claude.com/claude-code), and Python 3.
  Node.js too if you want to rebuild the phone panel.
- **Optional: a Lovable account** to control the lights from anywhere.

## Setup (let your AI agent do it)

1. Clone this repo and open the folder in Cursor or Claude Code.
2. Plug the ESP32 into your computer.
3. Tell the agent: **"Set up my ESP32 Govee hub."**

The agent follows [`AGENTS.md`](AGENTS.md): it finds the board, asks for your WiFi and Govee key, flashes the
firmware, tests it, and uploads the phone control panel. Your secrets stay in `include/secrets.h` on your
computer and the board; that file is git-ignored.

When it's done, plug the board into any USB phone charger and leave it. It reconnects on its own after power cuts
and WiFi drops.

## Control from anywhere with Lovable

1. Remix the template app: **REMIX_LINK_HERE**, publish it, and sign up.
2. Open **Set up your hub** in the app and copy the message it shows to your agent.
3. The agent gives you back a **hub code**. Paste it into the same page and press **Register**. Done.

Prefer to build your own app? Use [`docs/lovable-app-prompt.md`](docs/lovable-app-prompt.md).

## How it works

- The ESP32 joins your WiFi, finds your Govee lights, and controls them directly over your network (Govee's LAN
  API) or through Govee's cloud for lights without LAN Control.
- It serves a small control panel at `http://<board-ip>` for anyone on your WiFi.
- For the Lovable app, Supabase acts as a mailbox: the app drops commands in, and the ESP32 checks every 2 seconds,
  runs them, and writes back the result and each light's state. The ESP32 only makes outgoing requests, so
  nothing on the internet can reach into your home network.

| Path | What's there |
|---|---|
| `src/` | ESP32 firmware (PlatformIO, Arduino framework) |
| `web/` | Phone control panel (React, Vite, Tailwind, shadcn/ui) |
| `supabase/setup.sql` | Database tables and functions the app and hub share |
| `scripts/hub.py` | Setup helper: find board, save settings, flash, monitor, connect to Lovable |
| `AGENTS.md` | Step-by-step runbook for AI agents (and humans) |

## Doing it by hand

```bash
python3 -m pip install --user -r requirements.txt
python3 scripts/hub.py doctor
python3 scripts/hub.py set WIFI_SSID "..."      # also WIFI_PASSWORD, GOVEE_API_KEY
python3 scripts/hub.py flash
python3 scripts/hub.py upload-web --build
python3 scripts/hub.py monitor --reset --until "Control panel"
```

See [`AGENTS.md`](AGENTS.md) for the full walkthrough and troubleshooting.

## License

MIT
