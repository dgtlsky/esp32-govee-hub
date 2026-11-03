# Build your own Lovable app for the hub

Only needed if you're **not** remixing the template project. Create a new Lovable project with **Lovable Cloud**
enabled, then paste the prompt below. Replace the placeholder with the full contents of `supabase/setup.sql`.

```text
Build a mobile-first smart light controller with email sign-in (required for the whole app).

Run this SQL as a database migration exactly as written:
<paste the contents of supabase/setup.sql here>

Access:
- Right after sign-in, call the RPC claim_first_owner(). It returns true if this user is allowed (the very
  first user of a fresh app becomes the owner automatically). If it returns false, show only a friendly
  "You haven't been invited yet. Ask the owner to add your email." screen with a sign-out button.
- Add a Settings screen where allowed users can invite or remove people by email (insert/delete rows in
  allowed_emails, lowercase emails).

How the app works:
- Lights come from the `lights` table (id, name, sku, connection 'lan'|'cloud', state_known, is_on,
  brightness 0-100, color jsonb {r,g,b}, color_temp_k, updated_at). Subscribe to realtime changes on it.
  color_temp_k null means the light is in RGB color mode (show its color); a number means white mode at that
  Kelvin (show the temperature; color will be {0,0,0}).
- To control lights, INSERT into `light_commands` with any of: light_ids (text[]; null = all lights),
  power (boolean), brightness (1-100), color (jsonb {r,g,b}), color_temp_k (Kelvin, 2000-9000).
  Send either color or color_temp_k, never both. Leave other fields null to keep them unchanged.
  Never set status yourself. A hardware hub picks the row up within ~2 seconds and sets status to
  'done', 'failed' (with error), or 'expired'. Subscribe to realtime on light_commands to show this.
- Show a "Hub online/offline" indicator using the RPC hub_online() (poll every 10s).

"Set up your hub" page (show it automatically while the lights table is empty, and link to it from Settings):
  Step 1 - Get the hub software: "Plug your ESP32 into your computer. Download the code from
  https://github.com/dgtlsky/esp32-govee-hub, open the folder in Cursor or Claude Code, and tell the agent:
  Set up my ESP32 Govee hub."
  Step 2 - Give your agent this message: a read-only box with a Copy button containing exactly
  "Connect my ESP32 hub to my Lovable app. Backend URL: <this app's backend URL> Publishable key: <this app's
  publishable key>", filled in from the app's own backend client config (both are public by design).
  Step 3 - Paste your hub code: a text input + Register button that calls register_hub(code). Trim spaces,
  show the error message if it fails, "Hub registered!" if it works.
  Below the steps, poll hub_online() every 5s: "Waiting for your hub..." until online, then "Your hub is
  connected!" with a button to the lights.

UI: an "All lights" card (all on, all off, brightness slider, color swatches, warm-to-cool white slider), then
one card per light with an on/off switch, brightness slider, color swatches plus a custom color picker, and a
white slider (2000-9000 K) with presets Warm 2700K, Neutral 4000K, Daylight 6500K. Send slider values on
release, not while dragging. Update the UI optimistically, then reconcile with the lights table.
```

Then follow step 7 in `AGENTS.md` (or ask your coding agent to) to connect the hub.
