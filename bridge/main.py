"""
DCC Bridge — Lightweight API that receives data pushes and serves
them to the CrowPanel display.

Endpoints:
  GET  /api/health          — liveness check
  GET  /api/adapters        — adapter status overview
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

logger = logging.getLogger(__name__)

# Shared cache — used by both push endpoints and poll adapters
cache = TTLCache()
scheduler = AdapterScheduler(cache=cache)

# Default TTL for push-ingested data (10 minutes)
PUSH_TTL = 600


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
