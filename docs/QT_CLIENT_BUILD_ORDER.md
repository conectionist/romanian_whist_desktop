# Build order

**How to get from an empty `MainWindow` to a shipped Qt client, in the order that makes each bug
have one plausible cause.**

`QT_CLIENT_PLAN_proposal1.md` is the plan: what the client is, why every decision went the way it
did, and what the engine actually guarantees. **This document is the order**, and nothing more. It
restates none of the plan's reasoning — two documents describing the same client is how they come to
disagree (that plan's Appendix B is a list of exactly that happening) — so every step below names
the section to read *before* writing the code, and stops.

**Where they conflict, the plan wins.** If you find yourself arguing with a step here, the argument
is with §§2–6 there, and that is where it should be settled.

---

## How to use this

Each phase is a vertical slice that ends in something runnable. Each step inside a phase is roughly
a commit. The checkboxes are meant to be ticked.

Two rules travel with every step and are not repeated:

- **Every user-facing string goes through `tr()` as it is written.** `i18n/romanian_whist_desktop_ro_RO.ts`
  already exists. Phase F is where a string that skipped this is found, and sweeping for it then is
  far more expensive than not making the mistake (§7).
- **No colour, no pixmap path and no card metric is ever constructed outside `Theme` or
  `CardRenderer`** (§18.1). This costs nothing while you are writing the first widget and is a
  rewrite once there are twelve.

| Phase | | Ends with |
|---|---|---|
| **0** ✅ | Skeleton — verify what is already committed | The window opens and a card pixmap is non-null |
| **A** ✅ | The seam | A seeded bot game logs start to finish, and closing it mid-game exits cleanly |
| **B** | Cards on a table | A spectated game is watchable and legible |
| **C** | The human plays | A full game is playable, Forehead and Hidden rounds included |
| **D** | The game around the game | No command line, no restart between games |
| **E** | Polish | Part II is on screen |
| **F** | Ship | Three platforms produce installable artifacts |

**Do not reorder A and B.** The whole value of A is that there is no rendering in the process, so a
wrong card in the log cannot be a layout bug. Drawing a table first merges the two phases and gives
that up (§8, phase A).

---

## Phase 0 — verify the skeleton ✅ COMPLETE

Already committed. This phase is confirming it, which takes minutes and saves an afternoon of
blaming the wrong layer later.

- [x] `cmake --preset default && cmake --build build` — the engine submodule resolves and links.
- [x] The window opens.
- [x] **The card art is actually embedded.** `CMakeLists.txt` names three silent ways to end up with
      nothing in the binary, and all three produce a build that succeeds. Put a temporary line in
      `main()`:
      ```cpp
      Q_ASSERT(!QPixmap(":/cards/ace_of_spades.png").isNull());
      ```
      and one for the SVG through `QSvgRenderer` — not through `QPixmap`, which needs the
      `imageformats` plugin the deploy step strips (§18.1). Delete both once they pass.
- [x] CI is green on all three runners.

**Done when:** all four boxes are ticked. Nothing below is worth starting otherwise.

### What the run found

Kept because the next person to run this — on another machine, or after a Qt bump — hits the same
three things.

- **Point the preset at Qt explicitly** if it is not on `PATH`, which is the normal case for a
  `~/Qt` install: `cmake --preset default -DCMAKE_PREFIX_PATH=<qt>/gcc_64`. `which qmake6` finding
  nothing does not mean Qt is missing.
- **Run the card-art asserts against a bogus path first**, confirm they abort, *then* point them at
  the real one. A passing `Q_ASSERT` proves nothing on its own — it compiles out under `QT_NO_DEBUG`,
  so the `release` preset's binary checks nothing and passes exactly as loudly. Use the `default`
  (Debug) preset, and prove the probe has teeth before trusting it.
- **The diagnostic is cheaper than the run.** `build/qrc_cards.cpp` (~30 MB) is generated only if
  rcc actually ran, so its absence catches all three silent-embedding failures without launching
  anything. `CMakeLists.txt` says this; it is worth doing first rather than last.

Two stale comments in `CMakeLists.txt` were fixed on the way past: a pointer to a
`docs/QT_CLIENT_PLAN.md` that no longer exists, and a claim that `qt_standard_project_setup()` turns
on AUTORCC — which it does not, and which the same file's card-art comment already refuted forty
lines further down. That second one was the exact misconception this phase's card check exists to
catch, asserted as true above the comment disproving it.

---

## Phase A — the seam ✅ COMPLETE

**Goal.** The engine runs a full game on a worker thread, every frame arrives on the GUI thread as a
value, and closing the window mid-game ends it cleanly.

**Read first:** §§2, 3, 4.0–4.3, 5, 6. All of them, before writing any of it. This is the phase
where reading ahead actually pays.

> **Most of this phase is not threaded.** A1–A3 need no `QThread` at all and are testable from
> Catch2 on the test's own thread. Only A4 is the hard part, and by the time you reach it everything
> it coordinates already works. That is the reason for this order.

### A1 — the snapshot, standing alone

- [x] `src/core/GameSnapshot.h` — `HandEntry`, `SeatView`, `PlayedCardView`, `GameSnapshot` exactly
      as §4.0 declares them. Copyable, default-constructible, no references, no `unique_ptr`.
- [x] `src/core/HandLayout.{h,cpp}` — ported from the terminal's, **keeping `positions`** (the local
      it computes and throws away) and dropping `choices` (§4.0).
- [x] `src/core/Snapshot.{h,cpp}` — `refreshFromEngine(GameSnapshot&, const GameEngine&)`, ported
      from the terminal's `GameView.cpp`. Keep its comments about `canSeeHand()` and the lead seat.
- [x] `tests/` — CMake, Catch2, and the snapshot test: drive a real `GameEngine` with
      `AiMoveProvider`s and a fixed `shuffleSeed`, assert the snapshot at four points against
      literals (§8, "the snapshot, against a seeded engine").

**The two assertions that matter most** are `humanSeat` set *before* the refresh, and `hand` empty
in a Forehead or Hidden round. Both are §4.0, and both are silent when wrong.

### A2 — the provider, headless

- [x] `src/engine/GameAbandoned.h`.
- [x] `src/engine/QtMoveProvider.{h,cpp}` + `SeatProvider` (§4.2).
- [x] Headless tests: the generation gate drops a stale `submit()`, drops a second `submit()` for
      the same prompt, and a `submit()` with nothing parked does **not** answer the next prompt;
      `abandon()` throws whether the provider is parked or not yet parked (§8).

> **Yes, the provider is built in A even though phase A has no human seat.** `GameController` holds a
> `std::unique_ptr<QtMoveProvider>`, which needs the complete type, and the plan's test list puts
> the provider's headless tests in the phase-A bucket. It is written here, tested here, and first
> *used* in phase C.

### A3 — the bridge

- [x] `src/engine/GameBridge.{h,cpp}` — all thirteen `IGameObserver` callbacks, the `Pacing` struct,
      `publish()`, the signals, `pace()` and `cancelPacing()` (§4.1).
- [x] Test: `pace()` returns early on `cancelPacing()` and **does not throw**. One assertion, and
      §6's entire shutdown story rests on it.

### A4 — the thread

- [x] `src/engine/GameController.{h,cpp}` — `prepare()` / `start()` / `stop()`, the `engineMutex`,
      the `stopRequested` latch, the run slot with its catch-all (§4.3).
- [x] The controller **mirrors the bridge's twelve signals** and `prepare()` relays them, so the GUI
      connects to the controller once and never re-connects. `prepare()` then purges its own queue
      with `removePostedEvents(this, QEvent::MetaCall)` after the join — Qt purges posted events for
      a dead *receiver* only, so the per-game bridge dying takes none of its backlog with it.
- [x] `main.cpp` — `qRegisterMetaType` for every payload, **before any `connect()`** (§5).
- [x] Shutdown tests: `stop()` before `prepare()`; `stop()` on a spectator game (null `movePlayer`);
      `stop()` twice; `stop()` after the game finished on its own; and the hard one — `stop()`
      between `start()` and the engine being published, asserting the game ends through
      `onGameStopped()` rather than playing its schedule out (§8). That last one spins on
      `isStopRequested()` before releasing the parked game thread; nothing else about a landed
      `stop()` is observable from outside.
- [x] Reuse tests: a second game sees neither game one's undelivered snapshots and `gameOver`, nor
      its `gameFailed`.

### A5 — a window that logs

- [x] `MainWindow` with a `QTextEdit` and nothing else. `render()` appends a line per snapshot.
- [x] Hardcoded spectator setup: `humanSeat = std::nullopt`, `spectatorBeat`, a fixed `shuffleSeed`.
- [x] Wire it `connect()` once in the constructor → `prepare()` → `start()`, connecting to the
      **controller**, not the bridge. `prepare()` makes the relay before the thread starts, which is
      what keeps `onGameStarted`'s snapshot (§4.3).
- [x] **First chance to check the two open pacing placements** — carried forward from A3, where
      nothing could run. Even a text log shows them in the gaps between appended lines: the 1.2s
      dwell should land on the round-result frame and not the blank next-round one, and a bot's card
      should be preceded by a beat rather than appearing instantly after "waiting on Ana". See the
      note beside the §4.1 sketch for what the terminal does and why betting is already right.
      Defer the judgement to B2 if the log is too coarse to tell — just don't lose the question.

### A6 — the acceptance test

- [x] `tests/SnapshotSequenceTests.cpp` — invariants over the snapshot **sequence** a seeded game
      produces, collected through `GameBridge` so `publish()`'s own work is covered too. Nine of
      them, each carrying the mutation that must make it fail, all nine proven by applying it.

> **This replaces the transcript diff §21.5 specifies, and that amendment is superseded.** Three
> reasons, in `docs/PHASE_A_PLAN.md`: the two outputs are structurally incompatible (the terminal
> redraws the whole screen per event — 19,040 lines against this client's ~1,300), it couples two
> clients the plan documents as *meant* to diverge (§4.0's `PlayPrompt::blind`), and the desktop
> repository has no access to the terminal at run time — not a submodule, not in CI — so it could
> never have been a gate.
>
> **Two facts about the stream cost a failing run each, and are worth knowing before writing
> anything that walks it.** A completed trick is still standing at `onRoundScored`, because the
> trick is not cleared until the *next* round's deal. And on the **final** round the index does not
> advance, so `onRoundComplete` and `onGameOver` publish that same round number *after* the scores
> are committed — making its last frame the only one in the game reporting `roundScore == 0` after
> a round was scored rather than before.

**Done when:** a seeded bot game logs start to finish; the window stays responsive throughout;
closing it mid-game exits through **`onGameStopped()`** with no leaked thread; and the transcript
matches.

**If you see `GameAbandoned` on that close, `pace()` is throwing and §6 has been implemented
backwards.** That is the single most likely way to get this phase subtly wrong.

**Keep the `QTextEdit`.** Put it behind a `View ▸ Log` toggle in phase B rather than deleting it. It
is the diagnostic you will want every time something looks wrong on the table later.

---

## Phase B — cards on a table

**Goal.** The same spectated game, watchable. No interaction, no engine changes.

**Read first:** §§14, 18. §7's card-art paragraph for the filename rules.

### B1 — the two seams, and one card

Nothing in B1 touches the engine, which makes it the thing to work on when you are blocked on
something else.

- [x] `src/paint/Theme.{h,cpp}` — palette, `background()`, `avatar()`, `cardHeightFor()` (§18.1).
      Light and dark from the start; retrofitting a palette is the expensive version.
- [x] `src/paint/CardRenderer.{h,cpp}` — `face()` and `back()`, rasterizing the SVG set through
      `QSvgRenderer`, cached on `(card, size, state, dpr)`. **The two `switch`es over rank and suit
      live here and nowhere else**, and `Card::toString()` is not the filename (§7).
- [x] A scratch widget drawing one card at three sizes. Check it on a HiDPI display now, not later.
      Kept, as `View ▸ Card art…`, on A5's precedent rather than deleted.

### What the B1 run found

Kept for the same reason phase 0's list is: the next person to build on these two seams hits all
four, and two of them cost a failing build each.

- **There is no card back, no table background and no avatar art.** `resources/images/cards/` is 52
  faces as PNG and the same 52 as SVG, and nothing else — so `CardRenderer::back()`,
  `Theme::background()` and `Theme::avatar()` are all painted rather than loaded. That agrees with
  §18 (a desaturated green, "no texture bitmap"; "stick figures today, portraits tomorrow") and is
  cheap to reverse: each is one private helper behind a signature that already takes the size an
  image would be scaled to *and the ratio to scale it at*, so committing real art later is three
  function bodies and some `<file>` lines, with no caller and no layout touched. The ratio is part
  of that only because B1's own review put it there — the two `Theme` signatures shipped without it
  and would have had to change on the day the art arrived, which is the one thing the seam exists
  to prevent. **Placeholder art now would be work done twice** —
  and the avatar box and vignette cannot be judged until B2 puts a table on screen.
- **The card art reaches the executable only, and `tests/CMakeLists.txt` needs its own
  `qt_add_resources` on the same `.qrc`.** Resources belong to a binary, not to the `desktop_core`
  both binaries link. Two tidier arrangements were tried and both are wrong: attaching the parent's
  generated file to `desktop_tests` **builds fine under `cmake --build build` and then fails under
  `cmake --build build --target desktop_tests`** with *"No rule to make target `qrc_cards.cpp`"* —
  the rule that generates it belongs to the scope that declared it — and that by-target form is
  what every sanitizer run in `LOCAL_VERIFICATION.md` uses. Moving the resources into
  `desktop_core` is the other, and the root `CMakeLists.txt` already refuses it: a static library's
  `qrc_*.cpp` is a translation unit nothing references, which a linker may drop. The `.qrc` is
  consumed twice, and ~30 MB of generated source compiles twice with it.
- **`QPixmap` is fatal without a `QGuiApplication`, so `tests/TestApp.cpp` is no longer a
  `QCoreApplication`.** Its old comment argued a GUI application "would need a display that CI does
  not have", which was the right instinct and the wrong conclusion — `offscreen` is exactly the
  display-free platform, it ships inside qtbase rather than being one of the plugins the deploy step
  trims, and it needs no xvfb. It is set with `qputenv` from inside `TestApp` and only when unset,
  because the suite is run both through `ctest` and as a bare `./tests/desktop_tests`, and a ctest
  `ENVIRONMENT` property would have covered only the first.
- **The `QPixmap(":/cards/x.svg")` trap is invisible while developing, which none of the plan says.**
  §18.1 is right that it returns null in an installed build, but the `qsvg` imageformat plugin is
  present in a normal Qt install — so loading a card the obvious way *works on the machine it is
  written on* and produces blank cards only once deployed. **No test can catch it**: the suite would
  have to assert that a legitimately installed plugin is absent. `CardRenderer` being the only
  caller is the entire defence, which is a stronger argument for the seam than the one §18.1 gives.

### B2 — the scene

- [ ] `CardItem`, `SeatItem`, `TrickItem`, `HandItem`, `TableScene`, `TableView`.
- [ ] Seat layout from §14's angle table, **in fractions of the view rect**. Computed, never read
      off the artwork — that is what keeps the background replaceable.
- [ ] `InfoStrip` and a static `ScoreboardWidget`.
- [ ] The hand fan drawn from `GameSnapshot::hand`, **disabled**. It is not clickable until phase C
      and it cannot be (§4.0).

- [ ] **Re-ask the one pacing question A5 left open.** The two carried from A3 are settled — the
      round dwell moved to `onRoundScored()`, and `onCardRequested()` gained a bot beat — both
      measured off A5's timestamped log rather than guessed at. What A5 could **not** judge is the
      beat it *removed*: `onCardPlayed()` no longer paces, which diverges from the terminal, and the
      argument for dropping it was that the two frames are identical **in a log**. On a table they
      are not — the card landing and the highlight moving to the next seat become separate states,
      and collapsing them may read as a card appearing under a highlight that has already left.
      Watch a trick before deciding, and read the note beside `onCardPlayed()` in `GameBridge.cpp`.

**Done when:** a spectated game is watchable and legible end to end; the window resizes without the
layout breaking; the round result gets a beat you can actually read and no card lands on top of
another; and the winning-card highlight always agrees with the trick the engine awards — because it
comes from `isWinning` and is never recomputed.

---

## Phase C — the human plays

**Goal.** A full game, playable.

**Read first:** §§4.2, 4.4, 15, 17.

- [ ] **C1 — the seat.** `prepare()` with a real `humanSeat`; the human seat's `moveProvider` filled
      with a `SeatProvider` in `start()`. The bots keep `AiMoveProvider`.
- [ ] **C2 — bidding.** `BidStrip` as a **panel, not a `QDialog`** (§4.4 — this is a shutdown
      decision, and C4 is the test it exists for). One button per bid, `forbiddenBet` disabled with
      the explanation underneath.
- [ ] **C3 — playing.** `PlayPrompt` into the hand: legality lighting, hover, click →
      `handIndex[slot]` → `submit(promptId, …)`. Never a fan position, never a cached snapshot.
- [ ] **C4 — blind rounds.** Forehead and Hidden: `handIndex.size()` face-down backs, clickable;
      other seats' cards from `SeatView::hand` (§17).
- [ ] **C5 — shutdown while parked.** Close the window with the bid panel open. This is the
      `GameAbandoned` path and the only one that should produce it.

**Done when:** a full game is playable; illegal cards cannot be clicked; the forbidden bid is
disabled; the card clicked is the card played; and **the game reaches `onGameOver()` through the
Forehead and Hidden rounds that end the default schedule.**

> **Play to the very end, every time, in this phase.** With `endWithForeheadAndHidden` at its
> default the last two rounds are exactly where a snapshot-driven hand shows nothing and the engine
> parks on a card it will never be given. A run that stalls there has the prompt/snapshot split
> wrong, not the blind rendering (§4.0, §8).

---

## Phase D — the game around the game

**Read first:** §§13, 16, and §8's phase D.

### D1 — the dialog and the second game

- [ ] `NewGameDialog` (§13): live-validated names, clamped seat count, watching/playing, structure,
      seed.
- [ ] **Play again**, which is the first thing in the client that reuses a `GameController`.

> The second game is §4.3's ordering run again from the top: `stop()` and join, a fresh bridge and
> provider from `prepare()`, `start()`. **Nothing is reset in place**, and there is no reconnection
> step — the window's connections are to the controller and outlive every game. Two latches will
> bite here if they were written wrong in A — `QtMoveProvider::abandoned` (game two plays itself at
> full speed) and `GameController::stopRequested` (game two deals one round and stops) — and a
> third thing would, if `prepare()` did not purge: game one's queued signals, which arrive after
> game two has started and repaint its deal with game one's final table.

### D2 — the read-only panels

- [ ] **First:** give `roundScored()` a `RoundResult` payload (§21.3). The round sheet's `34 → 41`
      column cannot be reconstructed later — `onRoundScored()` is the only callback at which the
      per-round score can be read against the right round index. This is a change in `GameBridge`,
      not in the widget that wants it.
- [ ] `ScoreboardWidget`, `RoundSheet`, `GameOverPanel`.
- [ ] Winners from `getWinners()`, places from `Standing::place`. Never `standings.front()` — that
      bug shipped once already, in the terminal's 2.1.0.

**Done when:** two games back to back in one process, the second as playable as the first; and a
game deliberately ended in a draw announces **both** seats.

---

## Phase E — polish

Mostly independent; do it in any order. **Except one group, which is not polish:**

> **Pacing controls and the animation handshake are threading work wearing a UI hat** (§5, §15.3,
> §18.2). Bot speed, "wait for me between tricks", "skip to end of round" and the **Deal round n**
> button are all condition variables, never slots — nothing can be signalled into a thread parked in
> `run()`. The last two turn `pace()` into an *indefinite* wait, which `requestStop()` cannot reach
> and only `cancelPacing()` can. Treat this group as a small phase-A-shaped task, and do it first in
> the phase while that machinery is still fresh.

- [ ] The threading group above.
- [ ] The clock (§19) — the contrasting case: pure GUI-thread state on a `QTimer`, and it must stay
      off the snapshot.
- [ ] `QPropertyAnimation` on `CardItem::pos`, to §18.2's budget. **Animation targets go through the
      same layout function that places static items.**
- [ ] `RoundHistoryDialog` (§16.3), `PreferencesDialog` + `QSettings` (§20), dark mode, reduce
      motion, streak pips, keyboard map.

**Done when:** Part II is on screen.

---

## Phase F — ship

- [ ] `lupdate`, fill in `ro_RO.ts`, `lrelease`. Every missed `tr()` surfaces here.
- [ ] **Drop the PNG card set from `resources/cards.qrc`.** B1 settled on the SVGs (§18.1), and
      `CardRenderer` is the only thing in the client that opens a card file — so the 52 PNGs are
      4.3 MB that nothing loads, carried in **two** binaries now that the test suite consumes the
      same `.qrc`. Confirm `qrc_cards.cpp` shrinks in both build trees and that a card still draws;
      the build file's own diagnostic is unchanged. *(Noted in B1 rather than discovered here: the
      PNGs were left embedded deliberately, because dropping them is a packaging decision and B1
      was not the phase to take it.)*
- [ ] Icons per platform; About box printing the client and engine versions.
- [ ] `windeployqt` / `macdeployqt` / `linuxdeploy` through the existing workflow.
- [ ] README, CHANGELOG, `v1.0.0`.

---

## When something goes wrong

Symptom first, because that is what you have.

| What you see | Almost certainly | Where |
|---|---|---|
| A signal never arrives, no crash, game appears to hang | An unregistered metatype — Qt warns to stderr and drops it | §5 |
| Window freezes the moment it is your turn | The bid prompt is a modal `QDialog`, or something was connected *into* the game thread | §4.4, §5 |
| Closing the window hangs on `thread.wait()` | An indefinite `pace()` that `cancelPacing()` does not reach | §15.3, §6 |
| Closing mid-spectator-game throws `GameAbandoned` | `pace()` throws instead of returning — §6 implemented backwards | §6 |
| A close is silently ignored, game plays itself out | `stopRequested` not latched, or not read back under `engineMutex` | §4.3 |
| The card played is not the card clicked | A fan position was submitted instead of `handIndex[slot]` | §4.4 |
| "It played a card I did not click" | No `promptId` gate; a stray click armed the next prompt | §4.2 |
| Nothing to click in the last two rounds | The hand is being driven by the snapshot instead of `PlayPrompt` | §4.0, §17 |
| Game two plays itself at full speed | `QtMoveProvider::abandoned` latched and reused | §8, phase D |
| Game two deals one round and stops | `prepare()` did not clear `GameController::stopRequested` | §4.3 |
| The clock jumps instead of ticking | `elapsed` is riding on the snapshot | §19 |
| The trick shows the wrong names on the last trick of a round | Seats recomputed from the leader instead of read off `PlayedCardView::seatIndex` | §14 |
| Cards soften on a HiDPI display | The pixmap cache is not keyed on `dpr` | §18.1 |
| The table or an avatar is soft while the cards beside it are crisp | `Theme::background()`/`avatar()` were handed a logical size and never told the ratio — both halves (rasterize at `size * dpr`, then `setDevicePixelRatio`) have to be on one path | §18.1 |
| A use-after-free at shutdown, only on one machine | Member declaration order — the engine must be declared last | §21.2 |

---

## What can be done out of order

Almost nothing, with two useful exceptions:

- **B1 (`Theme` + `CardRenderer`) has no dependency on the engine or the thread.** It can be built
  at any point from phase 0 onward, and it is the right thing to pick up when you do not feel like
  threading.
- **A1 (`GameSnapshot`, `HandLayout`, `refreshFromEngine`) needs no `QThread`** and is testable
  against a real engine on the test's own thread.

Everything else depends on the phase before it, and the dependencies are real rather than
organisational: C cannot be tested without B's hand on screen, D's second game exercises A's
latches, and E's pacing controls are A's condition variables with a widget attached.
