# Phase A — the seam

## Context

Phase 0 verified the skeleton; the repo builds, links the engine submodule at 4.1.0, embeds the card
art and is green on three runners. Phase A is the first phase that writes client code, and the plan
calls it the highest-risk phase in the project despite being one of the smallest.

The problem it solves: `GameEngine::run()` blocks until the game ends, and calls its observer
callbacks on whatever thread called it. Run it on the GUI thread and the window stops repainting
until the game is over. So the engine goes on a worker thread, and everything it produces has to
cross a thread boundary as **values** — the engine has no locking anywhere, and a GUI-thread read
racing a deal is a torn read of a hand mid-mutation.

Phase A builds that seam and nothing else. The UI is deliberately a `QTextEdit` printing one line
per snapshot: with no scene, no card art and no layout in the process, a wrong card in the log
**must** be a snapshot bug. Drawing a table here merges phases A and B and gives that up.

**Reading before writing any of it:** plan §§2, 3, 4.0–4.3, 5, 6, and §21. All of them.

## Decisions taken (differ from a literal reading of the docs)

1. **The queued payload is `std::shared_ptr<const GameSnapshot>`, not `GameSnapshot` by value.**
   §4.1's code block shows by-value; §21.1 amends it and wins. The snapshot is still *built* by
   value on the game thread and only wrapped on the way out, so §4.0's "every member copyable and
   default-constructible" rule is unchanged.

2. **Phase A lands as two PRs, split at A3/A4.** A1–A3 need no `QThread` and are testable on the
   test's own thread; A4 is the threading. Keeps the porting review separate from the threading one.

3. **A6 is an invariant test over the snapshot sequence, not a transcript diff against the terminal
   client.** This replaces what the build order and §21.5 specify, and the reasoning is recorded
   below because it contradicts a shipped document.

### Why A6 changed

The docs specify: run the same seed through the terminal client and diff the transcripts. Three
things are wrong with it in practice.

- **The outputs are structurally incompatible.** The terminal redraws the whole screen per event —
  19,040 lines for one seeded game, 1,259 of them unique, the header alone reprinted 1,124 times.
  The Qt log appends ~1,200 lines. `diff` between them is not noisy, it is meaningless, so the check
  needs a normalisation step no document mentions.
- **It couples two apps that are meant to diverge.** The terminal is relevant only as the *original
  of a hand transcription* (§21.4 lists the eight files being copied), which is a one-time concern
  that expires when the port is reviewed. §4.0 already documents a case where the desktop client
  deliberately does something the terminal gets loose — and says the terminal should move to match,
  not the reverse. Permanent tooling pinned to terminal output would enshrine that backwards.
- **It needs the terminal repo at run time**, which the desktop repo has no reference to — not a
  submodule, not in CI. So it could never be a CI gate anyway.

What replaces it costs less and catches the same bug class. The invariants below are asserted over
**the snapshot sequence this client produces**, not over the engine. That is not a re-test of the
engine's own property suite: a port that misreads the lead seat breaks the invariant in *our*
snapshots while the engine stays perfectly correct. It needs no log file, no format, no parsing, no
GUI and no second repo — the test holds the snapshots in a `std::vector` in memory.

What is given up: a field with no internal invariant (`bidOrder`, `winStreak`, `cardsLeft`) that is
misread *consistently* is not caught. A1's literal assertions and reading the port side by side
against `GameView.cpp` cover that residue.

---

## Structural prerequisite (first commit of PR 1)

`CMakeLists.txt` currently has one `qt_add_executable` and no test target. Tests need something to
link against, so split it the way `romanian_whist_terminal` does — its comment is *"Everything but
main(), so the tests have something to link against. The executable is then just an entry point."*

- `qt_add_library(desktop_core STATIC ...)` holding everything except `main.cpp`, linking
  `Qt6::Widgets Qt6::Svg RomanianWhist::engine` PUBLIC, with `src/` as a PUBLIC include dir.
  AUTOMOC is already on globally via `qt_standard_project_setup()`, which the `Q_OBJECT` classes
  need.
- `qt_add_executable(romanian_whist_desktop ... src/main.cpp)` links `desktop_core`.
- `option(WHIST_DESKTOP_BUILD_TESTS "..." ${PROJECT_IS_TOP_LEVEL})`, then `enable_testing()` +
  `add_subdirectory(tests)`.
- `tests/CMakeLists.txt` copied from the terminal's: `FetchContent` Catch2 **v3.16.0** (same tag the
  engine uses, so both resolve to one target), link `desktop_core` + `Catch2::Catch2WithMain`,
  `CATCH_CONFIG_ENABLE_OPTIONAL_STRINGMAKER`, `catch_discover_tests`.
- Port `tests/CardStringMaker.h` verbatim — without it Catch2 renders every `Card` as `{?}`.
- **Uncomment the CI test step** in all three jobs of `.github/workflows/ci.yml`. It is already
  written, including `--no-tests=error` (a build with tests disabled would otherwise pass green
  having run nothing).

---

## PR 1 — A1 to A3 (no threading)

### A1 — the snapshot, standing alone

- `src/core/GameSnapshot.h` — `HandEntry`, `SeatView`, `PlayedCardView`, `GameSnapshot` exactly as
  §4.0 declares them. Drops `GameView`'s `message`, `hint`, `error`, `elapsedMinutes`, `Phase` and
  `HandEntry::choice`. Copyable, default-constructible, no references, no `unique_ptr`, no bare
  `Seat` (it has no default constructor).
- `src/core/CardOrder.{h,cpp}` — `inDisplayOrder` / `suitPosition` lifted out of the terminal's
  `CardFormat` as free functions, losing the `Terminal&` member. `layOutHand` needs the comparator.
- `src/core/HandLayout.{h,cpp}` — ported, **keeping `positions`** (the local `ordered` the terminal
  computes and throws away) and dropping `choices` and `HandEntry::choice` with the typed number.
- `src/core/Snapshot.{h,cpp}` — `refreshFromEngine(GameSnapshot&, const GameEngine&)`, ported from
  `GameView.cpp` including the `rebuildTable` helper. Keep every comment about `canSeeHand()` and
  the lead seat.

Each ported file keeps a header comment naming the repo and file it came from (§21.4).

**Engine API facts that will otherwise be got wrong** — verified against the headers:

- `getCurrentRoundIndex()` is **0-based**; `roundNumber = index + 1`.
- The getters are `getCurrentTrumpCard()`, `getCurrentRoundTrickCount()`, `getRoundCount()`,
  `getCurrentTrickNumber()` — not the shorter names the plan's prose implies.
- `trump` is `std::optional<Card>`; there is no trump in an 8-card round.
- `PlayerList` is indexed by `size_type`, so `getPlayers().at(seat.index)`.
- **`leadSeat` must be read off the trick's first played card**, never from `getTrickLeaderSeat()` —
  that has already moved to the winner by the time `onTrickWon` fires. The engine's own header warns
  about this and it is the documented cause of "wrong names on the last trick of a round".
- `isWinning` comes from `getCurrentTrickLeader()`, never recomputed.

Tests (`tests/SnapshotTests.cpp`, `tests/HandLayoutTests.cpp`):

- The hand round trip, ported from the terminal's `HandLayoutTests.cpp` with `choices` assertions
  rewritten against `positions`: `hand[layout.positions[i]] == layout.entries[i].card` for all `i`.
  The terminal's fixture already has a hand whose display order shares no prefix with deal order.
- `refreshFromEngine` against a seeded engine at four points — after `start()`, mid-bidding,
  mid-trick, after the final round is scored — asserted against literals.
- **The two that are silent when wrong:** `humanSeat` set *before* the refresh (leave it default and
  every hand arrives empty, the table renders fine, and the player never sees a card), and `hand`
  empty in a Forehead or Hidden round.

### A2 — the provider, headless

- `src/engine/GameAbandoned.h` — the README's name for it; matching it matters.
- `src/engine/QtMoveProvider.{h,cpp}` and `SeatProvider` (§4.2). Deliberately **not** `QObject`s:
  `SeatProvider` is destroyed on the game thread inside `~GameEngine`.
- `BetPrompt` / `PlayPrompt` as §4.2 declares them.

Facts from the headers that shape this:

- **`PlayContext` carries no `roundType`** — `BetContext` does. So the bridge caches it in
  `onRoundStarted` and the provider reads it there. It cannot be inferred from hand size: a Normal
  1-trick round looks identical to a Forehead one.
- **`BetContext::hand` and `PlayContext::hand` are `const&` references.** The context must not
  outlive the call, so anything sent to the GUI is copied out first.
- `CardValidator::getLegalCards` is a **non-static const instance method** — hold a member.
- `BetContext` has no `handSize`; `hand.size()` is the trick count and the highest legal bid.

Tests (`tests/QtMoveProviderTests.cpp`), all headless:

- a stale `submit()` is dropped; a second `submit()` for the same prompt is dropped
- **a `submit()` with nothing parked does not answer the *next* prompt** — this is the bug the
  generation gate exists for, otherwise found in a real game as "it played a card I did not click"
- `abandon()` throws `GameAbandoned` whether parked or not yet parked
- a `submit()` naming an illegal slot re-prompts rather than returning; so does one past the end

### A3 — the bridge

- `src/engine/GameBridge.{h,cpp}` — all **thirteen** `IGameObserver` callbacks overridden (all are
  non-pure with empty default bodies, so a missed one is silent), the `Pacing` struct, `publish()`,
  the signals, `pace()` and `cancelPacing()`.
- `Pacing` takes the terminal's numbers: 700ms beat, **250ms spectator beat**, 1200ms trick dwell.
  The spectator split is not a detail — phase A *is* a spectator game, and 700ms a card makes a
  26-round schedule a very long sit.
- `publish()` sets `humanSeat` **before** calling `refreshFromEngine`, then wraps the finished
  snapshot in `std::make_shared<const GameSnapshot>` on the way out.
- Signals carry `unsigned int seat`, never `Seat` — it has no default constructor and so can never
  be a queued payload.

Test: `pace()` returns early on `cancelPacing()` and **does not throw**. One assertion, and §6's
entire shutdown story rests on it.

---

## PR 2 — A4 to A6 (the thread)

### A4 — the thread

- `src/engine/GameController.{h,cpp}` — `prepare()` / `start()` / `stop()`, the `engineMutex`, the
  `stopRequested` latch, and the run slot with its catch-all. Worker-object pattern, never a
  `QThread` subclass.
- The ordering is load-bearing and three separate bugs live in getting it wrong: `prepare()` builds
  the bridge on the GUI thread → `MainWindow` connects → `start()` moves it and starts the thread →
  the run slot constructs the engine, `addObserver`, `start(std::move(setup))`, publishes the engine
  pointer under `engineMutex` **and reads `stopRequested` back while still holding it**, then
  `run()`.
- `main.cpp` — `qRegisterMetaType` for every payload **before any `connect()`**, including
  `std::vector<romanian_whist::Standing>`, which is easy to miss because it is the one payload that
  is not one of our own types.
- **Leave every bridge connection on `AutoConnection`.** A `DirectConnection` would run the slot on
  the game thread while the provider's non-recursive mutex is held, and deadlock.
- `GameEngine` is neither copyable nor movable, so the run slot owns it as a local. `start()`
  consumes the `GameSetup` even when it throws, so a retry needs a freshly built one.

Shutdown tests (`tests/GameControllerTests.cpp`): `stop()` before `prepare()`; `stop()` on a
spectator game (null `movePlayer` — this is phase A's permanent state, not an edge case); `stop()`
twice; `stop()` after the game finished on its own; and the hard one — `stop()` between `start()`
and the engine being published, asserting the game ends through **`onGameStopped()`** rather than
playing its schedule out. Asserting *which* of the two fired is the test: a spectator game with
pacing cancelled finishes either way and quickly, so "it terminated" says nothing.

These need a `QCoreApplication` (not `QApplication` — no display required) and a pumped event loop
for queued delivery. Pacing is set to zero in tests.

### A5 — a window that logs

- `MainWindow` with a `QTextEdit` and nothing else; `render()` appends a line per snapshot.
- Hardcoded spectator setup: `humanSeat = std::nullopt` (**not** an arbitrary seat — pass one and
  that bot silently plays without a beat and `canSeeHand()` is asked about a viewer who is not
  there), `spectatorBeat`, a fixed `shuffleSeed`, `GameStructure::S_181`, four
  `AiMoveProvider`s.
- Wire it `prepare()` → `connect()` → `start()`. `onGameStarted()` fires from inside
  `GameEngine::start()`, so connecting after the thread starts misses the first snapshot outright.
- Log lines are **event-shaped, not a state dump** — free at write time and far more readable.

### A6 — the invariant test

`tests/SnapshotSequenceTests.cpp`. Run a seeded spectator game with the bridge attached, collect
every published snapshot into a `std::vector<GameSnapshot>`, and assert over the sequence. No GUI,
no file, no parsing.

- the seat recorded as leading a trick equals the seat of the first card played in that trick
- every completed trick holds exactly one card per seat
- a card that appears on the table was in that seat's hand in the previous snapshot and is gone from
  it in the next; `cardsLeft` decrements in step
- no card appears twice within a round's deal
- tricks won across seats sums to the round's trick count
- `betSum` and `betsPlaced` agree with the per-seat `hasBet` / `bet` fields
- `roundNumber` runs 1..`roundCount` without gaps or repeats; the last two rounds are Forehead then
  Hidden under the default `endWithForeheadAndHidden`
- `hand` is empty in exactly those two rounds, and in every round of a spectator game
- totals accumulate: each seat's `totalScore` after a round equals the previous total plus
  `roundScore`
- `isWinning` is set on exactly one card of any non-empty table

---

## Verification

```
cmake --preset default -DCMAKE_PREFIX_PATH=/home/dan/Qt/6.11.2/gcc_64
cmake --build build -j
ctest --preset default --output-on-failure --no-tests=error
./build/romanian_whist_desktop
```

Phase A is done when:

- a seeded bot game logs start to finish in the window
- the window stays responsive throughout — drag it around mid-game
- **closing it mid-game exits through `onGameStopped()`**, with no leaked thread and no hang on
  `thread.wait()`. This is a real assertion, not a formality: a spectator game is asleep in `pace()`
  almost all the time, so the close lands there, and it reaches the clean path only because `pace()`
  returns instead of throwing.
- the full test suite passes, and CI is green on three runners with the test step now live

**If you see `GameAbandoned` on that close, `pace()` is throwing and §6 has been implemented
backwards.** That is the single most likely way to get this phase subtly wrong — and phase A has no
human seat, so nothing here should ever produce it.

## Documentation to update at the end

`QT_CLIENT_BUILD_ORDER.md`: tick A1–A6, mark the phase complete as phase 0 was, and **rewrite the A6
step** to the invariant test with a short note on why the transcript diff was dropped. The plan's
§21.5 should get a line saying it was superseded — it is currently the only amendment recorded as
"Taken" that no longer is.

`docs/PHASE_A_PLAN.md` is currently untracked. It gets committed as part of **PR 1**, so the plan
and the first code it describes arrive together rather than as a doc-only commit that CI skips.
Once phase A is ticked in the build order this file is history rather than instruction, and the
build order stays the document that holds the order.
