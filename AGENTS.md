## Agent skills

### Worktree setup

The user authorizes agents to run `codegraph init` in each new worktree for this repository. Reuse an existing `.codegraph/` index. If initialization fails, report it and follow the global CodeGraph fallback.

### Code reuse

Prefer reusing suitable code from Notepad4 and wxSTC. Let project requirements and maintainability guide the choice: adapt existing code or implement a focused solution when reuse would add unnecessary complexity or compromise the intended behavior.

### Issue tracker

Issues and specs live in GitHub Issues; prefer an available GitHub integration/plugin, with `gh` CLI as fallback. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the default canonical triage labels. See `docs/agents/triage-labels.md`.

### Domain docs

Use the single-context domain-doc layout. See `docs/agents/domain.md`.

### Native validation

For local native tests, standalone GUI probes, or SDK/package checks, use the entry points in `docs/ci.md`.
