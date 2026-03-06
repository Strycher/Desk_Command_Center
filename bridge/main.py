"""
DCC Bridge — Lightweight API that receives data pushes and serves
them to the CrowPanel display.

Endpoints:
  GET  /api/health          — liveness check
  GET  /api/adapters        — adapter status overview
  GET  /api/config          — current config (secrets masked)
  PUT  /api/config          — partial config update
  GET  /api/dashboard       — merged data for display (single poll)
  POST /calendar/ms         — ingest Outlook calendar JSON (push)
  GET  /calendar/ms         — retrieve latest calendar snapshot
  POST /calendar/google     — ingest Google calendar JSON (push)
  GET  /calendar/google     — retrieve latest Google calendar snapshot
"""

import logging
from contextlib import asynccontextmanager
from datetime import datetime, timezone

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

from adapters import TTLCache, AdapterScheduler
from adapters.beads import BeadsAdapter
from adapters.github import GitHubAdapter
from adapters.home_assistant import HomeAssistantAdapter
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
scheduler.register(HomeAssistantAdapter(_cfg))

# Default TTL for push-ingested data (10 minutes)
PUSH_TTL = 600

# Known data sources — the dashboard merges all of these.
# Each entry: cache_key -> human-readable label
DASHBOARD_SOURCES = {
    "calendar_ms": "Outlook Calendar",
    "calendar_google": "Google Calendar",
    "weather": "Weather",
    "github": "GitHub",
    "beads": "Beads Tasks",
    "home_assistant": "Home Assistant",
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
