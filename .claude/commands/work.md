# /work — Autonomous Worker Mode

You are entering **Worker Mode** for Desktop Command Center. Follow this protocol exactly.

## 1. Register with Agent Mail

```
ensure_project("/c/Dev/Desk_Command_Center")
register_agent(project_key="/c/Dev/Desk_Command_Center", program="claude-code", model="<your-model>", task_description="Worker session")
```

Save your agent name — you'll need it for file reservations and commits.

Write your agent name to `.agent-identity` in the repo root:

```bash
echo "<YourAgentName>" > .agent-identity
```

## 2. Check Inbox

```
fetch_inbox(project_key="/c/Dev/Desk_Command_Center", agent_name="<YourAgentName>")
```

Read and acknowledge any coordination messages before proceeding.

## 3. Sync Beads and Find Work

```bash
bd dolt pull
bd ready
```

If `bd ready` returns nothing, check `bd list --status=open` for blocked tasks. Report blockers and stop.

## 4. Claim ONE Task

Pick the highest-priority task from `bd ready`:

```bash
bd update <id> --status=in_progress
```

Show the task details with `bd show <id>` before starting.

## 5. Reserve Files

Before editing ANY file, reserve it via Agent Mail:

```
file_reservation_paths(project_key="/c/Dev/Desk_Command_Center", agent_name="<YourAgentName>", paths=["src/ui/*.cpp"], ttl_seconds=3600, exclusive=true, reason="Task #<id>: <description>")
```

Use specific patterns — never reserve the entire repo.

If there are conflicts, message the holding agent and pick a different task.

## 6. Create Feature Branch

```bash
git fetch origin main
git checkout -b feat/<id>-short-desc origin/main
```

## 7. Do the Work

- Follow the implementation plan in `docs/plans/2026-03-05-implementation-plan.md`
- Every successful compile gets committed
- Commit format: `type(#<id>): short description`
- Include Co-Author line

## 8. Complete the Task

After work is done:

```bash
# Push branch and create PR
git push -u origin feat/<id>-short-desc
gh pr create --title "type(#<id>): description" --body "..."

# Update Beads
bd update <id> --status=testing

# Release file reservations
release_file_reservations(project_key="/c/Dev/Desk_Command_Center", agent_name="<YourAgentName>")
```

## 9. Between-Task Reset

1. Release all file reservations
2. Check Agent Mail inbox
3. `bd dolt pull && bd ready`
4. Pick next highest-priority task
5. Repeat from step 4

## 10. Stop Conditions

Stop and report to user if:
- `bd ready` returns nothing (all done)
- Infrastructure is unreachable (Beads, Agent Mail)
- A task requires a design decision not in the plan
- File reservation conflict cannot be resolved
