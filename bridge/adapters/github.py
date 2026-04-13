"""GitHub adapter — polls repo status via REST API."""

from __future__ import annotations

import logging
from typing import Any

import httpx

from .base import AdapterConfig, BaseAdapter

logger = logging.getLogger(__name__)

API_BASE = "https://api.github.com"

# Best-effort deploy/release detection — these workflows affect infra,
# not code quality, so they shouldn't determine CI health status.
# Matched case-insensitively against workflow name.
_DEPLOY_KEYWORDS = {"deploy", "release", "publish", "distribute"}


class GitHubAdapter(BaseAdapter):
    """Polls GitHub REST API for repo status: open PRs, issues, CI status.

    Config from BridgeConfig:
      github.api_key: personal access token
      github.repos: list of "owner/repo" strings
      github.poll_interval: seconds between polls
    """

    def __init__(self, source_config: dict[str, Any]) -> None:
        poll_interval = source_config.get("poll_interval", 300)

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
        self.token = source_config.get("api_key", "")
        self.repos: list[str] = source_config.get("repos", [])
        self.org_token = source_config.get("org_api_key", "")
        self.org_repos: set[str] = set(source_config.get("org_repos", []))
        # Merge org repos into the poll list (deduplicated)
        for r in self.org_repos:
            if r not in self.repos:
                self.repos.append(r)

    def _headers(self, repo: str = "") -> dict[str, str]:
        headers = {
            "Accept": "application/vnd.github+json",
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "DCC-Bridge/0.2.0",
        }
        token = self.org_token if repo in self.org_repos else self.token
        if token:
            headers["Authorization"] = f"Bearer {token}"
        return headers

    async def poll(self) -> dict[str, Any]:
        if not self.repos:
            return {"repos": {}}

        results: dict[str, Any] = {}
        async with httpx.AsyncClient(timeout=15.0) as client:
            for repo in self.repos:
                try:
                    results[repo] = await self._poll_repo(client, repo)
                except Exception as exc:
                    logger.warning("GitHub: failed to poll %s: %s", repo, exc)
                    results[repo] = {"error": str(exc)}

        return {"repos": results}

    async def _poll_repo(self, client: httpx.AsyncClient, repo: str) -> dict[str, Any]:
        headers = self._headers(repo)

        # Repo metadata — open_issues_count is pre-computed, no pagination needed
        repo_resp = await client.get(
            f"{API_BASE}/repos/{repo}",
            headers=headers,
        )
        repo_resp.raise_for_status()
        repo_meta = repo_resp.json()

        # Open PRs
        prs_resp = await client.get(
            f"{API_BASE}/repos/{repo}/pulls",
            params={"state": "open", "per_page": 100},
            headers=headers,
        )
        prs_resp.raise_for_status()

        # Open issues — first page for display details only.
        # True count comes from repo metadata (open_issues_count).
        issues_resp = await client.get(
            f"{API_BASE}/repos/{repo}/issues",
            params={"state": "open", "per_page": 30},
            headers=headers,
        )
        issues_resp.raise_for_status()

        # Latest workflow runs (CI status) — only code-triggered runs.
        # Scheduled/cron workflows (uptime monitors, auto-promote) are excluded
        # so a failing cron job doesn't mark the whole repo as "failing".
        runs_resp = await client.get(
            f"{API_BASE}/repos/{repo}/actions/runs",
            params={"per_page": 20, "exclude_pull_requests": "false"},
            headers=headers,
        )
        runs_resp.raise_for_status()

        prs = prs_resp.json()
        issues_raw = issues_resp.json()
        runs = runs_resp.json()

        # Filter out PRs from issues endpoint
        issues = [i for i in issues_raw if "pull_request" not in i]

        # Only include code/manual-triggered runs for CI status.
        # Scheduled (cron) runs are auxiliary — not CI.
        # workflow_dispatch = manual re-runs (e.g., re-deploy after infra fix).
        ci_events = {"push", "pull_request", "pull_request_target", "workflow_dispatch"}
        ci_runs = [
            r for r in runs.get("workflow_runs", [])
            if r.get("event") in ci_events
        ]

        # open_issues_count includes PRs — subtract PR count for true issue count
        total_open_issues = repo_meta.get("open_issues_count", len(issues))
        true_issue_count = max(0, total_open_issues - len(prs))

        return {
            "prs": prs,
            "issues": issues,
            "runs": ci_runs,
            "open_issues_count": true_issue_count,
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
                    "labels": [lbl["name"] for lbl in i.get("labels", [])],
                    "assignee": i["assignee"]["login"] if i.get("assignee") else None,
                    "created_at": i["created_at"],
                }
                for i in data.get("issues", [])
            ]

            # Summarize CI: latest run per workflow, tagged as deploy or CI
            ci_runs = []
            seen_workflows: set[str] = set()
            for run in data.get("runs", []):
                wf_name = run.get("name", "unknown")
                if wf_name in seen_workflows:
                    continue
                seen_workflows.add(wf_name)
                is_deploy = any(
                    kw in wf_name.lower() for kw in _DEPLOY_KEYWORDS
                )
                ci_runs.append({
                    "workflow": wf_name,
                    "status": run.get("status"),
                    "conclusion": run.get("conclusion"),
                    "branch": run.get("head_branch"),
                    "created_at": run.get("created_at"),
                    "is_deploy": is_deploy,
                })

            # Derive summary CI status for firmware display.
            # Deploy/release workflows are excluded — they reflect infra
            # health, not code quality. Unknown workflows default to CI.
            code_runs = [r for r in ci_runs if not r.get("is_deploy")]
            if code_runs:
                has_failure = any(r["conclusion"] == "failure" for r in code_runs)
                has_pending = any(r["status"] != "completed" for r in code_runs)
                if has_failure:
                    ci_status = "failing"
                elif has_pending:
                    ci_status = "pending"
                else:
                    ci_status = "passing"
            else:
                ci_status = "passing"

            # Use pre-computed count from repo metadata (not capped by pagination)
            open_issues = data.get("open_issues_count", len(issues))

            repos[repo_name] = {
                "status": "ok",
                "open_prs": len(prs),
                "open_issues": open_issues,
                "prs": prs,
                "issues": issues,
                "ci": ci_runs,
                "ci_status": ci_status,
            }

        return {"repos": repos}
