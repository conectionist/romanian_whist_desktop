# CLAUDE.md

## Never freeze the owner's computer

The owner uses this machine while you work. A command that takes every core makes it unusable,
and that is worse than any wait. **Slower is always acceptable; a frozen desktop never is.**

- **Builds:** never a bare `-j` (it means unlimited). Always `nice -n 19 cmake --build <dir> -j 2`.
  This overrides the `-j` in `docs/LOCAL_VERIFICATION.md`.
- **Tests:** `nice -n 19 ./build/tests/desktop_tests` or `nice -n 19 ctest --preset <p> -j 1`.
  Run a subset by tag (`"[rack]"`, `"[table]"`) while iterating, and the whole suite once at the end.
- **One heavy command at a time.** Never build or test several presets in one command or back to
  back without being asked. Build and test the other presets (`release`, `clang`) only when the
  owner asks, or once before a PR, one preset per command.
- **Timing runs (`[.timing]`) and sanitizer builds** only when the owner asks, one run per command.
  Don't loop them.
- **Long jobs go in the background** (`run_in_background`), still under `nice` and `-j 2`, so the
  owner can keep working and can stop them.

## Branch workflow

Branches are stacked: `master` -> `claude/<name>` -> `redesign/phase-N-*`.

Phase branches are reviewed and PR'd against their parent `claude/*` branch, never `master`.
When running `/code-review` on a phase branch, diff against that parent branch (currently
`claude/zealous-lamport-zac87a`), or pass the PR number so GitHub's base branch is used.

## Changing a PR: never `gh pr edit`

`gh pr edit` always fails on this repo, with "GraphQL: Projects (classic) is being deprecated …
(repository.pullRequest.projectCards)", and changes nothing. Don't try it first. Change a PR's
description or title through the REST API from the start, with the body written to a file:

```
gh api -X PATCH repos/conectionist/romanian_whist_desktop/pulls/<N> -F body=@<file>
gh api -X PATCH repos/conectionist/romanian_whist_desktop/pulls/<N> -f title='<title>'
```

`gh pr view` and `gh pr create` work as usual.

## End of every redesign phase: show the owner side-by-side screenshots

The owner wants to see each phase's visual result without running the app. When a
`redesign/phase-N-*` phase is finished, its closing message must include these comparisons:

1. Run the screenshot tool (`tests/ScreenshotTool.cpp`) from the phase branch's `default` build:
   `WHIST_SHOTS_DIR=build/shots/phase-N ./build/tests/desktop_tests "[.screenshot]"`.
   It writes `table-3-players.png` … `table-6-players.png` and `bidding.png` at 1920×1080, and
   `table-empty.png`: the table alone at 1920×1034, which is exactly the mock-up's scale and place.
   It also writes `hand-1-card.png`, `hand-4-cards.png` and `hand-8-cards.png`: the table with your
   hand, at the same 1920×1034 (added in phase 7). And since phase 10, `sheet-pause.png`,
   `sheet-round.png`, `sheet-game-over.png`, `sheet-rules.png` and `sheet-settings.png` (each sheet
   over a table, 1920×1034) and `menu.png` (the main-menu page, 1920×1080).
2. Pair each one with the mock-up picture of the same name in `docs/ui-redesign/screenshots/`
   (`.jpg`): client on the left, mock-up on the right, each labelled. Save the pair as
   `build/shots/phase-N/compare-<name>.png` (Pillow is installed). `build/` is git-ignored, so
   these are never committed. `table-empty.png` has no namesake: pair it with
   `table-4-players.jpg` cropped below its 46 px menu bar (y 46–1080), as `compare-table-empty.png`.
   The `hand-*.png` shots pair with their namesakes cropped the same way. Those mock-up pictures
   come from `docs/ui-redesign/mockup/source/capture_hands.py`, which takes them again if the
   mock-up changes. Add a close-up of each hand pair too: the hand is small in a whole page. The
   sheet and menu shots have no namesake (the mock-up has no pictures of them): link them on their
   own.
3. Link the comparison images in the message. Add a line or two on what moved closer to the
   mock-up and what still differs. Until phase 8 replaces the side panels, the page is expected to
   be a mix of new and old.

If a phase's screenshots are pixel-identical to the previous phase's, say so, and still link the
comparisons.

## Explaining something visual: text first, then offer a recording

When something visual needs explaining and isn't obvious, explain it in text first, with a
screenshot if that helps. Then **offer** to show it in the running app. Don't record unless the
owner says yes. The owner finds the recording very useful, and the GIF most of all, but it isn't
the default.

When the owner says yes:

1. The owner starts the app and sets up the state, or uses the one already on screen. Before
   moving the mouse, say how long the demo will take and whether it will click: a click plays a card
   in their game.
2. Find the window with `xdotool search --onlyvisible --classname romanian_whist_desktop`, then
   `xdotool getwindowgeometry <id>`. Screen coordinates are the window's position plus window
   coordinates. The window may be on the second monitor.
3. Drive it from a short Python script in the scratchpad. Move with `xdotool mousemove` in 1 px
   steps with short pauses, so hover events fire and the owner can watch it happen.
4. Record the area of interest with the real cursor. ImageMagick's `import` leaves the cursor out,
   so use ffmpeg. Stop the recording by writing `q` to its stdin. Take stills the same way, with
   `-frames:v 1` in place of the codec options:
   ```
   nice -n 19 ffmpeg -y -loglevel error -f x11grab -draw_mouse 1 -framerate 25 -video_size <w>x<h> \
       -i :0.0+<x>,<y> -c:v libx264 -preset ultrafast -pix_fmt yuv444p demo.mkv
   nice -n 19 ffmpeg -y -loglevel error -i demo.mkv \
       -vf "fps=12,scale=780:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse" demo.gif
   ```
5. Put two or three labelled stills side by side with Pillow, enlarged around the spot that
   matters. Keep everything in the scratchpad, never in the repo.
6. Link the GIF and the stills, with a line for each frame on what it shows.
