# Issue tracker: GitHub

Issues and specs for this repo live as GitHub issues. Prefer an available GitHub integration/plugin when it supports the operation; otherwise use the `gh` CLI.

## Conventions

- **Create an issue**: use the GitHub integration when available, otherwise `gh issue create --title "..." --body "..."`.
- **Read an issue**: use the integration when available, otherwise `gh issue view <number> --comments`.
- **List issues**: use the integration when available, otherwise `gh issue list --state open --json number,title,body,labels,comments`.
- **Comment on an issue**: use the integration when available, otherwise `gh issue comment <number> --body "..."`.
- **Apply / remove labels**: use the integration when available, otherwise `gh issue edit <number> --add-label "..."` / `--remove-label "..."`.
- **Close**: use the integration when available, otherwise `gh issue close <number> --comment "..."`.

Infer the repo from `git remote -v`; `gh` does this automatically inside a clone.

## Pull requests as a triage surface

**PRs as a request surface: no.**

When a skill says “publish to the issue tracker”, create a GitHub issue.

When a skill says “fetch the relevant ticket”, read the referenced GitHub issue with comments.

## Wayfinding operations

The map is a single issue with child issues as tickets. Use the GitHub integration when available, otherwise the corresponding `gh issue`, `gh pr`, and `gh api` commands.
