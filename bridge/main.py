"""
DCC Bridge — Lightweight API that receives data pushes and serves
them to the CrowPanel display.

Stub endpoints:
  GET  /api/health          — liveness check
  POST /calendar/ms         — ingest Outlook calendar JSON
  GET  /calendar/ms         — retrieve latest calendar snapshot
  POST /calendar/google     — ingest Google calendar JSON
  GET  /calendar/google     — retrieve latest Google calendar snapshot
"""

from datetime import datetime
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse

app = FastAPI(title="DCC Bridge", version="0.1.0")

# In-memory store — replaced by Dolt persistence in dev phase
_store: dict[str, dict] = {}


@app.get("/api/health")
async def health():
    return {
        "status": "ok",
        "service": "dcc-bridge",
        "timestamp": datetime.utcnow().isoformat() + "Z",
        "store_keys": list(_store.keys()),
    }


@app.post("/calendar/ms")
async def ingest_ms_calendar(request: Request):
    data = await request.json()
    data["received_at"] = datetime.utcnow().isoformat() + "Z"
    _store["calendar_ms"] = data
    count = data.get("count", "?")
    return {"accepted": True, "event_count": count}


@app.get("/calendar/ms")
async def get_ms_calendar():
    data = _store.get("calendar_ms")
    if data is None:
        return JSONResponse(status_code=404, content={"error": "no data yet"})
    return data


@app.post("/calendar/google")
async def ingest_google_calendar(request: Request):
    data = await request.json()
    data["received_at"] = datetime.utcnow().isoformat() + "Z"
    _store["calendar_google"] = data
    count = data.get("count", "?")
    return {"accepted": True, "event_count": count}


@app.get("/calendar/google")
async def get_google_calendar():
    data = _store.get("calendar_google")
    if data is None:
        return JSONResponse(status_code=404, content={"error": "no data yet"})
    return data
