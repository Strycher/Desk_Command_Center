"""Tests for Monday.com tasks adapter."""

from __future__ import annotations

from unittest.mock import AsyncMock, MagicMock, patch

import pytest

from adapters.monday import MondayAdapter


@pytest.fixture
def adapter():
    return MondayAdapter({
        "api_token": "test-token-123",
        "board_ids": ["12345", "67890"],
        "poll_interval": 120,
    })


@pytest.fixture
def adapter_no_token():
    return MondayAdapter({
        "board_ids": ["12345"],
    })


@pytest.fixture
def adapter_no_boards():
    return MondayAdapter({
        "api_token": "test-token-123",
    })


@pytest.fixture
def sample_api_response():
    return {
        "boards": [
            {
                "id": "12345",
                "name": "Sprint Board",
                "items_page": {
                    "items": [
                        {
                            "id": "001",
                            "name": "Fix login bug",
                            "state": "active",
                            "column_values": [
                                {"id": "status", "text": "Working on it", "type": "status"},
                                {"id": "date", "text": "2026-03-20", "type": "date"},
                                {"id": "priority", "text": "High", "type": "color"},
                            ],
                        },
                        {
                            "id": "002",
                            "name": "Add export feature",
                            "state": "active",
                            "column_values": [
                                {"id": "status", "text": "Done", "type": "status"},
                                {"id": "date", "text": "", "type": "date"},
                                {"id": "priority", "text": "Medium", "type": "color"},
                            ],
                        },
                        {
                            "id": "003",
                            "name": "Deleted task",
                            "state": "deleted",
                            "column_values": [],
                        },
                    ],
                },
            },
            {
                "id": "67890",
                "name": "Backlog",
                "items_page": {
                    "items": [
                        {
                            "id": "004",
                            "name": "Research caching",
                            "state": "active",
                            "column_values": [
                                {"id": "status", "text": "", "type": "status"},
                                {"id": "priority", "text": "Low", "type": "color"},
                            ],
                        },
                    ],
                },
            },
        ],
    }


class TestMondayAdapterInit:
    def test_name(self, adapter):
        assert adapter.name == "monday"

    def test_config(self, adapter):
        assert adapter.config.poll_interval == 120
        assert adapter.api_token == "test-token-123"
        assert adapter.board_ids == ["12345", "67890"]

    def test_defaults(self):
        a = MondayAdapter({})
        assert a.config.poll_interval == 300
        assert a.api_token == ""
        assert a.board_ids == []

    def test_board_ids_coerced_to_strings(self):
        a = MondayAdapter({"api_token": "x", "board_ids": [123, 456]})
        assert a.board_ids == ["123", "456"]


class TestParse:
    def test_parse_boards(self, adapter, sample_api_response):
        result = adapter.parse(sample_api_response)
        assert result["board_count"] == 2
        assert result["total_items"] == 3  # deleted item excluded

    def test_parse_items(self, adapter, sample_api_response):
        result = adapter.parse(sample_api_response)
        sprint_board = result["boards"][0]
        assert sprint_board["name"] == "Sprint Board"
        assert sprint_board["item_count"] == 2  # deleted excluded

        item = sprint_board["items"][0]
        assert item["title"] == "Fix login bug"
        assert item["status"] == "Working on it"
        assert item["due_date"] == "2026-03-20"
        assert item["priority"] == "High"
        assert item["board"] == "Sprint Board"

    def test_parse_empty_columns(self, adapter, sample_api_response):
        result = adapter.parse(sample_api_response)
        backlog_item = result["boards"][1]["items"][0]
        assert backlog_item["status"] == ""  # empty text filtered
        assert backlog_item["due_date"] == ""

    def test_parse_deleted_excluded(self, adapter, sample_api_response):
        result = adapter.parse(sample_api_response)
        sprint_items = result["boards"][0]["items"]
        titles = [i["title"] for i in sprint_items]
        assert "Deleted task" not in titles

    def test_parse_empty(self, adapter):
        result = adapter.parse({"boards": []})
        assert result["board_count"] == 0
        assert result["total_items"] == 0
        assert result["boards"] == []

    def test_parse_no_items_page(self, adapter):
        raw = {"boards": [{"id": "1", "name": "Empty", "items_page": {"items": []}}]}
        result = adapter.parse(raw)
        assert result["boards"][0]["item_count"] == 0


class TestPoll:
    @pytest.mark.asyncio
    async def test_poll_no_token_raises(self, adapter_no_token):
        with pytest.raises(ValueError, match="API token not configured"):
            await adapter_no_token.poll()

    @pytest.mark.asyncio
    async def test_poll_no_boards_raises(self, adapter_no_boards):
        with pytest.raises(ValueError, match="No Monday.com board IDs"):
            await adapter_no_boards.poll()

    @pytest.mark.asyncio
    async def test_poll_api_error_raises(self, adapter):
        mock_response = MagicMock()
        mock_response.status_code = 200
        mock_response.raise_for_status = lambda: None
        mock_response.json.return_value = {
            "errors": [{"message": "Authentication failed"}],
        }

        async def mock_post(*args, **kwargs):
            return mock_response

        with patch("adapters.monday.httpx.AsyncClient") as mock_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__ = AsyncMock(return_value=False)
            mock_client.post = mock_post
            mock_cls.return_value = mock_client

            with pytest.raises(RuntimeError, match="Monday.com API error"):
                await adapter.poll()

    @pytest.mark.asyncio
    async def test_poll_success(self, adapter, sample_api_response):
        mock_response = MagicMock()
        mock_response.status_code = 200
        mock_response.raise_for_status = lambda: None
        mock_response.json.return_value = {"data": sample_api_response}

        async def mock_post(*args, **kwargs):
            return mock_response

        with patch("adapters.monday.httpx.AsyncClient") as mock_cls:
            mock_client = AsyncMock()
            mock_client.__aenter__ = AsyncMock(return_value=mock_client)
            mock_client.__aexit__ = AsyncMock(return_value=False)
            mock_client.post = mock_post
            mock_cls.return_value = mock_client

            result = await adapter.poll()

        assert "boards" in result
        assert len(result["boards"]) == 2
