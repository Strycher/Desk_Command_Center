"""Overwatch adapter — polls compliance audit reports from Overwatch API."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

DEFAULT_API_URL = "https://getunfocused.app/overwatch"


class OverwatchAdapter(BaseAdapter):
    """Polls Overwatch compliance report endpoint.

    Config from BridgeConfig:
      overwatch.api_url: Overwatch API base URL
      overwatch.poll_interval: seconds between polls (default 300)

    Returns structured compliance findings with pass/warn/fail counts
    and per-project breakdowns.
    """

    def __init__(self, source_config: dict[str, Any]) -> None:
        poll_interval = source_config.get("poll_interval", 300)

        super().__init__(
            name="overwatch",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 3,
                max_retries=2,
                backoff_base=10.0,
                backoff_max=120.0,
            ),
        )
        self.api_url = source_config.get("api_url", DEFAULT_API_URL).rstrip("/")

    async def poll(self) -> dict[str, Any]:
        async with httpx.AsyncClient(timeout=15.0) as client:
            health = await client.get(f"{self.api_url}/health")
            health.raise_for_status()

            report = await client.get(f"{self.api_url}/report")
            report.raise_for_status()
            return report.json()

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        """Transform Overwatch report JSON into display-ready structure.

        Output:
          {
            "timestamp": "2026-04-02 05:07:00Z",
            "counts": {"pass": 19, "warn": 21, "fail": 10},
            "findings": [...],           # all findings
            "failures": [...],           # severity=fail only
            "warnings": [...],           # severity=warn only
            "by_project": {
              "Unfocused": {"pass": N, "warn": N, "fail": N, "findings": [...]},
              ...
            }
          }
        """
        findings = raw.get("findings", [])
        counts = raw.get("counts", {"pass": 0, "warn": 0, "fail": 0})

        failures = [f for f in findings if f.get("severity") == "fail"]
        warnings = [f for f in findings if f.get("severity") == "warn"]

        # Build per-project breakdown
        by_project: dict[str, dict[str, Any]] = {}
        for finding in findings:
            # Extract project name from summary (e.g., "Rossum: missing hooks...")
            summary = finding.get("summary", "")
            project = _extract_project(summary)
            if project not in by_project:
                by_project[project] = {
                    "pass": 0,
                    "warn": 0,
                    "fail": 0,
                    "findings": [],
                }
            severity = finding.get("severity", "pass")
            if severity in by_project[project]:
                by_project[project][severity] += 1
            by_project[project]["findings"].append(finding)

        return {
            "timestamp": raw.get("timestamp", ""),
            "counts": counts,
            "findings": findings,
            "failures": failures,
            "warnings": warnings,
            "by_project": by_project,
        }


def _extract_project(summary: str) -> str:
    """Best-effort project name extraction from finding summary.

    Patterns:
      "Rossum: missing hooks..."        → "Rossum"
      "Rossum PR #54 stale..."          → "Rossum"
      "Rossum-4rk claimed 192h ago..."  → "Rossum"
      "Citadel API: 200 (9ms)"         → "Infrastructure"
      "Agent Mail: alive"               → "Infrastructure"
    """
    # Known infrastructure keywords that aren't project names
    infra_prefixes = ("Citadel API", "Agent Mail", "Gemini reviewer", "Postgres disk")
    for prefix in infra_prefixes:
        if summary.startswith(prefix):
            return "Infrastructure"

    # "ProjectName: ..." or "ProjectName-xxx ..." patterns
    if ": " in summary:
        candidate = summary.split(":")[0].strip()
        # Filter out short IDs like "Rossum-4rk"
        if "-" in candidate:
            candidate = candidate.split("-")[0]
        if candidate and candidate[0].isupper():
            return candidate

    # "ProjectName PR #N" or "ProjectName #N"
    for token in summary.split():
        if token and token[0].isupper() and token.isalnum():
            return token
        break

    return "Other"
