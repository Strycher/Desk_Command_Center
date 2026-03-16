"""Monday.com tasks adapter — fetches board items via GraphQL API."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

MONDAY_API_URL = "https://api.monday.com/v2"

# GraphQL query: fetch items from specified boards with key columns
ITEMS_QUERY = """
query ($board_ids: [ID!]!) {
  boards(ids: $board_ids) {
    id
    name
    items_page(limit: 50) {
      items {
        id
        name
        state
        column_values {
          id
          text
          type
        }
      }
    }
  }
}
"""


class MondayAdapter(BaseAdapter):
    """Polls Monday.com boards for task items via GraphQL API.

    Config from BridgeConfig:
      monday.api_token: Monday.com API token
      monday.board_ids: list of board IDs to fetch
      monday.poll_interval: seconds between polls (default 300)
    """

    def __init__(self, source_config: dict[str, Any]) -> None:
        poll_interval = source_config.get("poll_interval", 300)

        super().__init__(
            name="monday",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=2,
                backoff_base=5.0,
                backoff_max=120.0,
            ),
        )
        self.api_token: str = source_config.get("api_token", "")
        self.board_ids: list[str] = [
            str(bid) for bid in source_config.get("board_ids", [])
        ]

    async def poll(self) -> dict[str, Any]:
        if not self.api_token:
            raise ValueError("Monday.com API token not configured")
        if not self.board_ids:
            raise ValueError("No Monday.com board IDs configured")

        headers = {
            "Authorization": self.api_token,
            "Content-Type": "application/json",
            "API-Version": "2024-10",
        }

        async with httpx.AsyncClient(timeout=15.0) as client:
            resp = await client.post(
                MONDAY_API_URL,
                headers=headers,
                json={
                    "query": ITEMS_QUERY,
                    "variables": {"board_ids": self.board_ids},
                },
            )
            resp.raise_for_status()
            data = resp.json()

        if "errors" in data:
            raise RuntimeError(f"Monday.com API error: {data['errors']}")

        return data.get("data", {})

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        boards = []
        total_items = 0

        for board in raw.get("boards", []):
            board_name = board.get("name", "Unknown Board")
            board_id = board.get("id", "")
            items = []

            for item in board.get("items_page", {}).get("items", []):
                if item.get("state") == "deleted":
                    continue

                columns = {
                    cv["id"]: cv["text"]
                    for cv in item.get("column_values", [])
                    if cv.get("text")
                }

                # Extract common fields from column values
                status = columns.get("status", columns.get("status_1", ""))
                due_date = columns.get("date", columns.get("date4", ""))
                priority = columns.get("priority", "")

                items.append({
                    "id": item["id"],
                    "title": item.get("name", ""),
                    "status": status,
                    "due_date": due_date,
                    "priority": priority,
                    "board": board_name,
                })
                total_items += 1

            boards.append({
                "id": board_id,
                "name": board_name,
                "item_count": len(items),
                "items": items,
            })

        return {
            "boards": boards,
            "total_items": total_items,
            "board_count": len(boards),
        }
