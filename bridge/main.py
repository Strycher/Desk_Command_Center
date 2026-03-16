"""
DCC Bridge — Lightweight API that receives data pushes and serves
them to the CrowPanel display.

Endpoints:
  GET  /api/health          — liveness check
  GET  /api/adapters        — adapter status overview
  GET  /api/config          — current config (secrets masked)
  PUT  /api/config          — partial config update
  GET  /api/dashboard       — merged data for display (single poll)
  POST /api/ha/command      — send a whitelisted command to Home Assistant
  POST /calendar/ms         — ingest Outlook calendar JSON (push)
  GET  /calendar/ms         — retrieve latest calendar snapshot
  POST /calendar/google     — ingest Google calendar JSON (push)
  GET  /calendar/google     — retrieve latest Google calendar snapshot
"""

import asyncio
import logging
import time
from contextlib import asynccontextmanager
from datetime import datetime, timezone

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from adapters import TTLCache, AdapterScheduler
from adapters.base import BaseAdapter
from adapters.beads import BeadsAdapter
from adapters.claude_status import ClaudeStatusAdapter
from adapters.github import GitHubAdapter
from adapters.monday import MondayAdapter
from adapters.google_calendar import GoogleCalendarAdapter
from adapters.home_assistant import HomeAssistantAdapter
from adapters.unfocused_tasks import UnfocusedTasksAdapter
from adapters.weather import WeatherAdapter
from config import BridgeConfig
from push_store import PushStore

logger = logging.getLogger(__name__)

# --- Adapter factory --------------------------------------------------------

# Maps source config key to adapter class
ADAPTER_CLASSES: dict[str, type[BaseAdapter]] = {
    "weather": WeatherAdapter,
    "github": GitHubAdapter,
    "google_calendar": GoogleCalendarAdapter,
    "beads": BeadsAdapter,
    "unfocused_tasks": UnfocusedTasksAdapter,
    "home_assistant": HomeAssistantAdapter,
    "claude_status": ClaudeStatusAdapter,
    "monday": MondayAdapter,
}


def create_adapter(
    source_name: str,
    source_config: dict,
    user_id: str | None = None,
) -> BaseAdapter:
    """Create an adapter instance with appropriate cache_key.

    Per-user adapters get cache_key "source:user_id".
    Shared adapters (user_id=None) keep the default bare name.
    """
    cls = ADAPTER_CLASSES.get(source_name)
    if cls is None:
        raise ValueError(f"Unknown adapter source: {source_name!r}")
    adapter = cls(source_config)  # type: ignore[arg-type]  # subclasses accept dict
    if user_id is not None:
        adapter.cache_key = f"{adapter.name}:{user_id}"
    return adapter


# Shared state
cache = TTLCache()
scheduler = AdapterScheduler(cache=cache)
config = BridgeConfig()
push_store: PushStore | None = None  # initialized in lifespan (avoids mkdir at import)

# Register per-user adapters from config
for _user_id in config.list_users():
    for _source_name, _source_cfg in config.get_user_sources(_user_id).items():
        if _source_name not in ADAPTER_CLASSES:
            continue  # skip non-adapter sources (claude, devops)
        _adapter = create_adapter(_source_name, _source_cfg, _user_id)
        scheduler.register(_adapter)
        logger.info("Registered %s for user %s", _source_name, _user_id)

# Register shared adapters (one instance, bare cache key)
ha_adapter: HomeAssistantAdapter | None = None
for _source_name, _source_cfg in config.get_shared_sources().items():
    if _source_name not in ADAPTER_CLASSES:
        continue
    _adapter = create_adapter(_source_name, _source_cfg)
    scheduler.register(_adapter)
    if isinstance(_adapter, HomeAssistantAdapter):
        ha_adapter = _adapter
    logger.info("Registered shared %s", _source_name)

# Default TTL for push-ingested data.
# Calendar data is stable — a missed push shouldn't blank the display.
# After TTL expires, data shows as "stale" (not "missing") — still rendered.
PUSH_TTL = 604800  # 7 days — calendar data is stable; survives a full weekend

# Allowed HA service calls — security boundary for device control
ALLOWED_SERVICES: dict[str, set[str]] = {
    "light": {"turn_on", "turn_off", "toggle"},
    "switch": {"turn_on", "turn_off", "toggle"},
    "fan": {"turn_on", "turn_off", "toggle"},
    "lock": {"lock", "unlock"},
    "cover": {"open_cover", "close_cover", "stop_cover"},
    "climate": {"set_temperature", "set_hvac_mode", "set_preset_mode"},
    "media_player": {"media_play_pause", "volume_up", "volume_down", "volume_set"},
}

# Human-readable labels for source types
SOURCE_LABELS = {
    "calendar_ms": "Outlook Calendar",
    "calendar_google": "Google Calendar",
    "weather": "Weather",
    "github": "GitHub",
    "beads": "Beads Tasks",
    "home_assistant": "Home Assistant",
    "unfocused_tasks": "Unfocused Tasks",
    "claude": "Claude Agents",
    "claude_status": "Claude Agents",
    "monday": "Monday.com",
}

# Push-based sources (not adapter-driven, always included)
PUSH_SOURCES = {"calendar_ms", "calendar_google", "claude"}


@asynccontextmanager
async def lifespan(app: FastAPI):
    global push_store
    push_store = PushStore()  # deferred — mkdir only at startup, not import

    # Restore push data from disk so it survives restarts
    for key, data in push_store.load_all().items():
        cache.set(key, data, PUSH_TTL)
        logger.info("Restored push source '%s' from disk", key)

    await scheduler.start()
    logger.info("Bridge started — scheduler running")
    yield
    await scheduler.stop()
    logger.info("Bridge stopped")


app = FastAPI(title="DCC Bridge", version="0.2.0", lifespan=lifespan)


@app.get("/api/health")
async def health():
    return {
        "status": "ok",
        "service": "dcc-bridge",
        "version": "0.2.0",
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "cache_keys": cache.keys(),
    }


@app.get("/api/adapters")
async def list_adapters():
    return {"adapters": scheduler.list_adapters()}


@app.get("/api/config")
async def get_config():
    return config.get_all(mask_secrets=True)


@app.put("/api/config")
async def update_config(request: Request):
    updates = await request.json()
    result = config.update(updates)
    if result["errors"]:
        return JSONResponse(
            status_code=400,
            content=result,
        )
    return result


def _label_for_key(cache_key: str) -> str:
    """Derive human-readable label from a cache key.

    "weather:strycher" -> "Weather", "home_assistant" -> "Home Assistant"
    """
    base = cache_key.split(":")[0]
    return SOURCE_LABELS.get(base, base)


def _build_source_entry(cache_key: str) -> dict:
    """Build a dashboard source entry from a cache key."""
    entry = cache.get_entry(cache_key)
    label = _label_for_key(cache_key)
    if entry is None:
        return {"label": label, "status": "missing", "last_updated": None, "data": None}
    elif entry.is_expired:
        return {"label": label, "status": "stale",
                "last_updated": entry.value.get("received_at"), "data": entry.value}
    else:
        return {"label": label, "status": "ok",
                "last_updated": entry.value.get("received_at"), "data": entry.value}


@app.get("/api/dashboard")
async def dashboard(request: Request):
    """Merged snapshot of data sources for the display.

    Device identification via headers:
    - No X-Device-ID: legacy mode — all sources returned
    - Unknown device: shared sources only, device recorded
    - Known device, wrong key: 403 Forbidden
    - Known device, valid key: per-user + shared sources
    """
    device_id = request.headers.get("X-Device-ID")
    device_key = request.headers.get("X-Device-Key")

    if device_id is None:
        # Legacy mode: return all registered sources + push sources
        cache_keys = {}
        for adapter_info in scheduler.list_adapters():
            key = adapter_info["name"]
            cache_keys[key] = key
        for key in PUSH_SOURCES:
            cache_keys[key] = key
    else:
        device = config.get_device(device_id)
        registered_key = device.get("key", "") if device else ""
        if device is not None and registered_key and registered_key != (device_key or ""):
            return JSONResponse(status_code=403, content={"error": "Invalid device key"})

        if device is None:
            # Unknown device — shared sources only, record it
            config.record_unregistered_device(
                device_id, request.client.host if request.client else "unknown",
            )
            cache_keys = config.get_cache_keys_shared_only()
        else:
            user_id = device["user"]
            cache_keys = config.get_cache_keys_for_user(user_id)

        # Always include push sources
        for key in PUSH_SOURCES:
            cache_keys[key] = key

    sources = {}
    for source_name, cache_key in cache_keys.items():
        sources[source_name] = _build_source_entry(cache_key)

    adapter_statuses = {a["name"]: a["status"] for a in scheduler.list_adapters()}

    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "sources": sources,
        "adapter_statuses": adapter_statuses,
    }


# --- Push endpoints: Calendar -------------------------------------------

@app.post("/calendar/ms")
async def ingest_ms_calendar(request: Request):
    data = await request.json()
    data["received_at"] = datetime.now(timezone.utc).isoformat()
    cache.set("calendar_ms", data, PUSH_TTL)
    if push_store:
        push_store.save("calendar_ms", data)
    count = data.get("count", "?")
    return {"accepted": True, "event_count": count}


@app.get("/calendar/ms")
async def get_ms_calendar():
    data = cache.get("calendar_ms")
    if data is None:
        return JSONResponse(status_code=404, content={"error": "no data yet"})
    return data


@app.post("/calendar/google")
async def ingest_google_calendar(request: Request):
    data = await request.json()
    data["received_at"] = datetime.now(timezone.utc).isoformat()
    cache.set("calendar_google", data, PUSH_TTL)
    if push_store:
        push_store.save("calendar_google", data)
    count = data.get("count", "?")
    return {"accepted": True, "event_count": count}


@app.get("/calendar/google")
async def get_google_calendar():
    data = cache.get("calendar_google")
    if data is None:
        return JSONResponse(status_code=404, content={"error": "no data yet"})
    return data


# --- HA device control -------------------------------------------------------

@app.post("/api/ha/command")
async def ha_command(request: Request):
    if ha_adapter is None:
        return JSONResponse(status_code=503, content={
            "success": False,
            "error": "Home Assistant adapter not configured",
        })

    body = await request.json()
    entity_id = body.get("entity_id")
    service = body.get("service")
    data = body.get("data", {})

    # Validate required fields
    if not entity_id or not service:
        return JSONResponse(status_code=400, content={
            "success": False,
            "error": "entity_id and service are required",
        })

    # Extract domain and validate against whitelist
    domain = entity_id.split(".")[0]
    allowed = ALLOWED_SERVICES.get(domain)
    if allowed is None:
        return JSONResponse(status_code=400, content={
            "success": False,
            "error": f"Domain '{domain}' is not controllable",
        })
    if service not in allowed:
        return JSONResponse(status_code=400, content={
            "success": False,
            "error": f"Service '{service}' not allowed for domain '{domain}'",
        })

    # Call HA service
    try:
        await ha_adapter.call_service(entity_id, service, data or None)
    except Exception as exc:
        logger.error("HA command failed: %s", exc)
        return JSONResponse(status_code=502, content={
            "success": False,
            "error": f"Service call failed: {exc}",
        })

    # Re-fetch entity state (climate needs retries — Z-Wave/Zigbee propagation)
    entity = None
    try:
        if domain == "climate":
            await asyncio.sleep(1.0)
            for attempt in range(3):
                entity = await ha_adapter.get_entity_state(entity_id)
                # Check if state reflects the command
                expected_mode = (data or {}).get("hvac_mode")
                expected_temp = (data or {}).get("temperature")
                current_state = entity.get("state")
                current_temp = entity.get("target_temp")
                if expected_mode and str(current_state) == str(expected_mode):
                    break
                if expected_temp and str(current_temp) == str(expected_temp):
                    break
                if not expected_mode and not expected_temp:
                    break  # preset_mode or other — no easy check
                await asyncio.sleep(1.0)
        else:
            entity = await ha_adapter.get_entity_state(entity_id)
    except Exception as exc:
        logger.warning("HA state refetch failed: %s", exc)
        return {"success": True, "entity": None, "warning": "State refetch failed"}

    # Refresh scheduler cache so the next /api/dashboard poll sees fresh data
    asyncio.ensure_future(scheduler.poll_once(ha_adapter))

    return {"success": True, "entity": entity}


# --- Claude agent heartbeat ------------------------------------------------

# Per-user session registries: {user_id: {agent_name: {task, program, model, last_seen}}}
# "claude" (bare) is the legacy/unscoped key
_claude_sessions: dict[str, dict[str, dict]] = {}
CLAUDE_SESSION_TTL = 300  # 5 minutes — session is stale if no heartbeat


def _resolve_claude_user(request: Request) -> str:
    """Resolve device header to user_id, or return bare "claude" for legacy."""
    device_id = request.headers.get("X-Device-ID")
    if device_id:
        user_id = config.resolve_device_user(device_id)
        if user_id:
            return user_id
    return "claude"  # legacy/unscoped


@app.post("/api/claude/heartbeat")
async def claude_heartbeat(request: Request):
    """Register or refresh a Claude agent session.

    Body: {agent: "name", task: "description", program: "claude-code", model: "opus-4.6"}
    Header: X-Device-ID (optional) — scopes session to a user
    """
    body = await request.json()
    agent = body.get("agent", "unknown")
    user_key = _resolve_claude_user(request)

    sessions = _claude_sessions.setdefault(user_key, {})
    sessions[agent] = {
        "task": body.get("task", ""),
        "program": body.get("program", "claude-code"),
        "model": body.get("model", ""),
        "last_seen": time.monotonic(),
    }
    _refresh_claude_cache(user_key)
    return {"accepted": True, "agent": agent, "user": user_key}


@app.delete("/api/claude/heartbeat")
async def claude_goodbye(request: Request):
    """Remove a Claude agent session (clean shutdown)."""
    body = await request.json()
    agent = body.get("agent", "")
    user_key = _resolve_claude_user(request)

    sessions = _claude_sessions.get(user_key, {})
    sessions.pop(agent, None)
    _refresh_claude_cache(user_key)
    return {"removed": True, "agent": agent, "user": user_key}


def _refresh_claude_cache(user_key: str) -> None:
    """Rebuild claude cache entry for a specific user scope."""
    sessions = _claude_sessions.get(user_key, {})
    now = time.monotonic()

    # Prune stale sessions
    stale = [k for k, v in sessions.items()
             if now - v["last_seen"] > CLAUDE_SESSION_TTL]
    for k in stale:
        del sessions[k]

    active = [{**v, "agent": k} for k, v in sessions.items()]

    if active:
        latest = max(active, key=lambda s: s["last_seen"])
        status = "active"
        current_task = latest["task"]
    else:
        status = "offline"
        current_task = ""

    # Cache key: "claude:strycher" for scoped, "claude" for legacy
    cache_key = f"claude:{user_key}" if user_key != "claude" else "claude"
    cache.set(cache_key, {
        "status": status,
        "current_task": current_task,
        "active_count": len(active),
        "sessions": [
            {"agent": s["agent"], "task": s["task"], "program": s["program"]}
            for s in active
        ],
    }, CLAUDE_SESSION_TTL * 2)
