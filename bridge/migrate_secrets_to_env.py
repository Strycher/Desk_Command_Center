"""One-shot migration: extract plaintext secrets from bridge_config.json
into a sibling .env file, replacing in-place values with ${ENV:VAR}
placeholders (issue #252).

Run on the Pi where bridge_config.json contains real credentials:
    python migrate_secrets_to_env.py /home/strycher/dcc-bridge

The script:
  1. Backs up bridge_config.json with a timestamp suffix
  2. Walks the config tree, identifying values whose KEY matches the
     SECRET_KEYS pattern (api_key, secret, token, password, credential)
  3. Mints an env var name from the path (e.g. users.strycher.sources.github
     .api_key -> USERS_STRYCHER_GITHUB_API_KEY)
  4. Writes KEY=VALUE pairs to .env (preserving any existing entries that
     don't conflict)
  5. Replaces the in-place value with "${ENV:KEY}" in bridge_config.json
  6. Writes both files

Idempotent: re-running will skip values that are already placeholders.
"""

from __future__ import annotations

import json
import re
import shutil
import sys
import time
from pathlib import Path
from typing import Any

SECRET_KEYS = re.compile(
    r"(api_key|secret|token|password|credential)", re.IGNORECASE,
)
PLACEHOLDER_RE = re.compile(r"^\$\{ENV:[A-Z0-9_]+\}$")
SOURCES_TOKEN = "sources"  # strip from path-based names for brevity


def _path_to_env_name(path: list[str]) -> str:
    """Convert a config path to a safe env-var name.

    e.g. ['users', 'strycher', 'sources', 'github', 'api_key']
         -> 'USERS_STRYCHER_GITHUB_API_KEY'
    """
    parts = [p for p in path if p != SOURCES_TOKEN]
    name = "_".join(parts).upper()
    name = re.sub(r"[^A-Z0-9_]", "_", name)
    return name


def _walk_and_extract(
    node: Any, path: list[str], extracted: dict[str, str],
) -> Any:
    """Recursively walk node. Replace secret-key values with ${ENV:VAR}
    placeholders and record (env_name, value) pairs in `extracted`.

    Skips already-placeholder values. Returns the (possibly modified)
    node — caller must reassign for in-place semantics.
    """
    if isinstance(node, dict):
        for key, value in list(node.items()):
            sub_path = path + [key]
            if isinstance(value, str) and SECRET_KEYS.search(key):
                if not value:
                    continue  # empty string — nothing to extract
                if PLACEHOLDER_RE.match(value):
                    continue  # already migrated
                env_name = _path_to_env_name(sub_path)
                if env_name in extracted and extracted[env_name] != value:
                    raise ValueError(
                        f"Env var name collision: {env_name} maps to two "
                        f"different values"
                    )
                extracted[env_name] = value
                node[key] = "${ENV:" + env_name + "}"
            else:
                node[key] = _walk_and_extract(value, sub_path, extracted)
        return node
    if isinstance(node, list):
        return [_walk_and_extract(x, path, extracted) for x in node]
    return node


def _write_env_file(env_path: Path, new_entries: dict[str, str]) -> None:
    """Append new entries to .env, preserving existing entries that don't
    conflict. Each entry written as KEY=VALUE on its own line."""
    existing: dict[str, str] = {}
    if env_path.exists():
        for raw in env_path.read_text(encoding="utf-8").splitlines():
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            if "=" in line:
                k, v = line.split("=", 1)
                existing[k.strip()] = v
    # Merge — new wins on conflict (the migration is the source of truth)
    merged = {**existing, **new_entries}
    lines = ["# bridge secrets — referenced by bridge_config.json via ${ENV:VAR}",
             "# DO NOT include this file's contents in any output. Gitignored.",
             ""]
    for key in sorted(merged):
        lines.append(f"{key}={merged[key]}")
    env_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    if len(sys.argv) != 2:
        print("Usage: python migrate_secrets_to_env.py <bridge_dir>", file=sys.stderr)
        return 2

    bridge_dir = Path(sys.argv[1])
    cfg_path = bridge_dir / "bridge_config.json"
    env_path = bridge_dir / ".env"

    if not cfg_path.exists():
        print(f"Config not found: {cfg_path}", file=sys.stderr)
        return 1

    # Backup
    backup_dir = bridge_dir / "config_backups"
    backup_dir.mkdir(exist_ok=True)
    ts = time.strftime("%Y%m%d_%H%M%S")
    backup = backup_dir / f"bridge_config.{ts}.pre-env-migration.json"
    shutil.copy(cfg_path, backup)
    print(f"Backed up config to: {backup}")

    # Load, extract, rewrite
    cfg = json.loads(cfg_path.read_text(encoding="utf-8"))
    extracted: dict[str, str] = {}
    cfg = _walk_and_extract(cfg, [], extracted)

    if not extracted:
        print("No new secrets to migrate (all already placeholders or empty).")
        return 0

    print(f"Extracted {len(extracted)} secrets:")
    for k in sorted(extracted):
        masked = extracted[k][:6] + "…" if len(extracted[k]) > 6 else "***"
        print(f"  {k}={masked}")

    _write_env_file(env_path, extracted)
    print(f"Wrote secrets to: {env_path}")

    cfg_path.write_text(json.dumps(cfg, indent=2), encoding="utf-8")
    print(f"Updated config (placeholders only) at: {cfg_path}")

    print("\nNext steps:")
    print("  1. Verify .env is gitignored")
    print("  2. Add 'env_file: .env' to docker-compose.yml bridge service")
    print("  3. cd into bridge dir, restart container: docker compose up -d bridge")
    print("  4. Verify all adapters show 'ok' in /api/adapters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
