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

import logging
import time
from contextlib import asynccontextmanager
from datetime import datetime, timezone

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from adapters import TTLCache, AdapterScheduler
from adapters.beads import BeadsAdapter
from adapters.github import GitHubAdapter
from adapters.google_calendar import GoogleCalendarAdapter
from adapters.home_assistant import HomeAssistantAdapter
from adapters.unfocused_tasks import UnfocusedTasksAdapter
from adapters.weather import WeatherAdapter
from config import BridgeConfig

logger = logging.getLogger(__name__)

# Shared state
cache = TTLCache()
scheduler = AdapterScheduler(cache=cache)
config = BridgeConfig()

# Register poll-based adapters
_cfg = config.get_all(mask_secrets=False)
scheduler.register(WeatherAdapter(_cfg))
scheduler.register(GitHubAdapter(_cfg))
scheduler.register(BeadsAdapter(_cfg))
ha_adapter = HomeAssistantAdapter(_cfg)
scheduler.register(ha_adapter)
scheduler.register(GoogleCalendarAdapter(_cfg))
scheduler.register(UnfocusedTasksAdapter(_cfg))

# Default TTL for push-ingested data (10 minutes)
PUSH_TTL = 600

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

# Known data sources — the dashboard merges all of these.
# Each entry: cache_key -> human-readable label
DASHBOARD_SOURCES = {
    "calendar_ms": "Outlook Calendar",
    "calendar_google": "Google Calendar",
    "weather": "Weather",
    "github": "GitHub",
    "beads": "Beads Tasks",
    "home_assistant": "Home Assistant",
    "unfocused_tasks": "Unfocused Tasks",
    "claude": "Claude Agents",
}


@asynccontextmanager
async def lifespan(app: FastAPI):
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


@app.get("/api/dashboard")
async def dashboard():
    """Merged snapshot of all data sources for the display.

    Each source entry contains status, last_updated, and data.
    Missing or expired sources return null data with error metadata.
    """
    sources = {}
    for key, label in DASHBOARD_SOURCES.items():
        entry = cache.get_entry(key)
        if entry is None:
            sources[key] = {
                "label": label,
                "status": "missing",
                "last_updated": None,
                "data": None,
            }
        elif entry.is_expired:
            sources[key] = {
                "label": label,
                "status": "stale",
                "last_updated": entry.value.get("received_at"),
                "data": entry.value,
            }
        else:
            sources[key] = {
                "label": label,
                "status": "ok",
                "last_updated": entry.value.get("received_at"),
                "data": entry.value,
            }

    # Include any poll-based adapter statuses
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

    # Re-fetch entity state
    try:
        entity = await ha_adapter.get_entity_state(entity_id)
    except Exception as exc:
        logger.warning("HA state refetch failed: %s", exc)
        return {"success": True, "entity": None, "warning": "State refetch failed"}

    return {"success": True, "entity": entity}


# --- Claude agent heartbeat ------------------------------------------------

# In-memory registry: {agent_name: {task, program, model, last_seen}}
_claude_sessions: dict[str, dict] = {}
CLAUDE_SESSION_TTL = 300  # 5 minutes — session is stale if no heartbeat


@app.post("/api/claude/heartbeat")
async def claude_heartbeat(request: Request):
    """Register or refresh a Claude agent session.

    Body: {agent: "name", task: "description", program: "claude-code", model: "opus-4.6"}
    """
    body = await request.json()
    agent = body.get("agent", "unknown")
    _claude_sessions[agent] = {
        "task": body.get("task", ""),
        "program": body.get("program", "claude-code"),
        "model": body.get("model", ""),
        "last_seen": time.monotonic(),
    }
    _refresh_claude_cache()
    return {"accepted": True, "agent": agent}


@app.delete("/api/claude/heartbeat")
async def claude_goodbye(request: Request):
    """Remove a Claude agent session (clean shutdown)."""
    body = await request.json()
    agent = body.get("agent", "")
    _claude_sessions.pop(agent, None)
    _refresh_claude_cache()
    return {"removed": True, "agent": agent}


def _refresh_claude_cache():
    """Rebuild claude cache entry from active sessions."""
    now = time.monotonic()
    # Prune stale sessions
    stale = [k for k, v in _claude_sessions.items()
             if now - v["last_seen"] > CLAUDE_SESSION_TTL]
    for k in stale:
        del _claude_sessions[k]

    active = [
        {**v, "agent": k}
        for k, v in _claude_sessions.items()
    ]

    if active:
        # Pick most recently seen for the summary fields
        latest = max(active, key=lambda s: s["last_seen"])
        status = "active"
        current_task = latest["task"]
    else:
        status = "offline"
        current_task = ""

    cache.set("claude", {
        "status": status,
        "current_task": current_task,
        "active_count": len(active),
        "sessions": [
            {"agent": s["agent"], "task": s["task"], "program": s["program"]}
            for s in active
        ],
    }, CLAUDE_SESSION_TTL * 2)
