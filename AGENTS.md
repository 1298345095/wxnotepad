## Agent skills

### Worktree setup

The user authorizes the main agent and subagents to run `codegraph init` in each new worktree for this repository, without further confirmation. Run it from the worktree's repository root before exploring or editing code; reuse an existing `.codegraph/` index. If initialization is unavailable or fails, report the failure and use the global CodeGraph fallback.

### Issue tracker

Issues and specs live in GitHub Issues; prefer an available GitHub integration/plugin, with `gh` CLI as fallback. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the default canonical triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

Use the single-context domain-doc layout. See `docs/agents/domain.md`.
