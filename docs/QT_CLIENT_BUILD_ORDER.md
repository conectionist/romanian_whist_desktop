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
| **B** ✅ | Cards on a table | A spectated game is watchable and legible |
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

- [x] `CardItem`, `SeatItem`, `TrickItem`, `TableScene`, `TableView`. **`HandItem` is not here** —
      see the hand-fan box below, which is the finding that moved it.
- [x] Seat layout from §14's angle table, **in fractions of the view rect**. Computed, never read
      off the artwork — that is what keeps the background replaceable.
- [x] `InfoStrip` and a static `ScoreboardWidget`.
- [ ] ~~The hand fan drawn from `GameSnapshot::hand`, **disabled**.~~ **MOVED TO C3, AND IT COULD
      NEVER HAVE BEEN DONE HERE.** In a spectator game `GameSnapshot::hand` is empty on every frame:
      `Snapshot.cpp` returns early when `humanSeat < 0`, and gates every `seats[i].hand` the same
      way, saying outright that *"a spectator sees everything" stays a client policy nothing here
      implements*. §3 forbids a widget filling the gap from the engine itself.
      **And `humanSeat` is not a viewer flag.** `GameController::prepare()` builds a
      `QtMoveProvider` from it and `start()` then asserts the seat's provider is null before
      replacing it with a `SeatProvider` — so seating a viewer to populate the hand either aborts in
      Debug or, in Release, swaps a bot for a forwarder that parks forever waiting for a click phase
      C has not built. This phase's own goal is *"no interaction, no engine changes"*, and a phase
      defined as spectator-only cannot draw a hand because a spectator has none. C1 seats a human for
      real, and the fan is one snapshot field away from there.

- [x] **The one pacing question A5 left open, asked on a table and answered: `onCardPlayed()` stays
      unpaced.** Measured, not judged by feel. Without the beat the middle state — card down, its
      player still ringed — lasts **one millisecond**, so the card landing and the ring moving are a
      single visual event; with it that becomes a real 250 ms state and every card costs 500 ms
      instead of 250, which is half the speed of a 26-round spectator game.
      **What settles it is the thing a table has and a log does not: a card lands at its owner's
      seat and stays there for the whole trick**, so who played it is carried by position rather
      than by wherever the ring happens to be at the instant it appears. The worry this step
      recorded — "a card appearing under a highlight that has already left" — cannot happen on the
      drawn table, because the ring is on a plaque at the edge and the card is in the middle. The
      terminal keeps its beat for a reason that does not port: its only cue for who played what is a
      line of text that scrolls away. The full reasoning is beside `onCardPlayed()` in
      `GameBridge.cpp`.

### What the B2 run found

Three things, in the pattern phase 0 and B1 set. The fourth — that the hand fan belongs to C — is in
its own box above, because it changed what this phase is.

- **A ring laid along the seat directions bunches at six seats.** The trick was first placed a fixed
  fraction of the way from the middle toward each owner, which is what §14's wording suggests; on a
  window wider than it is tall the seat ellipse crowds adjacent directions toward the horizontal and
  two cards land in one place. The trick has a ring of its own now — the same ordering function over
  a circle whose radius comes from `2·r·sin(π/n)` inverted, i.e. from the card and the seat count
  and not from the window. **Sizing that ring off a constant multiple of the card height is not
  enough either**: a card tilted six degrees is 1.067 card heights tall, so the obvious constant
  left a three-pixel gap that reads as touching. It is measured off the item that does the rotating.
- **A centre inside a rect is not a plaque inside it.** `seatlayout` places centres, and at the two
  side seats the plaque hangs half its width further — off the edge of the window. The scene insets
  by half a plaque before laying out, which keeps the layout free of any knowledge of what is drawn
  at the points it returns.
- **Theme's card height is derived from the hand fan and is the wrong constraint at six seats**,
  where the same card must also pass between two plaques a side. `TableScene::fitsOnTable()` builds
  both rings and compares rectangles, and the scene shrinks from Theme's answer only when they
  collide — rather than re-tuning the one unit for the worst case and leaving every four-seat table
  smaller than it needs to be.

And one from the panels: **a column sized in digit widths is sized for its numbers and not for the
word above them.** "Round" over a four-digit column drew straight across "Won" beside it. Widths are
measured from the headings actually being drawn, which is what keeps a Romanian build from finding
the same defect again.

### What the review of the panels found

Four more, all of them measurement rather than wording — and the reason they are worth a section of
their own is that **every one is a widget measuring itself with something other than what it later
draws with.** That is the shape to look for in the panels phase C and D add.

- **A widget whose size depends on the round moves everything laid out beneath it.** The eight-card
  round has no trump, and hiding the trump label took the info strip from 77 pixels to 29 — so the
  table and the scoreboard jumped some fifty pixels on entering that round and back on leaving it,
  twice in every game. The slot is reserved for the whole game now and holds nothing in that round,
  with "No trump" beside it saying why. The strip is also full height before the first snapshot
  arrives, so the first frame no longer moves anything either.
- **A shared function fed different inputs is the same defect as two copies of it, wearing a tidier
  shape.** `columnsFor()` was already the one place the column widths are worked out — B2b's own
  finding — and it was still being handed tabular-figure metrics when painting and the plain widget
  font when measuring. Where the two differ in digit width the painted columns overrun the width the
  hint reserved, and the name column, which is whatever is left after them, is what pays. There is
  one `numberFont()` now and all three callers measure in it.
- **A name measured in one weight and drawn in another elides at the panel's own preferred width.**
  The human's row is bold, bold is wider, and `sizeHint()` measured every name plain — so a long
  human name was cut off at a width the panel had just asked for. Names are measured in the weight
  they are drawn in.
- **The one column with no heading was the one column with no padding.** Every other width picks the
  padding up from the lambda that measures a heading; the streak pips have no heading to measure, so
  they never got it, and the Total digits sat two pixels from the leftmost pip — which renders as
  touching.

The two with a right answer no eye is needed for — the strip's height, and the name width — are
regression tests in `InfoStripTests.cpp` and `ScoreboardTests.cpp`. Both construct a real widget,
which the suite can do because `TestApp` is a `QApplication` on the offscreen platform. The other
two are a constant and a structure, and are asserted by neither.

**A fifth was deferred rather than fixed** — the trump card going stale when the window changes
screen or font — and it is the entry in `KNOWN_ISSUES.md`, which exists because of it. The fix needs
a way past a change guard that is there for a good reason, and phase E owns the rest of §18.1's
device-pixel-ratio work anyway.

**Done when:** a spectated game is watchable and legible end to end — which for a *spectated* game
means no hand at the bottom, per the box above; the window resizes without the
layout breaking; the round result gets a beat you can actually read and no card lands on top of
another; and the winning-card highlight always agrees with the trick the engine awards — because it
comes from `isWinning` and is never recomputed.

---

## Phase C — the human plays

**Goal.** A full game, playable.

**Read first:** §§4.2, 4.4, 15, 17.

- [x] **C1 — the seat.** `prepare()` with a real `humanSeat`; the human seat's `moveProvider` filled
      with a `SeatProvider` in `start()`. The bots keep `AiMoveProvider`.
- [x] **…and the prompt seam under it, answered by a stand-in.** Not in the step as written, and the
      reason for it is the reason this document has an order at all — see the box below. C2 deletes
      half of `src/ui/PromptStandIn.{h,cpp}` and C3 the rest.
- [x] **C2 — bidding.** `BidStrip` as a **panel, not a `QDialog`** (§4.4 — this is a shutdown
      decision, and C5 is the test it exists for). One button per bid, `forbiddenBet` disabled with
      the explanation underneath. **Stop calling `promptstandin::chooseBet()`** — the bet half of
      `MainWindow::onBetRequested()` becomes the panel; the logging and the null guard stay.
      *(`chooseBet()` moved to `fixtures::lowestLegalBid()` rather than staying in `src/` as
      production code only tests call.)*
- [x] **C3 — playing.** `HandItem` itself, which B2 could not build — the fan from
      `GameSnapshot::hand` between turns, drawn disabled, and the bottom strip the table reserves for
      it. Then `PlayPrompt` into the hand: legality lighting, hover, click →
      `handIndex[slot]` → `submit(promptId, …)`. Never a fan position, never a cached snapshot.
      The stand-in is gone; `chooseSlot()` went to `fixtures::firstLegalSlot()` beside C2's
      `lowestLegalBid()`. **§12's prompt bar landed here too** — it is what explains a dimmed fan,
      which is a question only this step raises.
- [x] **…and C4's clickable backs, which C3 could not leave behind.** See the box below.
- [x] **C4 — blind rounds.** ~~`handIndex.size()` face-down backs, clickable~~ — done in C3, which
      could not delete the stand-in without them. This is the other half: **other seats' cards from
      `SeatView::hand`** in a Forehead round (§17), face-up on their plaques, in the slot the
      cards-left stack occupies — so **`plaqueSize()` does not change** and the table holds still
      between rounds. §17's accent-coloured round type landed with it. **Hidden needed no code at
      all**: see below.
- [ ] **C5 — shutdown while parked.** Close the window with the bid panel open. This is the
      `GameAbandoned` path and the only one that should produce it. **Both halves are already
      standing as of C2** — `GameControllerTests`' "A stop while the game is parked on a prompt ends
      it", and the manual close, measured at 39 ms with the strip up. What C5 is left to add is the
      same close with C3's *hand* waiting, which parks in `playCard()` rather than `makeBet()`.

**Done when:** a full game is playable; illegal cards cannot be clicked; the forbidden bid is
disabled; the card clicked is the card played; and **the game reaches `onGameOver()` through the
Forehead and Hidden rounds that end the default schedule.**

> **Play to the very end, every time, in this phase.** With `endWithForeheadAndHidden` at its
> default the last two rounds are exactly where a snapshot-driven hand shows nothing and the engine
> parks on a card it will never be given. A run that stalls there has the prompt/snapshot split
> wrong, not the blind rendering (§4.0, §8).

### Why C1 landed the whole prompt seam, and not only the seat

**The step as written is two lines of `MainWindow` and it ends in a game that parks forever.**
`GameController` already took a real `humanSeat`, already built a `QtMoveProvider` from it and
already filled that seat with a `SeatProvider` in `start()`; the bridge already skipped the human's
thinking-beat and already stamped `humanSeat` onto the snapshot before the refresh. All of it was
written in A2 and A4 and tested there. The only thing missing was the caller — `MainWindow` passed
`std::nullopt`. Change that and the engine deals round one, asks the human for a bid, and stops,
because C2 and C3 are what answer.

That is legal by this document's own rule — a *phase* ends in something runnable, a *step* is
roughly a commit — so C1 could have ended parked. It does not, for the rule the ordering actually
exists for: **one plausible cause.** C3 and C4 are otherwise the first moment anyone learns whether
the prompt/snapshot split works, and a stall in the Hidden round would then have two explanations —
the hand widget drew nothing, or the prompt never arrived. Answering the prompts before any widget
exists removes one of them. It is the argument that puts A before B, applied inside C.

So C1 answers its own prompts: the lowest legal bid, the first legal card, submitted through
`handIndex[slot]` exactly as C3's click handler will. **A full game now plays to `onGameOver()`
through the Forehead and Hidden rounds**, which is phase C's own "done when" — reached in the step
with the least UI standing on top of it rather than the most.

**What it is not** is a bot. It picks the first thing it is allowed to pick, and the engine already
has strategies for anything better; a stand-in that played well would be one somebody kept.

### What the C1 run found

- **`GameController` needed no change at all, and that is worth knowing before opening it.** A4
  built the seat-filling and A2 the provider, both against tests, and both were correct on the first
  seated game. The whole of the change is in `MainWindow` — the seat it prepares with, and the seat it
  leaves null in the setup. If something is wrong at this step, look at the window before the
  controller.
- **Nothing yet proved the POSITIVE path through `start()`.** Every controller test was a spectator
  game or a setup the engine was meant to reject by name — so "the seat is filled and the game then
  finishes" was untested, and it is the assertion a broken `prepare(Seat)` would fail. It is
  `HumanSeatTests`' first case, on a real thread, and the rest of that file runs the engine
  synchronously the way A6 does, because which prompt carried what is not a threading question.
- **A stand-in that answers wrongly HANGS the suite rather than failing it, and the ceiling that
  fixes it was found by mutation.** `makeBet()` and `playCard()` both re-prompt rather than pass an
  illegal answer on, which is right — but it means an answerer that keeps giving the same wrong one
  is asked again forever, the game never ends, and every assertion after it is unreachable. Mutating
  `chooseSlot()` to ignore legality did not fail `HumanSeatTests` — the run had to be killed, and
  printed nothing at all. `kPromptCeiling` now abandons the provider past a bound no real game reaches, and the same
  mutation reports `!transcript.looped` on every case. **This is the shape to expect from any future
  test that answers a prompt**, and `GameControllerTests` already states the rule it breaks: nothing
  waits without a deadline.
- **A whole-game count can police a rule it never exercises.** `getForbiddenBet()` is engaged for the
  final bidder only, so mutating `chooseBet()` to ignore `forbiddenBet` was caught by the unit test
  and *not* by the prompt-count assertion the count exists for — the human simply never bid last with
  a forbidden value in that seeded game. The count now carries an `any_of` proving at least one
  prompt arrived with one. Worth looking for wherever a case asserts over a whole game: the assertion
  can be right, and reached, and still be about nothing.
- **Seating seat 0 would have hidden the whole feature.** `seatlayout` puts seat 0 at bottom-centre
  when `humanSeat` is `-1`, so a client that dropped the human entirely draws a table identical to a
  spectator's. `kHumanSeat` is 1 in the window and in the tests for that reason, which makes "the
  bottom plaque reads Bogdan (you)" a real assertion rather than a coincidence.
- **Bots got slower, correctly.** `Pacing{}` is unchanged, but `botBeat()` picks `beat` (700 ms) over
  `spectatorBeat` (250 ms) the moment a seat is filled — §4.1's split, which phases A and B only ever
  saw one side of.
- **The window title still said "spectating", and only running it found that.** It lives in
  `MainWindow.ui` and had been right since phase A. Nothing compiles differently when it goes false,
  no test reads it, and it is the first thing on screen. It is plain "Romanian Whist" now rather than
  "playing", because phase D's dialog offers both and a title naming either is one more thing to keep
  in step with the game actually running. **Worth a look every phase that changes what the client
  is** — the .ui holds several strings of that kind and none of them is under test.
- **The relay `MainWindow` depends on entirely could be deleted with the suite green** — found by
  review, confirmed by deleting it. `prepare()` relays `betRequested` and `cardRequested` from the
  bridge to the controller, and the window connects to nothing else; remove either `connect()` and
  the human is never asked, the game parks forever, and **137 tests still passed**. The first
  `HumanSeatTests` case looked like cover and was not: it answers DIRECT on the bridge, on the game
  thread, so nothing crosses the relay. A seated game answered from the *test* thread now pins both.
  **The shape to look for is a test that connects to the bridge where the window connects to the
  controller** — the two are one hop apart and only one of them is what ships.
- **A seated game cannot use `Outcome::await()`, and the reason generalises.** The game thread parks
  inside the prompt waiting for an answer only the GUI thread can give, and the GUI thread can only
  give it by running its event loop — so a blocking wait on the condition variable holds the thread
  that has to do the unblocking, and the test times out instead of passing. Every other controller
  test can `await()` because a bot game needs nothing from the main thread. **Anything answering a
  prompt has to pump, not wait**, which is the same asymmetry `kPromptCeiling` exists for from the
  other direction.
- **The two `qRegisterMetaType` calls for the prompts cannot be guarded by a test, and now say so.**
  Removing them *and* both `Q_DECLARE_METATYPE` lines leaves the queued relay working and every test
  passing: Qt 6 builds a `QMetaType` from any complete type. So the registrations are belt-and-braces
  for a build where auto-registration does not reach, the defence is that all four declarations sit
  in two named places, and a test written to pin them would be asserting that a working mechanism
  does not work. Same shape as B1's `QPixmap(":/cards/x.svg")` trap — recorded in `MetaTypes.cpp`
  where someone would otherwise delete them as dead.
- **TSan is not silent on a clean tree**, which is worth knowing before reading a report as a
  verdict: `master` produces ~83 warnings and passes every test, all of them bottoming out in Qt's
  own uninstrumented libraries. C1 adds none — measured against a `master` worktree rather than
  assumed. The method, and what a *real* finding would look like, is now a section in
  `LOCAL_VERIFICATION.md`.


### What the C2 run found

- **The strip landed on top of the player's own name, and only running it showed that.** §15.1 says
  the strip "rises over the bottom of the table", and the bottom of the table is exactly where
  `seatlayout` puts the **seated player's plaque**. An overlay pinned to the viewport's bottom edge
  therefore draws straight through it. The fix is that the scene answers where an overlay may go —
  `TableScene::overlayBand()` — rather than the view measuring it off the viewport. **The general
  rule: a widget positioned against the window is positioned against the wrong thing whenever the
  scene has something of its own down there.**
- **And there is no free band to find.** Measured on a 1000×700 table: the trick's cards end 565 px
  down and the bottom plaque starts at 578, so the gap between them is **thirteen pixels**. The
  strip reaches *up over the middle of the table* instead, which is sound only because of *when* it
  is shown — bidding finishes before the round's first card is played, so the trick area is empty
  the whole time it is up. **C3 has to re-ask this**, because a hand is on screen while cards are
  down and cannot borrow the middle.
- **`xdotool windowclose` does not test a close.** It calls `XDestroyWindow`, so the window vanishes
  and Qt never delivers a `QCloseEvent` — `closeEvent()` never runs, `stop()` is never called, and
  the process is left alive with the game thread still parked. That reads *exactly* like the C5 hang
  it is supposed to be checking for, and it cost a false report. **Close with `wmctrl -ic`**, which
  sends `_NET_CLOSE_WINDOW`; the real close then exits in 39 ms.
- **A widget test cannot click a button through the window manager either.** A synthetic
  `xdotool click` at the right coordinates does nothing until the window is activated, which also
  looks like a dead button. The keyboard path (`xdotool key`) works once the window is active and is
  the cheaper check — and it exercises §15.1's digit binding at the same time.
- **The strip's size changes with the round**, so geometry set once is right for one round and wrong
  for the rest — nine buttons in an eight-card round, two in a one-card round, and the explanation
  only for the final bidder. `TableView` watches it for `LayoutRequest` and `Show` rather than
  placing it once, because a window nobody resizes would never correct itself.


### What the C4 run found

- **Hidden rounds were already finished, and the way to establish that was to look.** §17 wants every
  card face-down there, and that is what the table already showed: `canSeeHand()` is nobody, so every
  `seats[i].hand` is empty, every plaque keeps its cards-left stack — which *is* the face-down card —
  and your own card is C3's back in the fan. **A step whose plan says "build nothing here" is worth
  more than one that builds something**, and the offscreen render is what turned that from a claim
  into a fact.
- **A mutation that crashes proves nothing.** Making `showsFace()` always true made `refreshFace()`
  read `hand.front()` off an empty vector, so the run died instead of failing and the probe said
  nothing about the test. The guard now checks the thing it is about to dereference rather than a
  rect derived from it — belt and braces on a real crash — and only then was the mutation a fair
  question to ask.
- **And the answer was that the test did not cover it.** With the crash closed, `showsFace()` always
  true *passed*: `refreshFace()` re-checks the hand, so no card is drawn either way. What that flag
  really decides is the **layout** — a plaque wrongly believing it holds a card narrows its text
  column and drops its stack, in twenty-four rounds of twenty-six, drawing no card at all. `SeatItem`
  exposes its own decision now, and both directions of it fail.
- **`SeatItem` had no test file until this step** — the last item on the table without one, and
  B2b's four findings were every one of them in a painted widget of exactly this shape.
- **The table holding still was checked with pixels, not eyes.** The two rounds render to images whose
  plaque *name* areas are byte-identical, and whose only differences are inside the card slots. That
  is `plaqueSize()` being a static of the card height alone, which is the property the whole
  placement decision was chosen to keep.
- **Editing `tests/CMakeLists.txt` while a background sanitizer build was in flight destroyed its
  build tree.** The run came back with an empty error count rather than a failure, which reads like a
  pass at a glance. **Do not touch the build inputs while a background build is running** — and
  read the count, never the absence of one.

### Why C3 took C4's backs, and where the hand went

**Deleting the stand-in without them stalls the last two rounds.** In Forehead and Hidden a play
prompt's `entries` are empty and `handIndex.size()` face-down backs are all there is to draw — which
this document assigned to C4. But C3 is what removes `chooseSlot()`, so a C3 that stopped at faces
would end in a client that cannot finish a game, and every run would have to be abandoned at round
25. `CardItem::setBack()` already existed, so the backs are a branch in `rebuild()` and nothing more.
C4 keeps the half that is genuinely separate work.

**And the hand could not do what the bid strip did.** C2 floated its strip over the middle of the
table because bidding finishes before the round's first card is played. A hand cannot: cards are on
the table while you are choosing one. §14 had the answer already — *"a spectated game has no human…
and the bottom hand area is not drawn at all"* — and `SeatLayout.h` had been describing the mechanism
since B2: *"no bottom strip is reserved for a hand, and that is the caller's business: it passes a
shorter rect."* `TableScene::tableRect()` is that caller, and every other measurement on the table
now comes off it.

### What the C3 run found

- **A test that drives a private hit test through a public shortcut tests neither.** The case for
  "the slot under an overlap is the upper card" called `activate(1)` directly — which asserts only
  that the signal carries what it was handed, whichever way `slotAt()` walks. Mutating the walk to go
  forwards **passed**. It sends a real `QGraphicsSceneMouseEvent` through the scene now, and the same
  mutation fails it. **The shape to look for: a case whose name is about geometry but whose body
  never touches any.**
- **The lift constant was in two files within an hour of being written.** `TableScene` reserved
  `card * 1.12` and `HandItem` lifted by `0.12`, and nothing would have caught them drifting apart
  except a hovered card leaving the strip. `HandItem::liftFraction()` is the one copy now — the same
  argument `TrickItem::cardFootprint()` makes for the tilt.
- **Centring the fan in its strip puts half the lift room where the lift never goes.** The hover only
  rises, so a centred card had 7 px of headroom against a 14 px lift and went that far into the
  plaque above. The fan is bottom-aligned in the strip, one margin up, and the whole reserve is where
  the lift will use it. **Measured, not judged: `sceneBoundingRect()` against the scene rect.**
- **`slots` is a Qt macro.** `std::vector<Slot> slots;` expands to `std::vector<Slot> ;` and the
  compiler says *"declaration does not declare anything"* against a line that looks perfectly
  correct. The member is `fan`.
- **Driving the running client with `xdotool` sent keystrokes into another application** once the app
  had exited under its own watchdog — the loop kept typing and the keys landed on whatever had focus.
  Global `xdotool key` has no target; `import -window` on a dead window grabs something else. **The
  by-eye pass on a phase this interactive belongs to a person**, and this is the phase that settled
  that. The offscreen render — real widgets, real scene, no window — covers layout and legality
  without touching the desktop at all, and is what the earlier findings above were measured on.

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
- [ ] **Before it, read the box below.** The hand's prompt/snapshot split has a latent hang in it that
      only this phase can wake up.
- [ ] The clock (§19) — the contrasting case: pure GUI-thread state on a `QTimer`, and it must stay
      off the snapshot.
- [ ] `QPropertyAnimation` on `CardItem::pos`, to §18.2's budget. **Animation targets go through the
      same layout function that places static items.**
- [ ] `RoundHistoryDialog` (§16.3), `PreferencesDialog` + `QSettings` (§20), dark mode, reduce
      motion, streak pips, keyboard map.
- [ ] **`InfoStrip` re-rasterizes on a screen or font change** — the one entry in
      `KNOWN_ISSUES.md`, deferred out of B2b and belonging here because it is §18.1's
      device-pixel-ratio work rather than anything about the strip.


> **A snapshot published while a prompt is live re-opens C3's hang, and this is the phase that could
> publish one.** C3 shipped a defect where `TableScene::relayout()` re-sourced the fan from the
> snapshot mid-turn — in a blind round that empties the fan, leaving nothing to click and an engine
> parked forever. The fix was `HandItem` holding which of its two sources is on screen, and the
> prompt outranking the snapshot while it is live.
>
> **That fix has one seam left.** `setPlayable(false)` both disables the fan *and* ends the prompt's
> claim on it, and `MainWindow::render()` calls it unconditionally on every snapshot. So a snapshot
> arriving mid-prompt still leaves the fan standing but **unplayable** — the same hang through a
> different door. Measured, not supposed: set a blind prompt, arm it, then make `render()`'s two
> calls in its order, and `isPlayable(0)` comes back false.
>
> **It is unreachable today**, and the reason is worth knowing before anything here changes it:
> `GameBridge::onCardRequested()` calls `publish()` *before* the provider raises the prompt, both
> cross the boundary queued in that order, and the game thread is blocked in the provider for the
> whole wait — so `render()` always runs *before* `onCardRequested()` and never during it.
>
> **What would break that is a publish that does not come from the parked game thread.** Anything in
> this phase that repaints the table on its own schedule is the candidate — and note the clock is
> explicitly *not* one, because §19 keeps `elapsed` off the snapshot and on a `QTimer`, which is a
> second reason for that rule beyond the stutter it was written for.
>
> **The hardening, if you want it before the fact:** let `setHand()` set `playable = false` when it
> *accepts* a hand, and drop the separate call from `render()`. Then "the snapshot owns the fan" and
> "the fan is not clickable" are one fact rather than two that a caller can set apart — which is what
> §4.4's "DISABLED, always" says anyway.

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
| A seated game never ends and the log stops moving, with no hang in the window | An answer the provider will not accept. `makeBet()` and `playCard()` re-prompt rather than pass an illegal one on, so a widget that keeps sending it is asked again forever | §4.2 |
| The bid strip draws over your own seat plaque | It was placed against the viewport instead of `TableScene::overlayBand()` — the bottom of the table is where the seated player is drawn | §15.1 |
| The card played is not the card pointed at, only in the overlaps | The fan's hit test walks forwards. Cards overlap and later ones are on top, so the slot is the LAST box containing the point | §14 |
| A hovered card rises into your own plaque | The fan is centred in its strip rather than bottom-aligned, so half the lift reserve sits below the card | §15.2 |
| Your own card is face-up on your own plaque in a Forehead round | Something other than `SeatView::hand` decided to draw it. A non-empty hand IS `canSeeHand()`'s permission, and your own is empty there | §17 |
| The table shifts when a Forehead round starts | `plaqueSize()` stopped being a function of the card height alone. It feeds `seatRingRect()` and `fitsOnTable()` | §14, C4's findings |
| The game stops on your turn in a Forehead or Hidden round, after a resize | The fan was re-sourced from the snapshot mid-prompt, and a blind round's snapshot hand is empty. `HandItem` refuses that while a prompt is live | §17, phase E's box |
| Closing the window seems to leave the process running, tested with a script | `xdotool windowclose` destroys the window without a `QCloseEvent`, so `closeEvent()` never runs. Use `wmctrl -ic` | C2's findings |
| Nothing to click in the last two rounds | The hand is being driven by the snapshot instead of `PlayPrompt` | §4.0, §17 |
| Game two plays itself at full speed | `QtMoveProvider::abandoned` latched and reused | §8, phase D |
| Game two deals one round and stops | `prepare()` did not clear `GameController::stopRequested` | §4.3 |
| The clock jumps instead of ticking | `elapsed` is riding on the snapshot | §19 |
| The trick shows the wrong names on the last trick of a round | Seats recomputed from the leader instead of read off `PlayedCardView::seatIndex` | §14 |
| Cards soften on a HiDPI display | The pixmap cache is not keyed on `dpr` | §18.1 |
| The trump card is soft after the window moves to another screen | Known and deferred — `InfoStrip` rasterizes only inside `refresh()`, which the change guard skips when the round has not moved | `KNOWN_ISSUES.md` |
| The table or an avatar is soft while the cards beside it are crisp | `Theme::background()`/`avatar()` were handed a logical size and never told the ratio — both halves (rasterize at `size * dpr`, then `setDevicePixelRatio`) have to be on one path | §18.1 |
| A new rank is drawn as the card back | The back shared the faces' index space (`indexOf` is `rank * 4 + suit`, the back was 52). It has its own key bit now — and note a `static_assert` **cannot** catch this: C++ cannot name "the last enumerator", so the assert has to spell `Rank::Ace`, which is still 12 after a rank is appended after it | §18.1 |
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
