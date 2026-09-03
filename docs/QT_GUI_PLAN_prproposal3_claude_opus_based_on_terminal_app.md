# A desktop GUI client, in Qt C++

A plan for `romanian_whist_qt` — a second client for the
[Romanian Whist engine](https://github.com/conectionist/romanian_whist_engine), standing beside
the terminal client the way a GUI stands beside a CLI: same rules, same engine, a different
interface and nothing more.

This document is both halves of the job. **Part I** is the design — what a player sees, screen by
screen. **Part II** is the architecture and the implementation, including the one genuinely hard
problem (the engine owns a blocking loop; Qt owns an event loop) and the shape of the answer.

Everything here is written against **engine 4.1.0**, the version this repo's submodule is pinned
to, and against the terminal client at **2.2.0** — which is the reference implementation for
every question of "what does the engine actually guarantee here?", because it has already been
wrong about most of them once.

---

## 0. Scope

**In scope.** A single-window desktop application: set up a game, play it against 1–5 bots or
watch bots play each other, see the table, bid, play cards, read the scores, finish, play again.
Linux, Windows and macOS, from one source tree.

**Out of scope, deliberately.** Network play, save/resume, undo, new AI strategies, rule
variations, animation of the deal itself, sound. Every one of those is either an engine change or
a second project; none is needed for a GUI to be worth having.

**Non-negotiable.** No game rule lives in this client. Not the bidding restriction, not who may
see whose hand, not who wins a trick, not who wins a drawn game. The terminal client's CHANGELOG
records what happens when a client decides one of those for itself (2.1.0: it announced one winner
of a two-way draw), and the GUI gets the answer from the same place: the engine.

---

# Part I — The design

## 1. The shape of the application

One main window. No MDI, no floating tool windows, no wizard-with-a-back-button. A card game is a
single focused activity and the window should read as a table, not as an IDE.

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│  Romanian Whist                                              ─  □  ✕             │
├──────────────────────────────────────────────────────────────────────────────────┤
│  Game   View   Help                                                              │
├──────────────────────────────────────────────────────────────────────────────────┤
│  Round 7/24     6 cards dealt     Normal     Trump ♠Q     Elapsed 4:12           │  ← info strip
├───────────────────────────────────────────────────────┬──────────────────────────┤
│                                                       │  SEATS                   │
│                        ┌────┐                         │  ┌────────────────────┐  │
│                        │ Bogdan  ●3 │                  │  │ #  Name   Bid Won │  │
│                        │ 4 cards    │                  │  │ ▸1 You     2   1  │  │
│                        └────┘                         │  │  2 Ana     1   2  │  │
│                                                       │  │  3 Bogdan  0   0  │  │
│      ┌────┐                          ┌────┐            │  │  4 Carmen  3   1  │  │
│      │ Ana│         ╭─────╮          │Carm│            │  │                   │  │
│      │    │      ♥K │  ♠7 │ ♦2       │    │            │  │ Round   Total  Str│  │
│      └────┘         ╰─────╯          └────┘            │  │  +7      41    ●●●│  │
│                    (the trick)                        │  │  -1      33       │  │
│                                                       │  │  +5      28    ●● │  │
│                                                       │  │  -2      19       │  │
│         ┌───┐┌───┐┌───┐┌───┐┌───┐┌───┐                 │  └────────────────────┘  │
│         │♠A ││♠9 ││♥K ││♥4 ││♦J ││♣3 │   ← your hand   │                          │
│         └───┘└───┘└───┘└───┘└───┘└───┘                 │  Bids: 6 of 6 · total 6  │
│              You  ·  bid 2, won 1                     │  for 6 tricks — someone   │
│                                                       │  is going to be wrong.    │
├───────────────────────────────────────────────────────┴──────────────────────────┤
│  You must follow ♠.                                            [ Skip to end ]    │  ← prompt bar
└──────────────────────────────────────────────────────────────────────────────────┘
```

Four regions, each with one job:

| Region | What it is | Why it is separate |
|---|---|---|
| **Info strip** | Round n/N, cards dealt, round type, trump, elapsed | The five facts that are true all round and change only between rounds. Directly mirrors the terminal header table. |
| **Table** | A `QGraphicsView` — seats, the trick, your hand | The only animated surface. Everything that moves, moves here. |
| **Scoreboard** | One row per seat: bid order, name, bid, won, round score, total, streak | The terminal's seat table, which is the thing players actually read between plays. Never animated — a number that slides is a number you cannot read. |
| **Prompt bar** | Hint, error, and the one contextual action | Where the game talks to you. One line, always in the same place, so you learn where to look. |

Menus stay thin: **Game** (New Game…, Abandon Game, Quit), **View** (Round history…, Preferences…,
Full screen), **Help** (Rules, About — the About box prints the client version and the linked
engine version, exactly as `--version` does today).

## 2. Setup

`Game ▸ New Game…` opens a single modal dialog — not a wizard. The terminal asks its questions one
per screen because a terminal has one screen; a dialog can show all of them at once, and seeing
the whole shape of the game before starting it is strictly better.

```
┌─ New Game ───────────────────────────────────────────────┐
│                                                          │
│  ( ) I'm watching        (•) I'm playing                 │
│                                                          │
│  Your name   [ Dan            ]                          │
│                                                          │
│  Opponents   [ 3  ▾ ]   (2–6 at the table)               │
│                                                          │
│    1.  [ Ana      ]   plays  [ Low risk      ▾ ]         │
│    2.  [ Bogdan   ]   plays  [ Ducking       ▾ ]         │
│    3.  [ Carmen   ]   plays  [ Random        ▾ ]         │
│                                                          │
│  ── Shape of the game ─────────────────────────────────  │
│                                                          │
│  (•) 1-8-1   counts up from one card and back down       │
│  ( ) 8-1-8   the other way round                         │
│                                                          │
│  [x] End with a forehead round and a hidden round        │
│  [ ] Play every one-card round forehead                  │
│                                                          │
│         24 rounds · about 20 minutes                     │
│                                                          │
│  ── Pace ──────────────────────────────────────────────  │
│                                                          │
│  Bot speed   Slow ──────●──────── Instant                │
│  [x] Wait for me between tricks                          │
│  [ ] Fixed deal, seed [ 12345 ]                          │
│                                                          │
│                            [ Cancel ]  [ Deal ]          │
└──────────────────────────────────────────────────────────┘
```

Design notes:

- **The opponent rows appear and disappear with the count.** Changing "Opponents" to 4 adds a
  fourth row with a pre-filled default name; it does not open another dialog.
- **The count is bounded by the same rule the wizard enforces**: 2–6 seats total, so watching
  offers 2–6 opponents and playing offers 1–5. This is a hard engine limit (`GameEngine::start()`
  throws outside it) and the deck is built from the player count, so the spinner is clamped rather
  than validated after the fact.
- **Names are validated as you type**, not on OK: the engine rejects an empty name and rejects two
  names equal byte for byte, and it compares exactly — no trimming, no case folding. The dialog
  marks the offending field and disables **Deal** rather than letting `start()` throw.
- **"24 rounds · about 20 minutes"** updates live from the four shape controls. The round count is
  a pure function of the player count and the structure, so it can be computed before the engine
  exists; the time estimate is a rule of thumb from the bot-speed slider.
- **Fixed deal** is `--seed` with a friendlier name. It stays in the GUI because it is how a
  rendering change gets reviewed as a diff, and it costs one checkbox.

The strategy names are the wizard's four, in the wizard's order: Random, First card, Low risk,
Ducking. Each carries a one-line tooltip taken from the engine's
[STRATEGIES.md](../libs/RomanianWhistEngine/docs/STRATEGIES.md), so the choice means something to
a player who has never read the source.

## 3. The table

The centrepiece, and the whole reason to build a GUI at all: the terminal renders the table as a
list of "Ana played ♥K", and a card game is spatial.

**Seats sit on an ellipse.** The human is always at bottom-centre; the others are distributed
around the remaining arc in seat order, going clockwise, so the play order on screen matches the
play order in the game. A spectated game has no human, so all seats are distributed around the
full ellipse and the bottom hand area is not drawn at all.

Each seat is a small plaque showing:

- the player's name, with **(you)** on your own;
- **bid ▸ won** as a compact pair — `2 · 1` reads as "bid two, has one";
- how many cards are left in hand, drawn as a tiny stack rather than a number;
- a **turn ring** — a soft highlight around the plaque of whoever is being asked to move.

**The trick grows in the middle.** Each card is placed just inside its owner's seat, rotated
slightly toward the centre, in the order it was played. The card currently winning the trick gets
a raised shadow and a thin accent border. That highlight is not computed here: it comes from
`GameEngine::getCurrentTrickLeader()`, which is the engine's own ranking of a partly played trick,
so the highlight can never disagree with the winner the engine eventually declares.

**The lead card is marked.** A small pip under the first card played, because "which suit are we
following" is the single most-asked question at a whist table and the answer is otherwise implicit.

**Your hand fans across the bottom.** Sorted the way the terminal sorts it — trump suit first, then
a fixed alternating-colour order, high rank first within a suit — because a hand you can read at a
glance is worth more than a hand in deal order. Cards overlap by about 40% of their width and the
fan is centred; at most eight cards ever need to fit, so no scrolling and no squeezing is required.

## 4. Bidding

When it is your turn to bid, a strip rises over the bottom of the table:

```
              How many tricks will you win?

        ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐
        │ 0 │ │ 1 │ │ 2 │ │ 3 │ │ 4 │ │ 5 │ │ 6 │
        └───┘ └───┘ └───┘ └───┘ └───┘ └───┘ └───┘
                                  ✕
     You bid last — 3 would make the bids add up to 6,
     and somebody has to be wrong.
```

- **One button per legal bid**, 0 through the trick count. Numbers, not a spinner: a spinner asks
  you to think about a range, a row of buttons asks you to pick.
- **The forbidden bid is disabled, visibly, before you reach for it** — with the explanation
  underneath. This is `GameEngine::getForbiddenBet()`, which names exactly one value and only for
  the final bidder, and lifts entirely once the bids so far already exceed the trick count. The
  client asks; it does not reimplement the arithmetic.
- **Your hand stays visible above the strip**, unnumbered and unlit. You bid on what you can see.
- **Keyboard**: the digit keys bid directly, and a disabled digit is simply ignored.

Bots bid in turn with the same pacing as their plays, their bid appearing in the scoreboard row and
briefly on their seat plaque. After the last bid, the table holds for a beat on the completed bid
table — the terminal added this deliberately, because otherwise the final bid is only ever seen
when a bot made it.

**The bid total gets a permanent home** in the scoreboard panel: *"Bids: 6 of 6 · total 6 for 6
tricks — someone is going to be wrong."* The over/under is the whole tension of the round and the
terminal only implies it.

## 5. Playing a card

- **Legal cards are lit; illegal cards are dimmed and unclickable.** Legality comes from
  `CardValidator::getLegalCards` — the engine's own rule, in priority order (follow suit, else
  trump, else anything), including the part clients get wrong: **overtrumping is not required**.
- **Hovering a legal card lifts it.** Clicking plays it. There is no confirm step; a card game with
  an "are you sure?" is a spreadsheet.
- **The prompt bar says why**, when there is a why: *"You must follow ♠."* when your hand is
  restricted, *"You lead."* when it is not. Same two hints the terminal shows, same triggers.
- **Keyboard**: ← → move a selection through the legal cards, Enter plays. Digits 1–8 select by
  position in the fan, matching the terminal's numbering for anyone switching between the two.

**A played card animates from the hand to the table** — roughly 180 ms, ease-out, with the fan
closing behind it. This is the one animation that earns its keep: it is what tells you *who* played
*what*, which is exactly the information the terminal has to spell out in words.

## 6. The end of a trick

The winning card lifts and holds; the prompt bar says *"Ana wins the trick."*; the completed trick
sits on the table for a moment and then sweeps toward the winner's seat as it clears.

Whether it waits for you or simply dwells is the **"Wait for me between tricks"** preference, which
is the GUI's version of the terminal's `--auto`. A **Skip to end of round** button in the prompt
bar runs the rest of the round out at full speed — the same escape hatch as pressing Enter mid-hand
today, and, like it, it only skips the round it was asked for.

## 7. The end of a round

A sheet slides down over the table:

```
┌─ Round 7 · 6 cards · Normal ─────────────────────────────┐
│                                                          │
│   Name        Bid    Won      Round        Total         │
│   ─────────────────────────────────────────────────────  │
│   You           2      2        +7      34 → 41          │
│   Ana           1      2        −1      34 → 33          │
│   Bogdan        0      0        +5      23 → 28          │
│   Carmen        3      1        −2      21 → 19          │
│                                                          │
│   Ana has missed four in a row — a fifth costs 10.       │
│                                                          │
│                                 [ Deal round 8 ]         │
└──────────────────────────────────────────────────────────┘
```

Two details that are easy to get wrong and worth getting right:

- **`34 → 41` is the honest rendering.** Inside `onRoundScored` the engine's round score is what
  this round was worth and the total is the committed total it has *not yet* been folded into. A
  client that shows one without the other reads differently either side of the commit; showing the
  arrow shows both and needs no explanation.
- **The streak line is a genuine addition.** The engine awards ±10 on the fifth consecutive exact
  bid or miss, and one-card rounds count for neither. A player has no way to see that coming
  without counting rounds themselves. `Player::getConsecutiveWins()` / `getConsecutiveLosses()`
  make it a read, so the scoreboard carries streak pips (`●●●●`) and the round sheet warns at four.

## 8. Forehead and hidden rounds

These are the two rounds where the GUI can beat the terminal outright, because "you cannot see your
own card but you can see everyone else's" is a *picture*.

- **Forehead** — your own card is drawn face-down, held against your forehead in the classic pose;
  every other seat's single card is drawn **face-up on their plaque**, by name. You bid on what
  everyone else is holding.
- **Hidden** — every card at the table, yours included, is face-down. The bid strip appears over an
  empty table.
- The info strip's round type reads **Forehead** or **Hidden** in an accent colour, so the round is
  identifiable before the cards are dealt.

Who may see which hand is asked of `GameEngine::canSeeHand(viewer, holder)`, per seat, per frame —
never inferred from who is human. The terminal does exactly this, and the reason is that two
clients deciding it independently is how they come to disagree.

Play itself is unchanged: there is one card in hand and no decision to make, so the client simply
plays it out with the usual pacing.

## 9. The end of the game

The table dims and the final standings take the window:

```
                       ROMANIAN WHIST

                    1.   You        47   ★
                    1.   Ana        47   ★
                    3.   Bogdan     31
                    4.   Carmen     28

           You and Ana tie for first with 47 points.

              [ Play again ]      [ Close ]
```

- **Places come from `Standing::place`**, so a table finishing 47, 47, 31, 28 reads **1, 1, 3, 4** —
  a tie for first consumes second.
- **Every winner is starred**, from `getWinners()`, which returns every seat on the top score.
  Romanian Whist has no tie-breaker and the client does not invent one.
- **The closing line names all of them**, with the same wording the terminal uses: *"Ana wins with
  47 points."* for one, *"Ana and Bogdan tie for first with 47 points."* for two, *"Ana, Bogdan and
  Dan tie for first…"* for three.
- **Play again** reopens the setup dialog pre-filled with the same settings.

## 10. Round history

`View ▸ Round history…` opens a scoresheet — the whole game, one row per round:

| # | Cards | Type | Trump | You bid/won/pts | Ana bid/won/pts | … |
|---|---|---|---|---|---|---|
| 1 | 1 | Normal | ♦7 | 0 / 0 / +5 | 1 / 1 / +6 | … |
| 2 | 1 | Normal | ♣K | 1 / 0 / −1 | 0 / 1 / −1 | … |

Purely a GUI addition, and cheap: the engine lays out the whole schedule at `start()`, so
`getRound(index)` answers for every round including ones not yet played, and since engine 4.x holds
cards by value a finished round still reports the cards actually played in it.

One thing the engine does **not** store per round is the points that round was worth — scoring
writes into the players, not into the `Round`. So the client snapshots each round's score inside
`onRoundScored`, which is the only callback at which the engine still names that round. See
§18.3.

## 11. Visual language

**Not a skeuomorphic casino.** Flat, calm, high contrast; a felt-toned table surface rather than a
photograph of felt, and cards that look like cards without pretending to be photographs of cards.

| Element | Treatment |
|---|---|
| Table surface | A single desaturated green, subtly darker at the edges. No texture bitmap. |
| Cards | White face, 2 px rounded corners, a hairline border. Rank and suit in the top-left corner and repeated rotated in the bottom-right, plus a large centred suit glyph. Hearts and diamonds red, spades and clubs near-black. |
| Card backs | A flat accent colour with a simple geometric pattern. One back for the whole table. |
| Dimmed (illegal) card | 45% opacity plus a slight desaturation — not grey text on white, which is unreadable at card size. |
| Active seat | A 3 px ring in the accent colour, animated in over 120 ms. Never a colour-only cue: the seat plaque also gains a small ▸ marker, for players who cannot distinguish it. |
| Winning card | Raised shadow and an accent hairline. |
| Typography | The platform UI font throughout. Card ranks in a tabular-figures weight so `10` and `4` occupy the same box. |
| Scoreboard numbers | Tabular figures, right-aligned. Positive round scores green with a `+`, negative red with a `−`. |

**Cards are drawn, not shipped as images.** A `CardRenderer` paints one with `QPainter` into a
cached `QPixmap` per (card, size, state). That means: no asset licensing, crisp on every display,
correct at any DPI, and a card size that can follow the window instead of being fixed at whatever
the PNGs were exported at. Eight-cards-times-four-suits-times-thirteen-ranks is a small cache and
it can be built lazily.

**Dark mode** falls out of using a palette object rather than hard-coded colours, and the table
surface changes tone rather than the cards changing colour — a card is white in the dark too.

**Animation budget**, all of it interruptible and all of it disabled by a single "Reduce motion"
preference (which also honours the platform setting where Qt exposes it):

| Moment | Duration |
|---|---|
| Card hand → table | 180 ms, ease-out |
| Trick sweep to winner | 220 ms, ease-in |
| Bid strip in/out | 140 ms |
| Round sheet in/out | 200 ms |
| Turn ring | 120 ms fade |

Bot **thinking time** is not an animation but a pace, and it is the bot-speed slider: 0 ms at
"Instant" through about 900 ms at "Slow", defaulting to the terminal's 700 ms.

## 12. Keyboard, accessibility, preferences

Everything playable is reachable from the keyboard: digits bid and select, arrows move the
selection, Enter commits, Space advances between tricks and rounds, Esc closes a sheet.

No information is carried by colour alone. Suits carry their glyph, the active seat carries a
marker as well as a ring, legal cards are lifted as well as lit, and winners are starred as well as
highlighted. Every interactive element gets an accessible name, so a screen reader announces
"Queen of spades, playable" rather than "button".

**Preferences** (`View ▸ Preferences…`) persist through `QSettings`: bot speed, wait-between-tricks,
reduce motion, theme (System / Light / Dark), card size, and the last-used setup so **New Game**
opens where you left it.

---

# Part II — Architecture and implementation

## 13. The three decisions that shape everything else

### 13.1 Where the code lives — **a new repository**

`romanian_whist_qt`, with the engine as its own submodule at `libs/RomanianWhistEngine`, exactly as
here. Not a second target in this repo.

The reason is not tidiness, it is that the two clients share less than it first appears. The
obvious candidate for sharing is `GameView` and `HandLayout` — but `HandLayout` exists to reconcile
three orderings *one of which is a list of numbers a player types*, and a GUI has no such list. A
GUI needs display order and legality; it does not need `HandEntry::choice`. What actually transfers
is the display-order comparator (about forty lines) and the winner-announcement wording (about
twenty). Copying sixty lines with a comment naming where they came from is cheaper, and stays
cheaper, than a third repository or a shared library that both clients then have to agree with.

The one real cost — a rules-visible fix having to land twice — is bounded by the fact that rules
are not in either client.

**If you would rather have one repo**: add `src/gui/` and a `whist_client_core` static library
holding `GameView`, `HandLayout` and the pure half of `CardFormat`, and split `CardFormat` in two
(a static ordering half with no `Terminal`, and a painting half with one). That is a real
refactor of working code, which is why it is the alternative and not the recommendation.

### 13.2 Widgets or QML — **Qt Widgets, with `QGraphicsView` for the table**

| | Widgets + QGraphicsView | Qt Quick / QML |
|---|---|---|
| Language | C++ only | C++ plus QML plus JavaScript |
| Card table | `QGraphicsScene` is *made* for movable, animatable, z-ordered items | Also good; `Repeater` over a model |
| Animation | `QPropertyAnimation` on `QGraphicsObject` | Better — declarative transitions |
| Native look for dialogs | Free | Needs work |
| Testability from Catch2 | The scene is a plain object graph | Requires a QML engine |
| Deployment size | Smaller | Ships the QML runtime |

Quick would give prettier animation for less code. Widgets wins here because this project is
uniformly C++ with a documented, commented, tested C++ style, and splitting the client across two
languages to save a hundred lines of animation code is a bad trade for a codebase whose defining
quality is that every decision is written down in the language it was made in.

Requires **Qt 6.5 or newer** (LTS), components `Widgets` and `Svg`. Confirm what is installed
locally before Phase 0 — `qmake6 --version`, or `cmake --find-package`.

### 13.3 Where the game loop runs — **on a worker thread**

This is the load-bearing decision, and it comes straight out of the engine's own contract.

`GameEngine::run()` blocks. It calls into move providers and observers **synchronously, on the
calling thread**, and it does not return until the game is over. `IMoveProvider::playCard()` is
expected to *return a card*, which for a human means it must not return until the human has chosen
one. Qt's world is the opposite: a GUI thread that must return to its event loop promptly or the
window stops repainting.

Three ways to reconcile them:

**A. Engine on a worker `QThread`. ← recommended.** The GUI thread runs the event loop and nothing
else. Observer callbacks build an immutable snapshot and post it across with a queued signal. The
human's move provider blocks the *worker* thread on a condition variable while the GUI thread
collects the choice, and is woken when it arrives.

**B. Engine on the GUI thread, nested `QEventLoop` inside the move provider.** Simpler on paper —
no threads, no snapshots, no metatypes. Rejected because a nested event loop re-enters arbitrary
GUI code from *inside an engine callback*, and the engine throws `std::logic_error` when a callback
drives the game. A player clicking **New Game** while the nested loop is spinning would call
`start()` or `run()` from inside `playCard()`; every such path has to be found and defended by
hand, and the failure mode is a crash mid-game. Pacing sleeps would also freeze the window unless
they too became nested loops.

**C. Restructure the engine to a non-blocking `playStep()`.** `GameEngine.h` notes this is an
additive change the engine is shaped for. It is also an engine change, and this plan does not need
one.

So: **A**, with a small number of rules that are absolutely not negotiable, listed in §15.

## 14. Component map

```
  GUI THREAD                              │        GAME THREAD (QThread)
                                          │
  MainWindow                              │   EngineWorker : QObject, IGameObserver
   ├── InfoStripWidget                    │    ├── romanian_whist::GameEngine
   ├── TableView : QGraphicsView          │    ├── buildSnapshot()   (reads engine)
   │    ├── SeatItem × n                  │    ├── Pacer            (interruptible sleep)
   │    ├── TrickItem                     │    └── owns GuiMoveProvider (via the engine)
   │    └── HandItem ── CardItem × n      │
   ├── ScoreboardWidget                   │   GuiMoveProvider : IMoveProvider
   ├── PromptBar                          │    └── parks this thread until the GUI answers
   ├── BidStrip        (overlay)          │
   ├── RoundSheet      (overlay)          │
   └── GameOverPanel   (overlay)          │
                                          │
  Dialogs: NewGameDialog, PreferencesDialog, RoundHistoryDialog, AboutBox
                                          │
  ─────────────────────  the only two things that cross  ─────────────────────
                                          │
   worker → gui   snapshotReady(shared_ptr<const GameSnapshot>)
                  bidRequested(BidRequest) / cardRequested(PlayRequest)
                  roundScored(RoundResult) / gameOver(FinalResult) / gameAbandoned(QString)
                                          │
   gui → worker   submitBid(unsigned)     — wakes the parked provider
                  submitCard(size_t)      — wakes the parked provider
                  skipRestOfRound()       — wakes the pacer
                  requestStop()           — engine's own thread-safe flag
                  abandon()               — wakes the provider so it can throw
```

Everything crossing the line is either a value or a `shared_ptr<const T>`. **No object owned by the
worker is ever touched from the GUI thread**, and the `GameEngine` reference handed to an observer
callback never leaves the callback.

## 15. The thread boundary, precisely

Four rules. Each of them is written in `IGameObserver.h`; they are repeated here because the
consequences of breaking one are silent rather than loud.

**R1 — a `const GameEngine&` is only readable on the game thread, inside the callback that handed
it over.** Not stored, not captured in a lambda that outlives the call, not passed in a signal. The
callback reads what it needs into a `GameSnapshot` and the snapshot travels.

**R2 — no observer may add or remove observers.** The engine is walking its observer vector to
reach the call and both throw rather than corrupt the walk. `EngineWorker` registers itself once,
before `start()`, and detaches only after `run()` returns.

**R3 — no observer may drive the game.** `run()` and `playRound()` throw from every callback except
`onGameStarted()`. The only thing a callback may always do is `requestStop()`.

**R4 — `requestStop()` is the *only* engine method that is safe to call from the GUI thread.** It
sets an atomic flag. Everything else — including every accessor — must be reached by posting to the
worker.

A single assertion enforces R1 and R4 in debug builds:

```cpp
// EngineWorker.h
#define WHIST_ASSERT_GAME_THREAD() \
    Q_ASSERT_X(QThread::currentThread() == this->thread(), \
               Q_FUNC_INFO, "engine state touched off the game thread")
```

## 16. The snapshot

`GameSnapshot` is this repo's [`GameView`](../src/GameView.h) with the terminal parts removed and a
Qt-safe name — "view" means a widget in Qt, and a struct called `GameView` sitting next to a
`QGraphicsView` will be misread by everyone including its author.

```cpp
// GameSnapshot.h — plain data, no Qt types, no engine references.
struct SeatSnapshot
{
    QString name;
    unsigned int seatIndex   = 0;
    unsigned int bidOrder    = 0;   // 1 for whoever opened the round
    bool         isHuman     = false;
    bool         hasBet      = false;   // tells a genuine bid of 0 from no bid
    unsigned int bet         = 0;
    unsigned int tricksWon   = 0;
    unsigned int cardsLeft   = 0;
    int          roundScore  = 0;
    int          totalScore  = 0;
    unsigned int winStreak   = 0;
    unsigned int lossStreak  = 0;

    // Populated only for a seat whose hand the human may currently see
    // (Forehead rounds). Never for the human's own seat.
    std::vector<romanian_whist::Card> visibleHand;
};

struct PlayedCardSnapshot
{
    unsigned int seatIndex = 0;
    romanian_whist::Card card{};
    bool isWinning = false;         // from getCurrentTrickLeader(), never computed here
};

struct GameSnapshot
{
    Phase phase = Phase::Setup;

    unsigned int roundNumber = 0, roundCount = 0;
    unsigned int trickCount  = 0, trickNumber = 0;
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;

    std::optional<romanian_whist::Card> trump;   // empty IS the 8-card round

    std::vector<SeatSnapshot> seats;

    int humanSeat  = -1;            // -1 in a spectated game
    int activeSeat = -1;            // -1 between turns
    unsigned int leadSeat = 0;

    std::optional<romanian_whist::Suit> leadSuit;
    std::vector<PlayedCardSnapshot> table;

    std::vector<romanian_whist::Card> hand;      // the human's, in display order
    bool handIsVisible = false;                  // false in Forehead/Hidden

    unsigned int betsPlaced = 0, betSum = 0;
    std::chrono::seconds elapsed{0};
};
Q_DECLARE_METATYPE(std::shared_ptr<const GameSnapshot>)
```

`buildSnapshot(const GameEngine&)` is a near-verbatim port of
[`refreshFromEngine`](../src/GameView.cpp) and carries its two hard-won details:

- **The seat the table is drawn from is the trick's own first entry**, not
  `getTrickLeaderSeat()` — which has already moved to the winner by the time the trick-end frame is
  drawn. The terminal's `rebuildTable` has the comment; keep it.
- **`canSeeHand` is asked per seat**, and a spectator asks nothing, because "a spectator sees
  everything" would be a client policy and there is no viewer seat to ask about.

It is posted as `std::shared_ptr<const GameSnapshot>` rather than by value: the refcount is atomic,
the object is immutable once built, and a queued signal then copies a pointer instead of a dozen
vectors — at up to one snapshot per card played, that matters less for speed than for making it
structurally impossible for two threads to hold the same *mutable* snapshot.

Register once, in `main()`, before any connection is made:

```cpp
qRegisterMetaType<std::shared_ptr<const GameSnapshot>>();
qRegisterMetaType<BidRequest>();
qRegisterMetaType<PlayRequest>();
```

An unregistered type in a queued connection fails at runtime with a warning and a dropped signal —
which looks exactly like a hung game.

## 17. `GuiMoveProvider` — the parking protocol

The heart of it. The engine asks this object for a move on the game thread; it must not return
until the human has clicked, and it must be able to give up.

```cpp
class GuiMoveProvider : public romanian_whist::IMoveProvider
{
private:
    // Not a QObject: it lives and dies with the engine, on the game thread,
    // and it talks to the GUI through a QObject it holds a pointer to.
    EngineWorker& worker;

    std::mutex mutex;
    std::condition_variable answered;

    std::optional<unsigned int> pendingBid;
    std::optional<std::size_t>  pendingCard;
    bool abandoned = false;

public:
    unsigned int makeBet(const romanian_whist::BetContext& context) override
    {
        BidRequest request;
        request.maxBid        = static_cast<unsigned int>(context.hand.size());
        request.forbiddenBid  = context.forbiddenBet;
        request.roundType     = context.roundType;
        request.isFirstBidder = context.isFirstPlayer;
        // Forehead and Hidden both mean this seat may not bid on its own hand.
        // The provider only ever bids for its own seat, so roundType alone
        // decides - canSeeHand is for rendering every OTHER seat.
        request.hand = (context.roundType == romanian_whist::RoundType::Normal)
                     ? inDisplayOrder(context.hand, context.trump)
                     : std::vector<romanian_whist::Card>{};

        emit worker.bidRequested(request);          // queued: returns immediately

        std::unique_lock lock(mutex);
        answered.wait(lock, [this]{ return pendingBid || abandoned; });

        if(abandoned)
            throw GameAbandoned();                  // see 19.2

        const unsigned int bid = *pendingBid;
        pendingBid.reset();
        return bid;
    }

    std::optional<std::size_t> playCard(const romanian_whist::PlayContext& context) override
    {
        romanian_whist::CardValidator validator;
        const auto legal = validator.getLegalCards(context.hand, context.trump, context.leadSuit);

        // No legal play: the engine's own strategies answer this way too, and
        // there is no number a human could type that would be valid.
        if(legal.empty())
            return std::nullopt;

        emit worker.cardRequested(buildPlayRequest(context, legal));

        std::unique_lock lock(mutex);
        answered.wait(lock, [this]{ return pendingCard || abandoned; });

        if(abandoned)
            throw GameAbandoned();

        const std::size_t index = *pendingCard;
        pendingCard.reset();
        return index;                               // a HAND index, not a display index
    }

    // ---- called on the GUI thread ----
    void submitBid(unsigned int bid)
    {
        { std::lock_guard lock(mutex); pendingBid = bid; }
        answered.notify_one();
    }

    void submitCard(std::size_t handIndex)
    {
        { std::lock_guard lock(mutex); pendingCard = handIndex; }
        answered.notify_one();
    }

    void abandon()
    {
        { std::lock_guard lock(mutex); abandoned = true; }
        answered.notify_all();
    }
};
```

**The index problem is the same one `HandLayout` solves, and it must be solved again.**
`IMoveProvider::playCard` returns *the position of the chosen card within `context.hand`* — the
hand as the engine holds it, in deal order. The GUI shows a fan in *display* order, containing both
legal and illegal cards. So `PlayRequest` carries, per entry, the card, whether it is legal, **and
its original hand index**; the GUI hands back that index and never an index into its own fan.

```cpp
struct PlayRequestEntry
{
    romanian_whist::Card card;
    std::size_t handIndex;   // what the engine wants back. Not the fan position.
    bool legal;
};
```

The engine range-checks the index and erases by position, so a wrong index does not fabricate a
card — it plays a different one, silently, which is worse. This is precisely the mistake
`HandLayout.h`'s comment is about, and the GUI is a *second* chance to make it.

**Validation stays on the GUI side too, but only as a courtesy.** The bid strip disables the
forbidden bid because a player deserves to know before they click, not because the engine needs
protecting — the engine validates every bid and every card it is handed and throws on an illegal
one.

## 18. `EngineWorker`

```cpp
class EngineWorker : public QObject, public romanian_whist::IGameObserver
{
    Q_OBJECT
public:
    explicit EngineWorker(GameSetupRequest setup);   // built on the GUI thread, moved over

public slots:
    void runGame();          // the whole game; blocks this thread until it ends
    void submitBid(unsigned int);
    void submitCard(std::size_t);
    void skipRestOfRound();
    void abandon();

signals:
    void snapshotReady(std::shared_ptr<const GameSnapshot>);
    void bidRequested(BidRequest);
    void cardRequested(PlayRequest);
    void trickWon(unsigned int winnerSeat, QString message);
    void roundScored(RoundResult);
    void gameOver(FinalResult);
    void gameAbandoned(QString reason);

    // ---- IGameObserver, all on this thread ----
    void onRoundStarted(const romanian_whist::GameEngine&) override;
    void onBetRequested(const romanian_whist::GameEngine&, romanian_whist::Seat) override;
    // ... and the rest
};
```

### 18.1 Startup order

```cpp
void EngineWorker::runGame()
{
    engine.addObserver(this);          // BEFORE start(): start() fires onGameStarted on its way out
    startTime = std::chrono::steady_clock::now();

    try
    {
        engine.start(std::move(setup));
        engine.run();                  // every callback below happens inside here
    }
    catch(const GameAbandoned&)
    {
        emit gameAbandoned(tr("Game abandoned."));
        return;                        // the engine is NOT resumable; it dies with this worker
    }
    catch(const std::exception& e)
    {
        emit gameAbandoned(QString::fromUtf8(e.what()));
        return;
    }

    emit gameOver(buildFinalResult(engine));   // standings AND winners, both from the engine
}
```

`GameSetup` is move-only and owns the move providers, so it is built on the GUI thread from the
dialog's answers and moved into the worker before the thread starts. Note also that a rejected
`start()` has already moved from the caller's `GameSetup` — a retry has to build a fresh one, which
means the retry path re-runs the dialog's `buildSetup`, not a stashed copy.

### 18.2 Which callbacks do what

| Callback | The frame it produces |
|---|---|
| `onRoundStarted` | New round: clear the table, deal the fan in, show trump. Snapshot. |
| `onBetRequested` | Turn ring onto that seat. Snapshot. If it is the human, do **not** draw a prompt here — the provider's own `bidRequested` does it, and drawing twice only flickers. |
| `onBetPlaced` | The bid lands in the scoreboard, ring still on the bidder — the frame showing a bid should highlight who made it. Snapshot, then a bot beat. |
| `onBettingComplete` | Hold on the completed bid table for a beat. This is also where the round's skip flag is reset, deliberately *after* the bidding beats. |
| `onTrickStarted` | Trick number advances, table clears. Snapshot. |
| `onCardRequested` | Ring onto that seat; for a bot, the "thinking" hint and a beat. |
| `onCardPlayed` | Snapshot → the card animates from that seat to the table. A bot gets a beat afterward; the human does not — choosing already took time. |
| `onTrickWon` | `getTricksWon(winner)` **includes** this trick and `getCurrentTrick()` is still the finished trick, which is what lets the completed table be drawn under the message. Emits `trickWon` and waits per the trick-pause preference. |
| `onRoundScored` | The round sheet — **and the only place per-round points can be read**, see below. |
| `onRoundComplete` | Nothing. The index has already advanced and rendering here would attribute the round to the next one. |
| `onGameOver` / `onGameStopped` | Final result / abandoned, respectively. |

### 18.3 The one thing the engine does not keep

Round scores are written into the players and folded into totals; a `Round` does not carry what it
was worth. So `onRoundScored` — the last callback at which `getCurrentRoundIndex()` still names the
round being reported — snapshots it:

```cpp
void EngineWorker::onRoundScored(const romanian_whist::GameEngine& engine)
{
    RoundResult result;
    result.roundIndex = engine.getCurrentRoundIndex();
    result.trickCount = engine.getCurrentRoundTrickCount();
    result.roundType  = engine.getCurrentRoundType();
    result.trump      = engine.getCurrentTrumpCard();

    for(unsigned int i = 0 ; i < engine.getPlayerCount() ; i++)
    {
        const romanian_whist::Seat seat{i};
        RoundResultRow row;
        row.name          = QString::fromStdString(engine.getPlayers().at(i).getName());
        row.bid           = engine.getBet(seat).value_or(0);
        row.won           = engine.getTricksWon(seat);
        row.points        = engine.getRoundScore(seat);     // what THIS round was worth
        row.totalBefore   = engine.getTotalScore(seat);     // not yet including it
        row.totalAfter    = row.totalBefore + row.points;   // which is the arrow on screen
        row.winStreak     = engine.getPlayers().at(i).getConsecutiveWins();
        row.lossStreak    = engine.getPlayers().at(i).getConsecutiveLosses();
        result.rows.push_back(row);
    }

    history.push_back(result);          // the round-history dialog reads this
    emit roundScored(result);
    pacer.roundPause();
}
```

## 19. Pacing, stopping, abandoning

### 19.1 Pacing

Blocking in a callback pauses the game, and the engine says so explicitly: *"That is intentional —
it is where pacing and 'press Enter to continue' belong."* On a worker thread it costs nothing,
because the GUI thread is elsewhere and still repainting.

The `Pacer` is this repo's [`Pacer`](../src/Pacer.h) with the sleep swapped for an interruptible
wait, so **Skip to end** and **Abandon** take effect instantly instead of after the current beat:

```cpp
bool Pacer::sleepOrSkip(std::chrono::milliseconds duration)
{
    std::unique_lock lock(mutex);
    return interrupted.wait_for(lock, duration, [this]{ return skipRequested || abandoned; });
}
```

`trickPause()` either dwells or waits for the player, per the preference; `roundPause()` waits for
the **Deal round n** button. Both are the same wait, and both are interruptible.

### 19.2 Two different ways to end a game early

They are not interchangeable and conflating them is how this goes wrong.

**`requestStop()` — the clean one.** Thread-safe by design (it sets one atomic flag), honoured at
the next round, bid or trick boundary. The round it lands in is left **unscored**, and
`onGameStopped()` fires instead of `onGameOver()`. It is the right answer whenever the game is
between moves — a bot's turn, a pause, a trick dwell.

But: **it cannot interrupt a provider already parked waiting for a human.** The flag is only read
between bids, tricks and rounds. The engine states the remedy plainly: *"Unparking that provider is
the client's job, and throwing from it is the supported way out."*

**`GameAbandoned` — the hard one.** `EngineWorker::abandon()` calls `requestStop()` *and*
`provider->abandon()` *and* `pacer.abandon()`. Whichever the game thread is sitting in wakes; a
parked provider throws; the exception propagates out of `run()`; `runGame()` catches it and emits
`gameAbandoned`. The engine is then, by contract, **not resumable** — it is destroyed with the
worker and a new game builds a new one.

So the close/new-game path is:

```
User closes the window or picks New Game
        │
        ├─ no game running ────────────────────► proceed
        │
        └─ game running
              worker->abandon()   (queued; also sets the atomic flags directly)
              thread.quit(); thread.wait(2000ms)
              if still running → the game thread is stuck in something that is
                                 not a wait we own; log it and leak the thread
                                 rather than terminate it mid-callback
```

`MainWindow::closeEvent` runs that and only then accepts the close, so the worker never outlives
the widgets it emits into.

### 19.3 Destruction order

The terminal client's [`TerminalRomanianWhist.h`](../src/TerminalRomanianWhist.h) declares the
engine **last** on purpose: the engine owns the players, a player owns the human's move provider,
and the provider holds a reference to the UI context. Members die in reverse declaration order, so
declaring the engine last is what makes the provider die before the thing it points at.

`EngineWorker` has exactly the same hazard and the same fix. The `GameEngine` member is declared
last, after the pacer and the mutexes the provider signals on. **Do not reorder it, and say so in
the header**, because the symptom of getting it wrong is a use-after-free during shutdown that only
shows up under a debugger on the machine that happens to lose the race.

## 20. Files

```
romanian_whist_qt/
├── CMakeLists.txt
├── CMakePresets.json
├── .gitmodules                      → libs/RomanianWhistEngine
├── libs/RomanianWhistEngine/        (submodule, pinned to a tag)
├── resources/
│   ├── whist.qrc
│   ├── icons/                       app icon, per platform
│   └── styles/table.qss
├── src/
│   ├── main.cpp                     QApplication, metatype registration, MainWindow
│   │
│   ├── core/                        no Qt widgets, unit-testable with Catch2
│   │   ├── GameSnapshot.{h,cpp}     the frame, as data          (port of GameView)
│   │   ├── CardOrder.{h,cpp}        display order               (port of CardFormat's statics)
│   │   ├── Requests.h               BidRequest / PlayRequest / RoundResult / FinalResult
│   │   ├── WinnerAnnouncement.{h,cpp}  the closing line         (port of Renderer's static)
│   │   └── Settings.{h,cpp}         QSettings wrapper, typed
│   │
│   ├── engine/                      lives on the game thread
│   │   ├── EngineWorker.{h,cpp}     IGameObserver + QObject
│   │   ├── GuiMoveProvider.{h,cpp}  the parking protocol
│   │   ├── Pacer.{h,cpp}            interruptible waits         (port of Pacer)
│   │   └── GameAbandoned.h          the exception
│   │
│   ├── paint/
│   │   ├── CardRenderer.{h,cpp}     QPainter card art + pixmap cache
│   │   └── Theme.{h,cpp}            palette, metrics, light/dark
│   │
│   ├── table/                       QGraphicsScene contents
│   │   ├── TableScene.{h,cpp}       applies a snapshot to the scene
│   │   ├── TableView.{h,cpp}        QGraphicsView, resize → relayout
│   │   ├── CardItem.{h,cpp}         QGraphicsObject, animatable
│   │   ├── HandItem.{h,cpp}         the fan; hover, selection, click
│   │   ├── SeatItem.{h,cpp}         plaque, turn ring, visible hand (Forehead)
│   │   └── TrickItem.{h,cpp}        played cards, winning highlight
│   │
│   ├── widgets/
│   │   ├── MainWindow.{h,cpp}       owns the thread, wires every signal
│   │   ├── InfoStrip.{h,cpp}
│   │   ├── ScoreboardWidget.{h,cpp}
│   │   ├── PromptBar.{h,cpp}
│   │   ├── BidStrip.{h,cpp}
│   │   ├── RoundSheet.{h,cpp}
│   │   └── GameOverPanel.{h,cpp}
│   │
│   └── dialogs/
│       ├── NewGameDialog.{h,cpp}
│       ├── PreferencesDialog.{h,cpp}
│       ├── RoundHistoryDialog.{h,cpp}
│       └── AboutDialog.{h,cpp}
└── tests/
    ├── CMakeLists.txt
    ├── CardOrderTests.cpp
    ├── SnapshotTests.cpp
    ├── WinnerAnnouncementTests.cpp
    ├── MoveProviderTests.cpp        the parking protocol, no GUI
    └── HeadlessGameTests.cpp        a whole game, seeded, no widgets
```

The `core/` — `engine/` — `paint/table/widgets/` split is the same discipline the terminal client
enforces with `GameView`: the layer that knows about the engine does not know about drawing, and
the layer that draws cannot advance the game by accident.

## 21. CMake

```cmake
cmake_minimum_required(VERSION 3.21)

project(RomanianWhistQt
    VERSION 0.1.0
    DESCRIPTION "Qt desktop client for the Romanian Whist engine"
    LANGUAGES CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libs/RomanianWhistEngine/CMakeLists.txt")
    message(FATAL_ERROR
        "The Romanian Whist engine submodule is missing.\n"
        "Run: git submodule update --init --recursive")
endif()

add_subdirectory(libs/RomanianWhistEngine)     # WHIST_BUILD_TESTS is off: not top level

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Svg)
qt_standard_project_setup()                    # AUTOMOC/AUTOUIC/AUTORCC and C++ standard

# Everything but main(), so the tests have something to link against - exactly
# the terminal client's terminal_core arrangement, and for the same reason.
add_library(whist_gui_core STATIC
    src/core/GameSnapshot.cpp
    src/core/CardOrder.cpp
    src/core/WinnerAnnouncement.cpp
    src/core/Settings.cpp
    src/engine/EngineWorker.cpp
    src/engine/GuiMoveProvider.cpp
    src/engine/Pacer.cpp
    src/paint/CardRenderer.cpp
    src/paint/Theme.cpp
    # ... table/, widgets/, dialogs/
)

target_compile_features(whist_gui_core PUBLIC cxx_std_20)
target_include_directories(whist_gui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(whist_gui_core PUBLIC RomanianWhist::engine Qt6::Widgets Qt6::Svg)

qt_add_executable(romanian_whist_qt src/main.cpp)
qt_add_resources(romanian_whist_qt "app" PREFIX "/" FILES resources/styles/table.qss)
target_link_libraries(romanian_whist_qt PRIVATE whist_gui_core)

set_target_properties(romanian_whist_qt PROPERTIES
    WIN32_EXECUTABLE ON          # no console window on Windows
    MACOSX_BUNDLE ON)

configure_file(src/Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)
target_include_directories(whist_gui_core PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)

option(WHIST_QT_BUILD_TESTS "Build the Qt client's test suite" ${PROJECT_IS_TOP_LEVEL})
if(WHIST_QT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

`WIN32_EXECUTABLE ON` and `MACOSX_BUNDLE ON` are not cosmetic: without the first, every Windows
launch opens a console window behind the game; without the second, macOS treats the binary as a
command-line tool and it cannot take focus or carry an icon.

Presets mirror this repo's (`default` → `build/`, `release` → `build-release/`).

## 22. What gets ported, and what changes

| From the terminal client | Becomes | What changes |
|---|---|---|
| [`GameView.{h,cpp}`](../src/GameView.h) | `core/GameSnapshot` | `std::string` → `QString`; `bool hasTrump` + `Card` → `std::optional<Card>`; drop `HandEntry::choice`; drop `message`/`hint`/`error` (those are GUI state, not game state). Keep every comment about `canSeeHand`, the lead seat, and rebuilding the hand each frame. |
| [`HandLayout.{h,cpp}`](../src/HandLayout.h) | folded into `PlayRequest` | The numbering machinery goes; the **hand-index-carrying** goes nowhere — see §17. |
| `CardFormat::inDisplayOrder` / `sortForDisplay` / `suitPosition` | `core/CardOrder` | Loses its `Terminal` member; becomes free functions. |
| `CardFormat::suitGlyph` / `card` / `isRed` | `paint/CardRenderer` | Escape sequences → `QPainter`. |
| [`Pacer.{h,cpp}`](../src/Pacer.h) | `engine/Pacer` | `sleep` → `condition_variable::wait_for`; `--auto` → a preference. Keeps the rule that a skip only skips the round it was asked for. |
| `Renderer::winnerAnnouncement` | `core/WinnerAnnouncement` | Verbatim, plus `tr()`. Its test comes with it. |
| `SetupWizard` | `dialogs/NewGameDialog` | Same questions, same bounds, one screen. |
| `TerminalRomanianWhist` | `engine/EngineWorker` | Same callbacks, same order, same comments. Draws a snapshot instead of a frame. |
| `AppOptions`, `Terminal`, `Input`, `Renderer` | — | Not ported. Terminal capability detection has no GUI meaning. |

Each ported file keeps a header comment naming this repo and the file it came from, so a fix that
lands in one is findable in the other.

## 23. Testing

The point of `core/` and `engine/` having no widgets in them is that most of this is testable
without a window.

**Catch2, no `QApplication`:**

- `CardOrderTests` — trump leads, colours alternate, ranks descend. Ported wholesale.
- `WinnerAnnouncementTests` — one winner, two, three. Ported wholesale.
- `SnapshotTests` — drive a real `GameEngine` with scripted providers (the engine's
  `tests/ScriptedMoveProvider.h` is the model) and assert the snapshot: that a bid of zero reads as
  `hasBet` and not as no bid; that the winning-card flag tracks `getCurrentTrickLeader()`; that
  `leadSeat` is the trick's first entry and not the trick leader at trick end; that a Forehead
  round shows every hand but your own and a Hidden round shows none.

**Catch2, with a `QCoreApplication` and a real thread:**

- `MoveProviderTests` — the parking protocol on its own. A bid submitted from another thread wakes
  it; `abandon()` while parked throws `GameAbandoned`; an answer arriving *before* the wait is not
  lost (the predicate form of `wait` is what guarantees that, and the test is what proves the
  predicate form was used).
- `HeadlessGameTests` — a whole seeded game, four scripted bots, no widgets, asserting it reaches
  `gameOver` with the standings the engine reports; and a second run that abandons mid-round and
  asserts the thread joins.

**QTest, with widgets:** the dialogs, because their validation rules are real (name uniqueness,
seat bounds) and cheap to drive with `QTest::keyClicks`. Nothing else — asserting on pixel
positions in a scene is a maintenance liability that catches nothing.

**Determinism** comes free from the same place it does today: the seed feeds both the deal
(`GameSetup::shuffleSeed`) and each bot's `RandomCardStrategy`, offset per seat. Seeding one and
not the other still wanders.

## 24. CI and packaging

Mirror this repo's three-platform matrix, adding Qt via `jurplel/install-qt-action` (or the
platform package manager on Linux). Tests run headless with `QT_QPA_PLATFORM=offscreen`, which is
also what makes the QTest half runnable at all on a CI runner.

Packaging per platform:

| Platform | Tool | Output |
|---|---|---|
| Windows | `windeployqt` | A zip of the exe plus its Qt DLLs and plugins |
| macOS | `macdeployqt` | A `.app` in a `.dmg`; unsigned unless a certificate exists |
| Linux | `linuxdeploy` + the Qt plugin | An AppImage |

The release job stays as it is: tag from the `CMakeLists.txt` version, skip if the tag exists,
upload the artifacts.

---

## 25. Delivery, in phases

Each phase ends somewhere you could stop, and each has one thing that must be true to move on.

**Phase 0 — the skeleton.** New repo, engine submodule, CMake, an empty `QMainWindow`, CI green on
three platforms. *Done when:* the window opens on all three and the engine links.

**Phase 1 — the thread boundary.** `EngineWorker`, `GuiMoveProvider`, `GameSnapshot`, and a
throwaway `QTextEdit` that logs each snapshot. Four bots, no human, no art. *Done when:* a seeded
demo game runs start to finish, the log matches the terminal client's transcript for the same seed,
and abandoning mid-game joins the thread cleanly. **This is the phase the whole plan rests on —
do not start Phase 2 until it is boringly reliable, because every later bug will otherwise look
like a threading bug.**

**Phase 2 — cards on a table.** `CardRenderer`, `Theme`, `TableScene`, seats, trick, static hand.
Still bots only, still no interaction. *Done when:* a spectated game is watchable and legible.

**Phase 3 — the human plays.** `BidStrip`, hand hover/click/keyboard, legality lighting, prompt
bar. *Done when:* a full game is playable with a mouse and again with only the keyboard.

**Phase 4 — the game around the game.** `NewGameDialog`, `ScoreboardWidget`, `RoundSheet`,
`GameOverPanel`, Play again. *Done when:* the app never needs a command line and never needs
restarting between games.

**Phase 5 — polish.** Animation, `RoundHistoryDialog`, `PreferencesDialog` + `QSettings`, dark
mode, reduce-motion, Forehead/Hidden art, streak pips. *Done when:* the design in Part I is on
screen.

**Phase 6 — ship.** Packaging per platform, icons, About box with both versions, README,
CHANGELOG, `v1.0.0`.

## 26. Sharp edges

| Risk | Why it bites | What to do |
|---|---|---|
| A queued signal whose type is unregistered | Qt warns to stderr and **drops the signal**; the game simply appears to hang | Register every custom type in `main()`, before any connection. Assert in a debug build that each registration returned a valid id. |
| Touching engine state from the GUI thread | Works nearly always; corrupts under load | `WHIST_ASSERT_GAME_THREAD()` at the top of every worker method that touches the engine. `requestStop()` is the sole exception and the sole one documented as safe. |
| Storing the `const GameEngine&` from a callback | Dangles the moment the callback returns | Never name it outside a callback body. `buildSnapshot` takes it as a parameter and returns a value. |
| Adding/removing an observer, or driving the game, from a callback | `std::logic_error` out of the engine | Register once before `start()`; the only thing a callback may do is `requestStop()`. |
| `requestStop()` on a game parked for the human | Nothing happens — the flag is only read between moves | The abandon path always wakes the provider too. §19.2. |
| A wrong hand index from the GUI | Plays a *different legal card*, silently | `PlayRequest` carries the original hand index per entry; nothing else is ever sent back. §17. |
| Rendering the round result in `onRoundComplete` | The index has already advanced; the sheet names the wrong round | Use `onRoundScored`, and read *both* round score and total. §18.3. |
| Reading `getStandings().front()` as "the winner" | Names one winner of a drawn game | Use `getWinners()`, always. It cost this project a 2.1.0 release once already. |
| Member declaration order in `EngineWorker` | Use-after-free at shutdown, racy and machine-dependent | Engine declared last, with the comment explaining why. §19.3. |
| A card size that assumes a fixed window | Everything shifts on resize or on a HiDPI display | Card geometry derived from the view rect; pixmap cache keyed on `(card, size, state, dpr)`. |
| Reimplementing "who wins the trick" for the highlight | The highlight eventually disagrees with the engine | `getCurrentTrickLeader()`, per card. It exists for exactly this. |

## 27. Still to decide

1. **One repo or two** (§13.1). The recommendation is two; the alternative is a real refactor of
   `CardFormat` and worth doing only if you actually want one binary.
2. **How far the animation goes.** The plan budgets five animations. Dealing the hand card-by-card
   at the start of a round is a sixth and it is genuinely nice; it also adds a second or two to
   every round, so it needs to be a preference if it happens at all.
3. **Whether the GUI keeps a spectator "see everything" mode.** The terminal deliberately does not:
   a spectator is not a seat, so there is no viewer to ask `canSeeHand()` about, and showing every
   hand would be a *client policy* the engine has no opinion on. A GUI is a more tempting place to
   add one. If it is added, it must be an explicit, labelled toggle — never the default, and never
   silently applied to a seated player.
4. **Qt 6.5 vs 6.8.** 6.5 is LTS and widely packaged; 6.8 has nicer `QGraphicsView` HiDPI
   behaviour. Check what the three CI runners can install before committing to a floor.

---

*Written against engine 4.1.0 and terminal client 2.2.0. Every engine behaviour cited here is
documented in `libs/RomanianWhistEngine/include/romanian_whist/GameEngine.h`,
`IGameObserver.h`, `IMoveProvider.h` and `docs/RULES.md` — if one of them disagrees with this
document, it is right and this is out of date.*
