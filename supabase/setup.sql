-- Supabase "mailbox" between a web app and the ESP32 Govee hub.
--
--   App (allowed users)    -> inserts rows into light_commands, reads/subscribes to lights
--                             (only emails in allowed_emails; the first sign-up claims ownership)
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

-- Who may use the app. Anyone can sign up to a Lovable app, so access is limited to this list.
-- The first person to call claim_first_owner() (the app does this after sign-in) is added automatically;
-- they can then invite others by inserting their emails.
create table public.allowed_emails (
  email text primary key check (email = lower(email)),
  added_at timestamptz not null default now()
);

create or replace function public.is_allowed_user()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select exists (
    select 1 from public.allowed_emails where email = lower(coalesce(auth.jwt() ->> 'email', ''))
  );
$$;

create or replace function public.claim_first_owner()
returns boolean
language plpgsql
security definer
set search_path = ''
as $$
declare
  v_email text := lower(coalesce(auth.jwt() ->> 'email', ''));
begin
  if auth.uid() is null or v_email = '' then
    return false;
  end if;
  lock table public.allowed_emails in exclusive mode;
  if not exists (select 1 from public.allowed_emails) then
    insert into public.allowed_emails (email) values (v_email);
  end if;
  return exists (select 1 from public.allowed_emails where email = v_email);
end;
$$;

alter table public.lights enable row level security;
alter table public.light_commands enable row level security;
alter table public.devices enable row level security;
alter table public.allowed_emails enable row level security;

create policy "Allowed users can read lights"
  on public.lights for select to authenticated using (public.is_allowed_user());

create policy "Allowed users can read commands"
  on public.light_commands for select to authenticated using (public.is_allowed_user());

create policy "Allowed users can send commands"
  on public.light_commands for insert to authenticated
  with check (public.is_allowed_user() and created_by = auth.uid() and status = 'pending');

create policy "Allowed users can see the allow list"
  on public.allowed_emails for select to authenticated using (public.is_allowed_user());

create policy "Allowed users can invite others"
  on public.allowed_emails for insert to authenticated with check (public.is_allowed_user());

create policy "Allowed users can remove others"
  on public.allowed_emails for delete to authenticated
  using (public.is_allowed_user() and email <> lower(coalesce(auth.jwt() ->> 'email', '')));

-- public.devices has no policies on purpose: only the security definer functions below can touch it.

-- Called from the app's "Set up your hub" page with the hub code printed by `scripts/hub.py token`.
create or replace function public.register_hub(p_token_hash text, p_name text default 'ESP32 hub')
returns void
language plpgsql
security definer
set search_path = ''
as $$
begin
  if not public.is_allowed_user() then
    raise exception 'Please sign in with an allowed account first' using errcode = '42501';
  end if;
  if p_token_hash !~ '^[0-9a-f]{64}$' then
    raise exception 'That hub code doesn''t look right. It should be 64 letters and numbers.' using errcode = '22023';
  end if;
  insert into public.devices (name, token_hash)
  values (coalesce(nullif(trim(p_name), ''), 'ESP32 hub'), p_token_hash)
  on conflict (token_hash) do nothing;
end;
$$;

revoke all on function public.is_allowed_user() from public;
revoke all on function public.claim_first_owner() from public;
revoke all on function public.register_hub(text, text) from public;
grant execute on function public.is_allowed_user() to authenticated;
grant execute on function public.claim_first_owner() to authenticated;
grant execute on function public.register_hub(text, text) to authenticated;

create or replace function public.hub_online()
returns boolean
language sql
stable
security definer
set search_path = ''
as $$
  select public.is_allowed_user()
    and exists (select 1 from public.devices where last_seen_at > now() - interval '30 seconds');
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
