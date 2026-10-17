# CLAUDE.md

## Branch workflow

Branches are stacked: `master` -> `claude/<name>` -> `redesign/phase-N-*`.

Phase branches are reviewed and PR'd against their parent `claude/*` branch, never `master`.
When running `/code-review` on a phase branch, diff against that parent branch (currently
`claude/zealous-lamport-zac87a`), or pass the PR number so GitHub's base branch is used.

## End of every redesign phase: show the owner side-by-side screenshots

The owner wants to see each phase's visual result without running the app. When a
`redesign/phase-N-*` phase is finished, its closing message must include these comparisons:

1. Run the screenshot tool (`tests/ScreenshotTool.cpp`) from the phase branch's `default` build:
   `WHIST_SHOTS_DIR=build/shots/phase-N ./build/tests/desktop_tests "[.screenshot]"`.
   It writes `table-3-players.png` … `table-6-players.png` and `bidding.png` at 1920×1080, and
   `table-empty.png`: the table alone at 1920×1034, which is exactly the mock-up's scale and place.
2. Pair each one with the mock-up picture of the same name in `docs/ui-redesign/screenshots/`
   (`.jpg`): client on the left, mock-up on the right, each labelled. Save the pair as
   `build/shots/phase-N/compare-<name>.png` (Pillow is installed). `build/` is git-ignored, so
   these are never committed. `table-empty.png` has no namesake: pair it with
   `table-4-players.jpg` cropped below its 46 px menu bar (y 46–1080), as `compare-table-empty.png`.
3. Link the comparison images in the message. Add a line or two on what moved closer to the
   mock-up and what still differs. Until phase 8 replaces the side panels, the page is expected to
   be a mix of new and old.

If a phase's screenshots are pixel-identical to the previous phase's, say so, and still link the
comparisons.
