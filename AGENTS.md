# Agent runbook: set up the ESP32 Govee hub

You are helping someone (often a beginner) turn a classic ESP32 dev board into a hub that controls their Govee
lights, from a phone page on their WiFi and optionally from a Lovable app anywhere. Walk them through it one
step at a time, run the commands yourself, and explain what's happening in plain language.

All helper commands are in `scripts/hub.py` (run from the repo root). It auto-detects the board's USB port.
On Windows use `py` instead of `python3`.

## Rules

- **Never print, echo, or commit secret values** (WiFi password, Govee API key, `DEVICE_TOKEN`). Write them only
  with `python3 scripts/hub.py set NAME "value"`. `include/secrets.h` is git-ignored; keep it that way.
- The Supabase URL and publishable/anon key are public by design; the SHA-256 hash of `DEVICE_TOKEN` is safe to show.
- Don't change the user's lights except for one brief, announced test (and put it back afterwards).
- If a step fails, stop and fix it before moving on. Use the troubleshooting table at the bottom.

## Step 1: tools

```bash
python3 -m pip install --user -r requirements.txt
python3 scripts/hub.py doctor
```

`doctor` checks the Python packages and finds the board. Expected output ends with something like
`Chip: ESP32-D0WD-V3` and a MAC address. If `pip` installs scripts outside PATH that's fine: everything here runs
through `python3 -m ...`. Node.js is only needed in step 5.

If no board is found, see troubleshooting (charge-only cable and missing Windows driver are the usual causes).

## Step 2: what the user needs

Ask for these (one message, plain language):

1. **WiFi name and password.** The ESP32 only joins **2.4 GHz** networks. If their router shows separate
   "Name" and "Name-5G" networks, use the one without 5G.
2. **Govee API key.** Govee Home app: Profile tab, Settings (gear), **Apply for API Key**. Govee emails it within
   minutes. It's usually 36 characters.
3. **Optional, recommended:** in the Govee Home app, open each light, tap its settings, and turn on
   **LAN Control** where it's offered. Those lights respond instantly; the rest go through Govee's cloud.

## Step 3: save settings

```bash
python3 scripts/hub.py set WIFI_SSID "their wifi name"
python3 scripts/hub.py set WIFI_PASSWORD "their wifi password"
python3 scripts/hub.py set GOVEE_API_KEY "their key"
python3 scripts/hub.py check
```

`check` lists which settings are filled in without showing them.

## Step 4: flash the firmware

```bash
python3 scripts/hub.py flash
python3 scripts/hub.py monitor --reset --seconds 60 --until "Control panel"
```

The first build downloads the ESP32 toolchain and takes a few minutes. The monitor output should show:

- `Connected. IP: ...`: on WiFi.
- `Managing N light(s):` with each light's name and either its IP (local control) or `cloud`.
- `Control panel: http://govee.local  or  http://<ip>`.

Tell the user how many lights were found and which ones are local vs cloud.

## Step 5: upload the phone control panel

```bash
python3 scripts/hub.py upload-web --build
```

Needs Node.js. Afterwards, wait ~10 seconds and have the user open `http://<ip>` (from step 4) on a phone or
computer on the same WiFi. They should see a card per light. `govee.local` may also work, but the IP is reliable.

At this point the hub works on its own: it can be unplugged and moved to any USB phone charger.

## Step 6: optional quick test

Announce it first ("I'm going to briefly change one light's brightness"), then:

```bash
curl -s http://<ip>/api/lights
curl -s -X POST http://<ip>/api/control -H 'Content-Type: application/json' -d '{"ids":["<id>"],"brightness":70}'
```

Then set it back to its previous value. On Windows PowerShell use `curl.exe`.

## Step 7: connect a Lovable app (control from anywhere)

Ask whether they remixed the template Lovable project (link in README.md) or are building their own.

- **Remixed the template:** the database tables and functions come with the remix. Have them publish it and
  sign up in the app first.
- **Building their own:** have them paste the prompt in `docs/lovable-app-prompt.md` into a new Lovable project
  with Lovable Cloud enabled.

Then:

1. Ask for the project's **URL** (`https://....supabase.co` or `https://....lovable.cloud`) and **publishable /
   anon key** (`sb_publishable_...` or a long `eyJ...`). In Lovable they're under Cloud settings, or they can ask
   Lovable's chat "what are my backend URL and publishable key?".
2. Save them and create the hub's token:

   ```bash
   python3 scripts/hub.py set SUPABASE_URL "https://..."
   python3 scripts/hub.py set SUPABASE_KEY "sb_publishable_..."
   python3 scripts/hub.py token
   ```

3. `token` prints a short prompt containing a hash. Have the user paste it into their Lovable project's chat
   (it registers the hub in their database). Wait for them to confirm Lovable ran it.
4. Verify and flash:

   ```bash
   python3 scripts/hub.py verify-cloud
   python3 scripts/hub.py flash
   python3 scripts/hub.py monitor --reset --seconds 60 --until "Supabase sync on"
   ```

5. Have them tap a light in their Lovable app, then watch for `Supabase command {...} -> done`:

   ```bash
   python3 scripts/hub.py monitor --seconds 60 --until "Supabase command"
   ```

Note: `verify-cloud` calls the same endpoint as the hub, so any commands queued at that moment are claimed by
the check instead of the hub. Run it before the hub is syncing, or expect a queued command to be skipped.

## How it works (for explaining to the user)

- `src/govee.cpp`: finds lights (Govee cloud device list + LAN multicast scan on port 4001) and controls them
  (LAN UDP on port 4003, or Govee's cloud API for lights without LAN Control).
- `src/main.cpp`: WiFi, the web server (`/api/lights`, `/api/control`, phone panel), BOOT button toggles all lights.
- `src/cloud_sync.cpp`: every 2 s calls the Supabase function `device_sync`, which reports light state and
  hands back queued commands. The hub only makes outgoing requests, so nothing on the internet can reach into
  the home network.
- `supabase/setup.sql`: the database contract (tables `lights`, `light_commands`, `devices`; functions
  `device_sync`, `hub_online`).
- `web/`: the phone control panel (React + Vite + Tailwind + shadcn/ui), stored on the board's flash.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `doctor`: no ESP32 found | Try another USB cable (many are charge-only). On Windows, install the CP210x (Silicon Labs) or CH340 (WCH) USB driver, then replug. Close any other program using the port (Arduino IDE, serial monitors). |
| `doctor`: found port but can't talk to chip | Hold the board's **BOOT** button while the command runs; release when it starts writing. |
| Upload fails partway | Same as above, or use a shorter cable / a USB port directly on the computer. |
| `WiFi connection failed` | Wrong name/password, or a 5 GHz-only network. Re-run step 3, then `flash`. Names and passwords are case-sensitive. |
| `Govee cloud ... failed (HTTP 401)` | Bad Govee API key. Re-run `set GOVEE_API_KEY`, then `flash`. |
| A light shows `cloud` instead of an IP | Turn on LAN Control for it in the Govee app (if the model offers it), then restart the hub (unplug/replug). |
| Control panel page is blank or says "isn't uploaded yet" | Run step 5. |
| Phone can't open the page | Phone must be on the same home WiFi. Use the IP, not `govee.local`. |
| `verify-cloud`: doesn't know this hub | The Lovable registration prompt from `hub.py token` wasn't run. Paste it into Lovable again. |
| `verify-cloud`: `device_sync()` doesn't exist | The Lovable project doesn't have the database setup. Paste `docs/lovable-app-prompt.md` into it. |
| `Supabase sync failed (HTTP 401/403)` in monitor | Wrong `SUPABASE_KEY` or token not registered. Re-check step 7. |
| App commands never arrive | In Lovable, ask it to show recent `light_commands` rows and whether inserts return an RLS error. The user must be signed in. |
| New Govee light doesn't appear | Restart the hub (unplug/replug). It reads the device list at startup. |
