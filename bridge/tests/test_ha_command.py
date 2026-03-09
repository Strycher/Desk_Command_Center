"""Tests for POST /api/ha/command endpoint."""

from unittest.mock import AsyncMock, patch

import pytest
from httpx import ASGITransport, AsyncClient

from main import app


@pytest.fixture
def transport():
    return ASGITransport(app=app)


class TestHACommand:
    @pytest.mark.asyncio
    async def test_missing_entity_id(self, transport):
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "service": "turn_off",
            })
            assert resp.status_code == 400

    @pytest.mark.asyncio
    async def test_missing_service(self, transport):
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "entity_id": "light.office",
            })
            assert resp.status_code == 400

    @pytest.mark.asyncio
    async def test_unknown_domain_rejected(self, transport):
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "entity_id": "automation.something",
                "service": "trigger",
            })
            assert resp.status_code == 400
            body = resp.json()
            assert body["success"] is False

    @pytest.mark.asyncio
    async def test_disallowed_service_rejected(self, transport):
        async with AsyncClient(transport=transport, base_url="http://test") as client:
            resp = await client.post("/api/ha/command", json={
                "entity_id": "light.office",
                "service": "delete_entity",
            })
            assert resp.status_code == 400
            body = resp.json()
            assert body["success"] is False

    @pytest.mark.asyncio
    async def test_valid_command_calls_adapter(self, transport):
        """Mock the HA adapter to verify the endpoint routes correctly."""
        with patch("main.ha_adapter") as mock_adapter:
            mock_adapter.call_service = AsyncMock(return_value=True)
            mock_adapter.get_entity_state = AsyncMock(return_value={
                "entity_id": "light.office",
                "state": "off",
                "friendly_name": "Office Light",
                "domain": "light",
            })

            async with AsyncClient(transport=transport, base_url="http://test") as client:
                resp = await client.post("/api/ha/command", json={
                    "entity_id": "light.office",
                    "service": "turn_off",
                })
                assert resp.status_code == 200
                body = resp.json()
                assert body["success"] is True
                assert body["entity"]["state"] == "off"
