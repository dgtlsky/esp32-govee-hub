-- Supabase "mailbox" between a web app and the ESP32 Govee hub.
--
--   App (signed-in users)  -> inserts rows into light_commands, reads/subscribes to lights
--   ESP32                  -> calls device_sync() every ~2s: reports light state, picks up commands, reports results
--
-- The ESP32 authenticates with its own device token (stored hashed in public.devices),
-- so it only needs the project's public anon/publishable key, never the service role key.

create extension if not exists pgcrypto with schema extensions;

-- Current state of each light, written by the ESP32.
create table public.lights (
  id text primary key,
  name text not null,
  sku text not null,
  connection text not null check (connection in ('lan', 'cloud')),
  state_known boolean not null default false,
  is_on boolean not null default false,
  brightness integer not null default 100 check (brightness between 0 and 100),
  color jsonb not null default '{"r":255,"g":255,"b":255}',
  color_temp_k integer, -- null while the light is in RGB color mode
  updated_at timestamptz not null default now()
);

-- Commands from the app. light_ids = null means "all lights". Leave power/brightness/color/color_temp_k
-- null to keep them unchanged. Send either color (RGB) or color_temp_k (Kelvin), not both.
create table public.light_commands (
  id bigint generated always as identity primary key,
  created_at timestamptz not null default now(),
  created_by uuid default auth.uid() references auth.users (id) on delete set null,
  light_ids text[],
  power boolean,
  brightness integer check (brightness between 1 and 100),
  color jsonb check (color is null or (color ? 'r' and color ? 'g' and color ? 'b')),
  color_temp_k integer check (color_temp_k between 1000 and 10000),
  status text not null default 'pending' check (status in ('pending', 'sent', 'done', 'failed', 'expired')),
  processed_at timestamptz,
  error text
);

create index light_commands_pending_idx on public.light_commands (id) where status = 'pending';

-- Hubs allowed to sync. Only the SHA-256 hash of each device token is stored.
create table public.devices (
  id uuid primary key default gen_random_uuid(),
  name text not null,
  token_hash text not null unique,
  last_seen_at timestamptz
);

alter table public.lights enable row level security;
alter table public.light_commands enable row level security;
alter table public.devices enable row level security;

create policy "Signed-in users can read lights"
  on public.lights for select to authenticated using (true);

create policy "Signed-in users can read commands"
  on public.light_commands for select to authenticated using (true);

create policy "Signed-in users can send commands"
  on public.light_commands for insert to authenticated
  with check (created_by = auth.uid() and status = 'pending');

-- public.devices has no policies on purpose: only device_sync() (security definer) can read it.

create or replace function public.hub_online()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (select 1 from public.devices where last_seen_at > now() - interval '30 seconds');
$$;

revoke all on function public.hub_online() from public;
grant execute on function public.hub_online() to authenticated;

create or replace function public.device_sync(
  p_token text,
  p_lights jsonb default null,
  p_results jsonb default null
)
returns jsonb
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_device uuid;
  v_commands jsonb;
begin
  select id into v_device
  from public.devices
  where token_hash = encode(extensions.digest(p_token, 'sha256'), 'hex');

  if v_device is null then
    raise exception 'invalid device token' using errcode = '28000';
  end if;

  update public.devices set last_seen_at = now() where id = v_device;

  if p_lights is not null then
    insert into public.lights (id, name, sku, connection, state_known, is_on, brightness, color, color_temp_k, updated_at)
    select
      l ->> 'id',
      l ->> 'name',
      l ->> 'sku',
      l ->> 'connection',
      coalesce((l ->> 'stateKnown')::boolean, false),
      coalesce((l ->> 'on')::boolean, false),
      coalesce((l ->> 'brightness')::integer, 100),
      coalesce(l -> 'color', '{"r":255,"g":255,"b":255}'),
      nullif((l ->> 'colorTempK')::integer, 0),
      now()
    from jsonb_array_elements(p_lights) as l
    on conflict (id) do update set
      name = excluded.name,
      sku = excluded.sku,
      connection = excluded.connection,
      state_known = excluded.state_known,
      is_on = excluded.is_on,
      brightness = excluded.brightness,
      color = excluded.color,
      color_temp_k = excluded.color_temp_k,
      updated_at = excluded.updated_at;
  end if;

  if p_results is not null then
    update public.light_commands as c
    set
      status = case when (r ->> 'ok')::boolean then 'done' else 'failed' end,
      error = r ->> 'error',
      processed_at = now()
    from jsonb_array_elements(p_results) as r
    where c.id = (r ->> 'id')::bigint and c.status = 'sent';
  end if;

  -- Don't replay commands that sat unclaimed while the hub was offline.
  update public.light_commands
  set status = 'expired', processed_at = now()
  where status = 'pending' and created_at < now() - interval '1 minute';

  with picked as (
    update public.light_commands
    set status = 'sent'
    where id in (
      select id from public.light_commands
      where status = 'pending'
      order by id
      limit 10
      for update skip locked
    )
    returning id, light_ids, power, brightness, color, color_temp_k
  )
  select coalesce(jsonb_agg(to_jsonb(picked) order by picked.id), '[]'::jsonb)
  into v_commands
  from picked;

  return jsonb_build_object('commands', v_commands);
end;
$$;

revoke all on function public.device_sync(text, jsonb, jsonb) from public;
grant execute on function public.device_sync(text, jsonb, jsonb) to anon, authenticated;

alter publication supabase_realtime add table public.lights, public.light_commands;
