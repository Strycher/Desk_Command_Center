"""GitHub adapter — polls repo status via REST API."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

API_BASE = "https://api.github.com"


class GitHubAdapter(BaseAdapter):
    """Polls GitHub REST API for repo status: open PRs, issues, CI status.

    Config from BridgeConfig:
      github.api_key: personal access token
      github.repos: list of "owner/repo" strings
      github.poll_interval: seconds between polls
    """

    def __init__(self, bridge_config: dict[str, Any]) -> None:
        gh_cfg = bridge_config.get("github", {})
        poll_interval = gh_cfg.get("poll_interval", 300)

        super().__init__(
            name="github",
            config=AdapterConfig(
                poll_interval=poll_interval,
                ttl=poll_interval * 2,
                max_retries=2,
                backoff_base=5.0,
                backoff_max=120.0,
            ),
        )
        self.token = gh_cfg.get("api_key", "")
        self.repos: list[str] = gh_cfg.get("repos", [])

    def _headers(self) -> dict[str, str]:
        headers = {
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "DCC-Bridge/0.2.0",
        }
        if self.token:
            headers["Authorization"] = f"Bearer {self.token}"
        return headers

    async def poll(self) -> dict[str, Any]:
        if not self.repos:
            return {"repos": {}}

        results: dict[str, Any] = {}
        async with httpx.AsyncClient(timeout=15.0, headers=self._headers()) as client:
            for repo in self.repos:
                try:
                    results[repo] = await self._poll_repo(client, repo)
                except Exception as exc:
                    logger.warning("GitHub: failed to poll %s: %s", repo, exc)
                    results[repo] = {"error": str(exc)}

        return {"repos": results}

    async def _poll_repo(self, client: httpx.AsyncClient, repo: str) -> dict[str, Any]:
        # Open PRs
        prs_resp = await client.get(
            f"{API_BASE}/repos/{repo}/pulls",
            params={"state": "open", "per_page": 10},
        )
        prs_resp.raise_for_status()

        # Open issues (excludes PRs)
        issues_resp = await client.get(
            f"{API_BASE}/repos/{repo}/issues",
            params={"state": "open", "per_page": 10},
        )
        issues_resp.raise_for_status()

        # Latest workflow runs (CI status)
        runs_resp = await client.get(
            f"{API_BASE}/repos/{repo}/actions/runs",
            params={"per_page": 5},
        )
        runs_resp.raise_for_status()

        prs = prs_resp.json()
        issues_raw = issues_resp.json()
        runs = runs_resp.json()

        # Filter out PRs from issues endpoint
        issues = [i for i in issues_raw if "pull_request" not in i]

        return {
            "prs": prs,
            "issues": issues,
            "runs": runs.get("workflow_runs", []),
        }

    def parse(self, raw: dict[str, Any]) -> dict[str, Any]:
        repos = {}
        for repo_name, data in raw.get("repos", {}).items():
            if "error" in data:
                repos[repo_name] = {
                    "status": "error",
                    "error": data["error"],
                }
                continue

            prs = [
                {
                    "number": pr["number"],
                    "title": pr["title"],
                    "author": pr["user"]["login"],
                    "draft": pr.get("draft", False),
                    "created_at": pr["created_at"],
                    "updated_at": pr["updated_at"],
                }
                for pr in data.get("prs", [])
            ]

            issues = [
                {
                    "number": i["number"],
                    "title": i["title"],
                    "labels": [l["name"] for l in i.get("labels", [])],
                    "assignee": i["assignee"]["login"] if i.get("assignee") else None,
                    "created_at": i["created_at"],
                }
                for i in data.get("issues", [])
            ]

            # Summarize CI: latest run per workflow
            ci_runs = []
            seen_workflows: set[str] = set()
            for run in data.get("runs", []):
                wf_name = run.get("name", "unknown")
                if wf_name in seen_workflows:
                    continue
                seen_workflows.add(wf_name)
                ci_runs.append({
                    "workflow": wf_name,
                    "status": run.get("status"),
                    "conclusion": run.get("conclusion"),
                    "branch": run.get("head_branch"),
                    "created_at": run.get("created_at"),
                })

            repos[repo_name] = {
                "status": "ok",
                "open_prs": len(prs),
                "open_issues": len(issues),
                "prs": prs,
                "issues": issues,
                "ci": ci_runs,
            }

        return {"repos": repos}
