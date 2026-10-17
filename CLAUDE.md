# CLAUDE.md

## Branch workflow

Branches are stacked: `master` -> `claude/<name>` -> `redesign/phase-N-*`.

Phase branches are reviewed and PR'd against their parent `claude/*` branch, never `master`.
When running `/code-review` on a phase branch, diff against that parent branch (currently
`claude/zealous-lamport-zac87a`), or pass the PR number so GitHub's base branch is used.
