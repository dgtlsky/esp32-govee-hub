#pragma once

// Syncs with a Supabase "mailbox" (see supabase/setup.sql): reports light state and runs commands
// queued by a remote web app. Disabled unless SUPABASE_URL, SUPABASE_KEY and DEVICE_TOKEN are set.
namespace cloud_sync {

void begin();
void loop();

}  // namespace cloud_sync
