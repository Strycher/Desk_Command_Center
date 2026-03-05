#!/usr/bin/env bash
# Configure git to use tracked hooks from .claude/hooks/
# Run once per clone: bash .claude/hooks/setup-hooks.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

cd "$REPO_ROOT"

git config core.hookspath .claude/hooks
echo "✓ Git hooks configured: core.hookspath = .claude/hooks"

# Make hooks executable
chmod +x .claude/hooks/pre-commit 2>/dev/null || true
chmod +x .claude/hooks/preflight.sh 2>/dev/null || true

echo "✓ Hook files marked executable"
echo "Done. Hooks are active for this repository."
