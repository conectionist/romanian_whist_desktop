# Qt desktop client, on the v4 engine

A design sketch for a Qt 6 / C++ GUI client for Romanian Whist, written against the engine as it
exists after `ENGINE_V4_PLAN.md` — which has now landed in full. The engine is at 4.1.0 and every
prerequisite below is met, so this is buildable today.

> **This is the canonical copy.** It used to live in the engine repository, beside the v4 plan it
> depends on, and moved here when this repo was created; the engine's copy has been deleted. Engine
> files now live under `libs/RomanianWhistEngine/` (§11) and are reachable from here; terminal files
> are not, so both are named by repository-relative path rather than linked.

---

## 1. Prerequisites

**All met** — the engine is at **4.1.0** and nothing here is waiting on it. What each v4 phase gave
this client, for orientation:

| v4 phase | What this client gets from it |
|---|---|
| 0 — tests | The engine under the GUI is pinned by a Catch2 suite |
| 1 — seats | `Seat` is the identity used throughout the UI. Note it is a **struct with an explicit constructor**, not an integer: `Seat{i}` to build one, `seat.index` to read one |
| 2 — loop + observer | **Load-bearing.** The entire design is `IGameObserver` + `run()` |
| 3 — `start(GameSetup)` | Setup dialog builds a `GameSetup` directly. `shuffleSeed` came with it, which makes a reproducible deal a one-line debugging aid |
| 4 — cards by value | `CardValidator::getLegalCards(hand, trump, leadSuit)` returns a `std::vector<Card>` by value, and the hand `PlayContext` hands over is a `const std::vector<Card>&` — cards by value inside it, where copying `Card*` was a bug. Copying one for a snapshot is now trivial |
| 5 — Forehead/Hidden | `canSeeHand()` and `RoundType`, which the table and the bid prompt both need |
| 4.1.0 (post-plan) | `Standing::place` and `getWinners()` — the engine ranks the standings, so no client decides a tie for itself |

Repository layout follows `romanian_whist_terminal`: the engine is a git submodule at
`libs/RomanianWhistEngine`, consumed with `add_subdirectory` and linked as `RomanianWhist::engine`,
behind the same guard that tells you to run `git submodule update --init --recursive` when it is
missing. Qt arrives via `find_package(Qt6 REQUIRED COMPONENTS Widgets)`.

**The submodule has landed** — pinned at engine `1270b8a` (`v4.1.0-3-g1270b8a`), with `.gitmodules`
recording the same HTTPS URL the terminal uses so anonymous clones and CI resolve it without a key.
Clone this repo with `--recurse-submodules`.

`CMakeLists.txt` agrees with all of the above: C++20 stated outright rather than inherited through
the engine's interface, Qt 6 with no Qt 5 fallback, and a `FATAL_ERROR` guard on
`libs/RomanianWhistEngine/CMakeLists.txt` — the terminal's form, since there is no useful build
without the engine and a soft skip would only defer the failure to link errors that say nothing
about submodules. The engine's own `WHIST_BUILD_TESTS` follows `PROJECT_IS_TOP_LEVEL`, so as a
subproject its test suite and the Catch2 fetch behind it are skipped with nothing to pass in.

Its comments carry the reasoning — including the three silent ways to embed no card art at all —
and this document deliberately does not restate them. The build file is the authority on the build.

---

## 2. The core constraint

**The Qt client is architecturally the *planned* web backend, not the terminal.**

The web backend named throughout this document **does not exist yet** — see
the engine's `ENGINE_V4_PLAN.md` §1. Where the text below compares against it, read that as
"the shape the two share", not as code to go and copy. The Qt client will be the first client to
run the engine off the UI thread, and so the first to find out where this is wrong.

The terminal can let the engine block, because its "UI" is a blocking `std::cin` — when
`ConsoleMoveProvider` waits for input, there is nothing else the process needs to be doing. Qt has
no such luxury: `engine.run()` on the GUI thread would freeze the event loop, and the window would
stop repainting until the game ended.

So the Qt client needs what a web backend would need:

1. The engine on a **worker thread**.
2. An **observer** that marshals events out to the GUI thread.
3. A **move provider** that blocks the game thread until the GUI answers.

### The two directions

```
GUI thread                            game thread (QThread)
──────────                            ─────────────────────
MainWindow                            engine.run()
   ▲                                     │
   │  Qt signals (queued),                ├─ IGameObserver ──► snapshot ──┐
   │  GameSnapshot BY VALUE      ◄────────┴──────────────────────────────┘
   │
   └─ click ──► provider->submit(i) ──► condition_variable ──► unblocks playCard()
```

Only the outbound direction needs Qt's machinery. Inbound is a plain direct call, because the
provider is thread-safe by construction — it touches nothing but a mutex and a condition variable.

### Why the engine keeps the loop

The obvious alternative is to invert it: let the GUI own the loop and drive the engine turn by turn
through its API, which would put everything on one thread and delete this whole section. It is the
right question to ask once and then stop asking, so here is the answer.

**It is not a decision this client can make.** The engine's entire public driving surface is
`start()`, `run()`, `playRound()` and `requestStop()`; `placeBet()` and the deal are private, and
there is no `step()`.

**And the mechanics are not the expensive part** (Appendix A.1). `GameEngine.h` says outright that
the three stages of a round "advance state that lives in members - the phase, the active seat, the
trick number, the round's in-flight trick - rather than in locals of a nested loop, **which is what
keeps a future non-blocking `playStep()` an additive change**". Reading `playRound()` bears that out: what is actually left in locals is two
seat rotations and their counters, in `runBidding()` and `playTrick()`, plus the round and trick
loop indices. `playTrick()`'s trump, lead suit and legal set are locals too but are *derived* fresh
each seat, so a step function would recompute them exactly as the loop does. The engine has already
done most of this work, deliberately.

What is expensive is the contract on top of it. Each stop check in `playRound()` and `runBidding()`
carries a paragraph about which boundary the flag may be read at and what fires when it lands there
— those comments are the specification for §6's table, down to why the last trick gets one extra
read and why a stop in `onRoundScored()` is a no-op. Inverting the loop means re-deriving every one
of them as a state-machine invariant, against a shipped terminal client and a Catch2 suite both
written to the current contract.

So the trade is a few hundred lines of well-understood threading code **in this client**, against
re-deriving the engine's stop semantics and risking the one client that already works. The threading
tax is real, but it is paid once, in the three types of §4, and it leaks into no widget or dialog.

**The middle path is worse than either.** Keeping `run()` but calling it on the GUI thread, with the
provider spinning a nested `QEventLoop` until the click arrives, looks like it buys single-threaded
simplicity for nothing. It does not: a nested event loop delivers *every* pending event, including a
window close that destroys `MainWindow` while engine frames are still on the stack, and a second
click that re-enters code which calls back into the engine — precisely what §5's re-entrancy guards
throw on. That trades several understood problems for one that surfaces as stack corruption much
later.

If the loop is ever inverted, it should be an engine change motivated by the web backend, which
needs it far more than a desktop app with one game in flight does — and, on the header's own
account, a smaller change than the threading above makes it look. Not one this client makes.

---

## 3. The cardinal rule

`IGameObserver` hands you `const GameEngine&`. In the terminal you read it directly, because the
callback runs on the same thread as the rendering.

**In Qt you must not do that.** The engine has no locking anywhere, so a GUI-thread read racing the
game thread is a data race, and the failure mode is a torn read of a hand mid-mutation — a card
that is half-removed, or a `std::vector` resized under a loop.

> **Snapshot on the game thread. Emit the snapshot by value. The GUI thread touches nothing but
> its own copy.**

Never emit a signal carrying `const GameEngine&`, `const Round&`, `const Player&`, or a pointer to
anything the engine owns. Signal payloads must be self-contained values.

---

## 4. Components

### 4.0 `GameSnapshot` — the payload, and where it comes from

Everything below moves this type across the thread boundary, and every widget and both threads
touch it, so it is written out in full rather than described as a diff: **a struct this client owns,
modelled on the terminal's `GameView` but not shared with it.**

`refreshFromEngine()` exists only in the terminal, as
`refreshFromEngine(GameView&, const romanian_whist::GameEngine&)` — a different type, in a different
repository. §9 asks whether the engine-derivable half should be promoted into the engine as
`romanian_whist::GameSnapshot` + `snapshotFrom()`, and that question is **still open**. Until it is
answered, `publish()` below calls a *client-side* `refreshFromEngine(GameSnapshot&, const
GameEngine&)` that this client writes by porting `GameView.cpp`'s body. The call site does not
change if §9 later lands; only the function's home does.

The fields are `GameView`'s, minus the terminal-only ones — `message`, `hint`, `error`,
`elapsedMinutes`, `Phase`, and `HandEntry::choice`, which is the number a player types:

```cpp
struct HandEntry
{
    romanian_whist::Card card{};

    // Whether this card can be played on the current trick; false greys it out.
    // On a SNAPSHOT this is always true - see the warning below. Legality that
    // means something arrives on PlayPrompt (4.2).
    bool legal = true;
};

struct SeatView
{
    std::string name;
    unsigned int seatIndex = 0;
    unsigned int bidOrder = 0;         // 1 for whoever opened this round
    bool isHuman = false;

    bool hasBet = false;               // tells a genuine bid of 0 from no bid
    unsigned int bet = 0;
    unsigned int tricksWon = 0;
    unsigned int cardsLeft = 0;
    int roundScore = 0;
    int totalScore = 0;
    unsigned int winStreak = 0;
    unsigned int lossStreak = 0;

    // This seat's hand in display order, filled only for a seat that is NOT the
    // human's and only where canSeeHand() allows - a Forehead round, today.
    std::vector<HandEntry> hand;
};

struct PlayedCardView
{
    unsigned int seatIndex = 0;
    romanian_whist::Card card{};
    bool isWinning = false;            // the engine's ranking, never recomputed
};

struct GameSnapshot
{
    unsigned int roundNumber = 0;      // 1-based
    unsigned int roundCount = 0;
    unsigned int trickCount = 0;
    unsigned int trickNumber = 0;      // 0 between the deal and the first trick
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;

    bool hasTrump = false;             // no trump in an 8-card round
    romanian_whist::Card trump{};

    std::vector<SeatView> seats;

    // -1 is "nobody", which is why these are int rather than Seat: a spectator
    // game has no human seat, nobody is active between turns, and Seat has
    // neither a default constructor nor a sentinel to say either.
    int humanSeat = -1;
    int activeSeat = -1;
    unsigned int leadSeat = 0;         // who LED this trick, not who leads next

    bool hasLeadSuit = false;
    romanian_whist::Suit leadSuit{};
    std::vector<PlayedCardView> table;

    // The human's own hand, display order. For looking at - see below.
    std::vector<HandEntry> hand;

    unsigned int betsPlaced = 0;
    unsigned int betSum = 0;
};
```

Every member is copyable and default-constructible, which §5 requires of a queued payload. Keep it
that way: no references, no `unique_ptr`, no bare `Seat`.

> **The snapshot's `hand` is for looking at. It is never what a player clicks.** Two things follow
> from `refreshFromEngine()`'s body, and a hand widget driven only by snapshots walks into both:
>
> 1. **It carries no legality.** It is built by `layOutHand(hand, {}, trump)`, and an empty legal
>    set is precisely what says "nothing is being chosen here" — so every entry comes back
>    `legal = true` and nothing can be greyed out from a snapshot at all. A legal set exists in
>    exactly one place: inside `QtMoveProvider::playCard()`, which is where `CardValidator` is
>    asked.
> 2. **It is empty in a Forehead or Hidden round**, because `canSeeHand(human, human)` is false in
>    both — and empty in a spectator game, which has no human seat to fill it for.
>
> The second one bites hardest. `endWithForeheadAndHidden` defaults to `true`, so **every default
> game ends with a Forehead round and then a Hidden one** — and in both, a snapshot-driven hand
> widget shows nothing while the engine is parked waiting for a card. No card to click, no way to
> answer, and the only way out of the window is closing it.
>
> **So the hand a player clicks comes from `PlayPrompt` (§4.2)**, which carries its own entries,
> its own legality and its own way back to the engine's indices. The snapshot's `hand` is what is
> drawn, *disabled*, between turns.
>
> The terminal never hits this because `ConsoleMoveProvider::playCard()` re-lays the hand itself at
> prompt time, writing over `view.hand` — the same split between "the hand on screen" and "the hand
> being chosen from", arrived at from the other end.
>
> **It re-lays it unconditionally, though, and this client deliberately does not.**
> `ConsoleMoveProvider::makeBet()` has a `blind` check and clears the hand in a Forehead or Hidden
> round; `playCard()` has no such check, so the terminal prints the player's own card under
> `YOUR HAND` at the play prompt — after a bid that was correctly blind, but before the card
> reaches the table. It is harmless, which is why the terminal is being left as it is: a 1-trick
> round leaves exactly one card and one legal play, so no decision can turn on it, and the decision
> that could have was already made without it. What it costs is the reveal, which is the whole
> point of holding a card on your forehead.
>
> So `PlayPrompt::blind` (§4.2) is not a divergence waiting to be reconciled. It is this client
> getting right a detail the terminal is loose about, and the direction of any future alignment is
> the terminal moving to match this — not this dropping it to match the terminal.

`humanSeat` is an `int` defaulting to `-1`, exactly as `GameView` has it, because **a spectator game
has no human seat and `Seat` cannot say so** — it has no default constructor and no sentinel value.
Phase A is a spectator game, so this matters from the first thing built. It has to be set **before**
`refreshFromEngine()` runs, not after; `publish()` in §4.1 says why.

**The ported `layOutHand()` gains one field, and loses one.** The terminal's returns `entries` in
display order plus `choices`, the hand positions of the numbered *legal subset*. A click lands on a
display slot rather than on a typed number, so this client needs the mapping over the **whole**
hand — which `HandLayout.cpp` already computes, as a local named `ordered`, sorts by, and then
throws away. The port keeps it:

```cpp
struct HandLayout
{
    std::vector<HandEntry> entries;      // display order, the whole hand

    // positions[i] is the index in the ENGINE's hand of the card at display
    // slot i. This is `ordered` in the terminal's HandLayout.cpp, which builds
    // it and then returns only the two things it was used to build.
    //
    // `choices` does not substitute and is gone with the typed number it
    // existed for: it covers the legal subset only, and a display slot that
    // cannot be played still has to know which card it is drawing.
    std::vector<std::size_t> positions;
};
```

### 4.1 `GameBridge` — the observer

Lives on the game thread. Every method runs there.

```cpp
// This client's own pacing knobs. The terminal's Pacer (src/Pacer.h) is a class
// with beat()/trickPause()/roundPause() and a skip flag, not an options struct -
// the delays are the part worth copying, not the type.
//
// Its numbers, for reference: 700ms between plays with a human at the table,
// 250ms in a spectator game (Pacer.cpp, SpectatorBeat), 1200ms of trick dwell.
// The spectator split is worth copying and not just a detail - phase A IS a
// spectator game, and 700ms a card makes a full 181-point schedule a long sit.
struct Pacing
{
    std::chrono::milliseconds beat{700};
    std::chrono::milliseconds spectatorBeat{250};
    std::chrono::milliseconds trickDwell{1200};
};

class GameBridge : public QObject, public romanian_whist::IGameObserver
{
    Q_OBJECT
public:
    // std::optional, because a spectator game has no human seat and Seat has
    // no way to say so (no default constructor, no sentinel). Phase A is a
    // spectator game - see 4.0.
    explicit GameBridge(std::optional<Seat> humanSeat, Pacing pacing);

    // ---- IGameObserver: all called on the GAME thread ----
    // The engine declares thirteen callbacks and this client overrides all
    // thirteen - there is no remainder left defaulted.

    // Fires while the engine is still idle - set up but not yet dealt. The one
    // callback from which run() may legally be called (see section 5).
    //
    // Note WHERE it fires from: the last thing GameEngine::start() does, not
    // the first thing run() does. So this snapshot is emitted before run() is
    // ever entered, and a MainWindow that connects to the bridge after the game
    // thread has been started misses it outright. See section 4.3 - the bridge
    // is built and connected on the GUI thread, before the thread starts.
    void onGameStarted(const GameEngine& e) override { publish(e); }

    // Caches the round type on the way past. PlayContext does NOT carry one the
    // way BetContext does, and QtMoveProvider needs it to know whether this
    // round's hand may be shown at all (4.2). Written here and read there, both
    // on the game thread, so it needs no locking - and it cannot be inferred
    // from the hand size, because a Normal 1-trick round looks identical.
    void onRoundStarted(const GameEngine& e) override
    {
        roundType = e.getCurrentRoundType();
        publish(e);
    }

    // Whose turn it is to bid. Drives the "waiting on Ana" indicator; the human
    // seat's own prompt comes from QtMoveProvider, not from here.
    void onBetRequested(const GameEngine& e, Seat seat) override
    {
        publish(e);
        emit turnChanged(seat.index);
    }

    void onBetPlaced(const GameEngine& e, Seat seat, unsigned int bet) override
    {
        publish(e);
        emit betPlaced(seat.index, bet);
        if(!isHuman(seat)) pace(botBeat());
    }
    void onBettingComplete(const GameEngine& e) override { publish(e); pace(botBeat()); }

    void onTrickStarted(const GameEngine& e, unsigned int trickNumber, Seat leader) override
    {
        publish(e);
        emit trickStarted(trickNumber, leader.index);
    }

    void onCardRequested(const GameEngine& e, Seat seat) override
    {
        publish(e);
        emit turnChanged(seat.index);
    }

    void onCardPlayed(const GameEngine& e, Seat seat, const Card& card) override
    {
        publish(e);
        emit cardPlayed(seat.index, card);
        if(!isHuman(seat)) pace(botBeat());
    }
    void onTrickWon(const GameEngine& e, Seat seat, unsigned int trickNumber) override
    {
        publish(e);
        emit trickWon(seat.index, trickNumber);
        pace(pacing.trickDwell);
    }
    void onRoundScored(const GameEngine& e) override { publish(e); emit roundScored(); }
    void onRoundComplete(const GameEngine& e) override { publish(e); pace(pacing.trickDwell); }

    // Publish first: the final state is what the game-over dialog is drawn over.
    // getWinners() rather than standings.front() - a drawn game has more than
    // one winner, and the engine is what decides that (4.1.0).
    void onGameOver(const GameEngine& e) override
    {
        publish(e);
        emit gameOver(e.getStandings(), e.getWinners());
    }

    // requestStop() was honoured. This, NOT onGameOver, is what a window closed
    // mid-game produces - see section 6. The round it landed in is left
    // unscored, so there is nothing to announce; the window is closing anyway.
    void onGameStopped(const GameEngine&) override { emit gameStopped(); }

signals:
    void stateChanged(GameSnapshot snapshot);      // BY VALUE - crosses a thread boundary
    void turnChanged(unsigned int seat);
    void betPlaced(unsigned int seat, unsigned int bet);
    void trickStarted(unsigned int trickNumber, unsigned int leader);
    void cardPlayed(unsigned int seat, romanian_whist::Card card);
    void trickWon(unsigned int seat, unsigned int trickNumber);
    void roundScored();
    void gameOver(std::vector<romanian_whist::Standing> standings,
                  std::vector<romanian_whist::Standing> winners);
    void gameStopped();

    // Raised by QtMoveProvider through the forwarders below, since it has no
    // signals of its own.
    void betRequested(BetPrompt prompt);
    void cardRequested(PlayPrompt prompt);

public:
    // The seam QtMoveProvider raises its prompts through. Both are called on
    // the GAME thread, from inside the provider.
    //
    // Note `emit provider->bridge->betRequested(...)` WOULD compile - signals
    // have been public since Qt 5. These exist by convention, not because a
    // compiler enforces them: a signal is an announcement its owner makes, and
    // a forwarder is somewhere to say which thread the call arrives on. See 4.2.
    void raiseBetRequested(BetPrompt prompt) { emit betRequested(std::move(prompt)); }
    void raiseCardRequested(PlayPrompt prompt) { emit cardRequested(std::move(prompt)); }

    // Read by QtMoveProvider, on the GAME thread. See onRoundStarted().
    romanian_whist::RoundType currentRoundType() const { return roundType; }

    // Called on the GUI thread, from GameController::stop(). Like the
    // provider's abandon() it touches only a mutex and a condition variable,
    // which is what makes it safe to call on an object that lives elsewhere.
    void cancelPacing();

private:
    void publish(const GameEngine& engine)
    {
        // Snapshotted HERE, on the game thread. See section 3: the GUI thread
        // must never reach into the engine, which has no thread safety at all.
        GameSnapshot snapshot;

        // BEFORE the refresh, not after. Modelled on the terminal's
        // refreshFromEngine(), which fills the human's hand only when the view
        // already names a human seat, and fills each other seat's hand only for
        // a seat this one can see - both are canSeeHand() questions, and
        // canSeeHand() needs a viewer. Its header says as much: what it leaves
        // to the caller is "which seat is active, the human's numbered hand
        // layout, any message or hint, and elapsed wall-clock time".
        //
        // Leave it at its default and every snapshot arrives with an empty
        // hand, the table renders correctly, and the player never sees a card.
        //
        // -1 in a spectator game, which is the default and the correct answer:
        // no seat's hand is filled, and canSeeHand() is never asked a question
        // with no viewer to ask it about.
        snapshot.humanSeat = humanSeat ? static_cast<int>(humanSeat->index) : -1;

        // This client's own refreshFromEngine(GameSnapshot&, const GameEngine&),
        // ported from the terminal's GameView.cpp - NOT a function the engine
        // ships. See 4.0, and 9 for whether it should be.
        refreshFromEngine(snapshot, engine);

        emit stateChanged(snapshot);               // copied into the queued event
    }

    void pace(std::chrono::milliseconds delay);    // interruptible - see section 6

    // Absent in a spectator game, which makes both helpers below one-liners
    // rather than a special case sprinkled through the callbacks.
    bool isHuman(Seat seat) const { return humanSeat && seat == *humanSeat; }

    // THE reason Pacing carries two numbers. With nobody at the table there is
    // nobody to wait for, and 700ms a card makes a full 181-point schedule a
    // very long sit - which is precisely phase A. Without this the spectator
    // number is declared and never reached.
    std::chrono::milliseconds botBeat() const
    {
        return humanSeat ? pacing.beat : pacing.spectatorBeat;
    }

    std::optional<Seat> humanSeat;
    Pacing pacing;

    // Game thread only, written in onRoundStarted and read by the provider.
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;

    // Woken by cancelPacing() so a close does not wait out a trick dwell.
    std::mutex pacingMutex;
    std::condition_variable pacingCv;
    bool pacingCancelled = false;
};
```

**`Seat` is a struct, not an integer.** `Seat{i}` builds one, `seat.index` reads one, and `==` /
`!=` compare them. It has **no default constructor**, so it can never be a queued signal payload —
which is why every signal above carries `unsigned int seat` and every callback body spells
`seat.index`. Comparing seats (`seat == *humanSeat`, behind `isHuman()`) is fine and clearer than
comparing indices.

Because `GameBridge` has affinity to the game thread and `MainWindow` to the GUI thread, Qt's
default `AutoConnection` resolves to `QueuedConnection` and marshals for you — the same job a web
backend's event-loop post would do.

`pace()` inside a callback is deliberate, and mirrors the terminal's `Pacer`: blocking the observer
pauses the game, which is exactly what makes bot turns watchable. One note carried over from the
v4 plan: the terminal found that `Pacer::resetSkip()` belongs at the end of `onBettingComplete`,
not in `onRoundStarted`. If this client grows a skip key, it inherits that.

### 4.2 `QtMoveProvider` — the blocking seam

The two prompts first, in full. They are what a parked provider asks the GUI, and between them they
carry everything an answer needs — so nothing in a click handler has to go looking elsewhere, and
nothing in either points back into the engine:

```cpp
// Both are queued payloads, so both are plain copyable data - section 5.
struct BetPrompt
{
    unsigned int promptId = 0;         // the generation this answers; see submit()
    std::size_t handSize = 0;          // the trick count, and the highest legal bid
    std::optional<unsigned int> forbiddenBet;
    romanian_whist::RoundType roundType = romanian_whist::RoundType::Normal;
    bool isFirstPlayer = false;

    // Empty in a Forehead or Hidden round - see makeBet(). Display order.
    std::vector<HandEntry> hand;
};

struct PlayPrompt
{
    unsigned int promptId = 0;

    // The whole hand in DISPLAY order, with `legal` finally meaning something:
    // this is the one place a legal set exists at all (4.0). Empty in a blind
    // round, on the same rule as BetPrompt::hand.
    std::vector<HandEntry> entries;

    // handIndex[i] is the position in the ENGINE's hand of display slot i, so
    // the click handler has the way back without consulting a snapshot, a
    // cache, or anything that could be one turn out of date.
    //
    // Carried even in a blind round, where `entries` is not: a position names
    // no card, so it leaks nothing, and it keeps the click handler free of a
    // special case.
    std::vector<std::size_t> handIndex;

    // Forehead or Hidden. Both are 1-trick rounds, so the hand is one card and
    // the play is forced - and the player is not entitled to see it. The widget
    // draws handIndex.size() face-down cards and a click on one submits it.
    bool blind = false;
};
```

And the provider itself:

```cpp
// Deliberately NOT a QObject. The engine owns this via unique_ptr and destroys
// it on the game thread; destroying a QObject with GUI-thread affinity from
// another thread is a problem you do not want to debug.
class QtMoveProvider : public romanian_whist::IMoveProvider
{
public:
    QtMoveProvider(GameBridge* _bridge, Seat _seat);

    // ---- called on the GAME thread. Blocks until the GUI answers. ----
    unsigned int makeBet(const BetContext& context) override
    {
        // BetContext has no handSize: the hand's size IS the round's trick
        // count, and so the upper bound on a legal bid.
        //
        // roundType matters here beyond presentation. In Forehead and Hidden
        // the bidder is not entitled to their own hand's contents - the engine
        // still passes the real hand, having no other to give - so the prompt
        // must not show it back to them. Unlike playCard(), this context DOES
        // carry the round type, so nothing has to be cached to know it.
        const bool blind = context.roundType != RoundType::Normal;

        // The hand is left OUT of the prompt in a blind round rather than sent
        // and then not drawn. The engine passes the real hand because it has no
        // other one to give; a prompt that never carries it cannot leak it
        // through a tooltip, a debug overlay or a later refactor.
        //
        // Laid out rather than copied raw, and laid out with no legal set -
        // which is what says "nothing is being chosen here", so every card
        // reads legal and the bidding screen greys nothing. Same display order
        // the play prompt will use, so the cards do not rearrange themselves
        // between bidding and playing.
        std::vector<HandEntry> shown;
        if(!blind)
            shown = layOutHand(context.hand, {}, context.trump).entries;

        std::unique_lock lock(mutex);

        // Asks again rather than trusting the answer. The prompt carries
        // everything needed to build a dialog that cannot produce an illegal
        // bet, so a bad one arriving here is a UI bug - but the ENGINE's
        // response to a bad one is to throw out of run(), which reaches the
        // player as "the game thread failed" (4.3): a widget bug reported as an
        // engine crash, with the game unresumable. Re-prompting costs nothing
        // when the UI is right and is the whole fix when it is not.
        while(true)
        {
            const unsigned int prompt = ++generation;

            bridge->raiseBetRequested(BetPrompt{ prompt,
                                                context.hand.size(),
                                                context.forbiddenBet,
                                                context.roundType,
                                                context.isFirstPlayer,
                                                shown });   // COPIED, not moved: see the loop
            cv.wait(lock, [this, prompt]{ return answeredPrompt == prompt || abandoned; });
            if(abandoned) throw GameAbandoned{};

            // A bet, not a hand index - the one channel carries both, and the
            // generation is what keeps them apart. See `answer` below.
            const unsigned int bet =
                static_cast<unsigned int>(*std::exchange(answer, std::nullopt));

            // GameEngine::isBetLegal() is these two rules and nothing else -
            // no more tricks than there are cards, and never the bet that would
            // make the totals add up. Restated rather than called: the provider
            // holds no engine reference, and section 5 is that the engine is not
            // reachable from anywhere but the callbacks that hand it over.
            if(bet <= context.hand.size() &&
               (!context.forbiddenBet || bet != *context.forbiddenBet))
                return bet;
        }
    }

    std::optional<std::size_t> playCard(const PlayContext& context) override
    {
        CardValidator validator;
        const std::vector<Card> legal =
            validator.getLegalCards(context.hand, context.trump, context.leadSuit);
        if(legal.empty()) return std::nullopt;

        // Display order, real legality, and the way back to the engine's
        // indices - built HERE, on the game thread, out of the hand the engine
        // has just handed over. This is the only point in the program where all
        // three exist at once, which is why the prompt carries them and the
        // snapshot cannot stand in for it (4.0).
        HandLayout layout = layOutHand(context.hand, legal, context.trump);

        // PlayContext carries no roundType, so the bridge cached it in
        // onRoundStarted (4.1). Same rule as makeBet(): a blind round's hand is
        // not sent and then not drawn, it is not sent. Nothing is lost by it -
        // Forehead and Hidden are 1-trick rounds, so there is one card and one
        // legal choice, and what the widget needs is a face-down card to click.
        const bool blind = bridge->currentRoundType() != RoundType::Normal;

        std::unique_lock lock(mutex);

        // Loops for the reason makeBet() does, and here the legal set to check
        // against is already sitting in a local.
        while(true)
        {
            const unsigned int prompt = ++generation;

            bridge->raiseCardRequested(PlayPrompt{ prompt,
                                                   blind ? std::vector<HandEntry>{}
                                                         : layout.entries,
                                                   layout.positions,
                                                   blind });   // COPIED: see the loop
            cv.wait(lock, [this, prompt]{ return answeredPrompt == prompt || abandoned; });
            if(abandoned) throw GameAbandoned{};

            // Already an index into context.hand, because the click handler
            // translated through PlayPrompt::handIndex before submitting - see
            // 4.4. IMoveProvider is explicit that this is an index into the hand
            // AS GIVEN, not into any sorted or filtered view of it.
            //
            // std::exchange clears it. Leaving it set means the next turn
            // returns this same index without ever waiting, and the engine
            // throws on the illegal play - see README.md, "Abandoning a game
            // whose provider is parked".
            const std::size_t chosen = *std::exchange(answer, std::nullopt);

            // The prompt marked each slot legal or not and the widget is meant
            // to refuse the rest; this is what runs when it does not. The
            // bounds check is not paranoia either - handIndex is the only
            // sanctioned way to produce this number, and a widget that indexes
            // it with a stale display slot produces one that is out of range.
            //
            // By value, because a deck holds no duplicates - the same identity
            // argument layOutHand() makes.
            if(chosen < context.hand.size() &&
               std::find(legal.begin(), legal.end(), context.hand[chosen]) != legal.end())
                return chosen;
        }
    }

    // ---- called on the GUI thread, straight from a click handler ----
    //
    // `forPrompt` is the generation the widget was drawn for, carried through
    // BetPrompt/PlayPrompt and handed back. A submission for any other
    // generation is dropped.
    //
    // Without that gate a click landing when nothing is parked - a stray click
    // during a bot's turn, a double click, a dialog accepted twice - sets
    // `answer`, and the NEXT prompt returns from cv.wait() immediately with an
    // index chosen for a different hand. That is the same failure the
    // std::exchange below prevents, arriving from the other direction, and
    // disabling the hand widget between turns narrows the window without
    // closing it.
    void submit(unsigned int forPrompt, std::size_t choice)
    {
        {
            std::lock_guard lock(mutex);
            if(forPrompt != generation || answeredPrompt == generation)
                return;                      // stale, or already answered

            answer = choice;
            answeredPrompt = forPrompt;
        }
        cv.notify_one();
    }

    void abandon()
    {
        { std::lock_guard lock(mutex); abandoned = true; }
        cv.notify_one();
    }

private:
    GameBridge* bridge;                       // non-owning; outlives this
    Seat seat;
    std::mutex mutex;
    std::condition_variable cv;

    // One channel for two kinds of answer: a bet, and an index into a hand.
    // They never overlap - the engine asks for one at a time - and the
    // generation is what keeps an answer to one from being read as an answer to
    // the other.
    unsigned int generation = 0;              // bumped per prompt raised
    unsigned int answeredPrompt = 0;          // the generation `answer` answers
    std::optional<std::size_t> answer;
    bool abandoned = false;
};
```

**And the object the engine actually owns is not that one.** `GameSetup::seats[i].moveProvider` is
a `std::unique_ptr<IMoveProvider>`, `GameEngine::start()` rejects a null one outright, and whatever
goes in there is destroyed with the engine on the game thread — which is exactly what §4.3 refuses
to let happen to `QtMoveProvider`. So the seat gets a forwarder that owns nothing:

```cpp
// What the engine holds. Ten lines, and every one of them exists so that the
// real provider can outlive the engine (4.3): a click landing while the
// game-over dialog is up finds an object the controller still owns, rather than
// one destroyed on another thread half a second earlier.
//
// Not a QObject either, for the same reason QtMoveProvider is not: this is
// destroyed on the game thread, inside ~GameEngine.
class SeatProvider : public romanian_whist::IMoveProvider
{
public:
    // Non-owning by construction - there is no unique_ptr overload to reach
    // for by accident, which is the whole point of the type.
    explicit SeatProvider(QtMoveProvider* _target) : target(_target) {}

    unsigned int makeBet(const BetContext& context) override
    {
        return target->makeBet(context);
    }

    std::optional<std::size_t> playCard(const PlayContext& context) override
    {
        return target->playCard(context);
    }

private:
    QtMoveProvider* target;    // owned by GameController; outlives the engine
};
```

`GameController::start()` is what fills the human seat in, with
`std::make_unique<SeatProvider>(movePlayer.get())` — see §4.3. A spectator game builds none of
this; its seats are all `AiMoveProvider`.

`submit()` needs no signal or slot. It touches only a mutex, so calling it directly from a click
handler on the GUI thread is safe — and skipping Qt's event queue means the game thread wakes
immediately.

**Both prompts are raised while this mutex is held**, which is safe for exactly one reason: the
connection is queued, so emitting to an object on another thread posts an event and returns.
Nothing else about it is safe. If that connection is ever made `Qt::DirectConnection`, or the bridge
is somehow still on the GUI thread when a prompt is raised, the slot runs *here*, on the game
thread, calls `submit()`, and deadlocks on a mutex that is not recursive. The same applies to
`Qt::BlockingQueuedConnection` on any bridge signal, from either direction: `GameController::stop()`
already joins the game thread from the GUI thread, so a blocking connection the other way is a
two-way wait. **Leave every bridge connection on `AutoConnection`** — §4.3's ordering is what makes
that resolve correctly — and never pass an explicit connection type to one.

**Emitting a signal from a non-QObject compiles.** `emit bridge->betRequested(...)` is legal:
signals have been `public` since Qt 5, where `qtmetamacros.h` defines `Q_SIGNALS` that way
precisely so `&Class::signal` works in the pointer-to-member `connect` syntax. Nothing below is
enforced by a compiler (Appendix A.2).

The `raiseBetRequested()` / `raiseCardRequested()` forwarders on `GameBridge` (§4.1) stay anyway,
because Qt's own guidance is that a signal is an announcement its owner makes and no other object
should make on its behalf — and a forwarder is the one place to put a comment saying which thread
the call arrives on. It is a convention, not a diagnostic, so it has to be held to deliberately.
Declaring `QtMoveProvider` a friend would also work; the forwarder is cleaner.

**`GameAbandoned` is the README's name for this**, and it is worth matching:
the engine's `README.md`, "Abandoning a game whose provider is parked", documents wake-and-throw as the
supported way out of a parked provider, with this exact shape. `std::nullopt` is not an
alternative — the engine reads it as "no legal play", which it treats as a bug.

### 4.3 `GameController` — owns the thread

```cpp
class GameController : public QObject
{
    Q_OBJECT
public:
    // TWO calls, not one, and the split is load-bearing. prepare() builds the
    // bridge - and a provider, if there is a human seat - on the GUI thread and
    // hands them back; the caller connects; only then does start() spawn the
    // thread.
    //
    // Doing it in one call cannot work: onGameStarted() fires from inside
    // GameEngine::start(), so the first stateChanged() is emitted before run()
    // is entered, and a MainWindow that connects afterwards has already missed
    // it. A queued connection made after the emit does not receive it.
    //
    // A SPECTATOR GAME GETS NO PROVIDER: there is no human seat for one to
    // serve. That is not an edge case to bolt on later - phase A is a spectator
    // game, so a null provider is the first state this class is ever in, and
    // section 6's stop() null-checks it for exactly that reason.
    //
    // Clears stopRequested, under engineMutex, before it builds anything. The
    // previous game's stop set it; leaving it set stops the next one before it
    // starts. Safe here and nowhere else: stop() has already joined the thread
    // by the time a second prepare() is called, so there is no run slot to race.
    void prepare(std::optional<Seat> humanSeat, Pacing pacing);  // nullopt = spectator, phase A

    // GameSetup is MOVE-ONLY - it owns the seats' unique_ptr<IMoveProvider> -
    // so the call site reads start(std::move(setup)), and the setup is moved
    // again into the run slot's captures. It cannot travel through a queued
    // signal, which is why it goes into the lambda rather than a slot argument.
    //
    // The human seat's moveProvider is left NULL by the caller and filled in
    // here, with std::make_unique<SeatProvider>(movePlayer.get()) - the
    // non-owning forwarder of 4.2, NOT the QtMoveProvider itself, which this
    // class keeps. GameEngine::start() rejects a null provider, so the seat
    // cannot simply be left empty. Note the caller's own
    // GameSetup is gutted by the move whatever happens, so a start() that
    // throws needs a freshly built setup to retry - not because the engine kept
    // anything (GameEngine::start() validates before it applies, and a rejected
    // one leaves the engine untouched and startable) but because a move-only
    // type passed by value is spent at the call site.
    void start(GameSetup setup);
    void stop();                                  // see section 6

    // Calls stop(). Not a tidiness measure: `gameBridge` is a QObject with
    // affinity to the game thread, and destroying it from the GUI thread is
    // only defined once that thread has been joined. stop() is what joins it,
    // and it is idempotent and safe on a game that never started or has already
    // finished - so calling it unconditionally here costs nothing and closes
    // the one path (a controller destroyed without a window close) that would
    // otherwise skip it.
    //
    // This is the mirror of why QtMoveProvider is not a QObject at all, below:
    // same hazard, opposite thread.
    ~GameController();

    GameBridge* bridge() const;                   // built by prepare(); connect to it before start()

    // NULL in a spectator game - see prepare(). Every caller checks.
    //
    // Owned HERE, not by the engine, and that is a lifetime fix rather than a
    // style choice. The engine owns whatever is in GameSetup and destroys it
    // with itself on the game thread - so a provider owned by the engine is a
    // dangling pointer the moment run() returns on its own, and a click that
    // arrives while the game-over dialog is up is a use-after-free that no
    // amount of stopping protects against. The engine gets a SeatProvider
    // instead (4.2), which forwards to this and owns nothing; the real object
    // outlives the thread.
    QtMoveProvider* provider() const;

signals:
    // The run slot caught something. NEITHER onGameStopped() NOR onGameOver()
    // fires on that path (section 6), so this signal is the only way the GUI
    // learns the game ended - and without it the exception escapes a slot
    // invoked from QThread's event loop, which is std::terminate with no
    // message. See the catch in step 4 below.
    void gameFailed(QString what);

private:
    QThread thread;
    std::unique_ptr<GameBridge> gameBridge;       // built on the GUI thread, then moveToThread()
    std::unique_ptr<QtMoveProvider> movePlayer;   // null in a spectator game; see above

    // The engine is constructed, used and destroyed on the game thread, so the
    // GUI thread may not read this pointer unguarded - stop() would otherwise
    // race the run slot's construction (and find it null if the window is
    // closed in the gap between thread.start() and the slot running) and its
    // destruction. Published under the mutex once built, withdrawn under it
    // before destruction, and stop() calls requestStop() while holding it.
    // Cheap: the game thread takes this lock exactly twice per game.
    std::mutex engineMutex;
    GameEngine* engine = nullptr;

    // The stop that arrived before there was an engine to tell. Guarding the
    // pointer stops the crash and loses the REQUEST: the engine's own
    // stopRequested is a member of an object that does not exist yet, so there
    // is nothing for stop() to set and nothing to re-read later. Latched here
    // instead, under the same mutex, and honoured by the run slot the moment it
    // publishes the pointer (step 4 below).
    //
    // CLEARED BY prepare(), and that is not housekeeping. A controller is
    // reused for a second game (phase C), and the stop that ended the first one
    // set this - so a flag left latched is read back by game two's run slot,
    // which requests a stop before the first card is dealt and ends it at the
    // first boundary. That is QtMoveProvider::abandoned's bug exactly (section
    // 8, phase C), in the object one level up: a game that appears to deal
    // itself and stop. prepare() is where it belongs because prepare() is
    // already where everything else per-game is rebuilt.
    bool stopRequested = false;
};
```

Use the **worker-object pattern**, not a `QThread` subclass: create the objects, `moveToThread()`
them, connect `QThread::started` to a slot that runs the game.

The engine itself must be constructed *inside* that slot, so it is born, played and destroyed
entirely on the game thread. This is not a preference: the engine's `README.md`, "Threading, stopping,
and failure" opens with **one thread per engine, and no internal locking**, and `requestStop()` is
the only member of the whole class that is safe to touch from elsewhere.

**The order the whole thing hangs on**, since three separate bugs live in getting it wrong:

1. `prepare()` builds `GameBridge` and `QtMoveProvider` **on the GUI thread**.
2. `MainWindow` connects to every bridge signal (§4.4).
3. `start()` moves the bridge to the game thread and starts it.
4. The run slot, **on the game thread**, constructs the engine, calls `addObserver(bridge)`, calls
   `engine.start(std::move(setup))` — which fires `onGameStarted()` into connections that already
   exist — publishes the engine pointer under `engineMutex`, **and, still holding that lock, reads
   `stopRequested` back and calls `engine.requestStop()` if it is set**, before calling `run()`.
5. On the way out, still on the game thread: withdraw the pointer under the lock, then destroy the
   engine. The provider is not destroyed with it; it belongs to the controller.

**Step 4's read-back is the whole of the fix for a stop that lands in the gap**, and it is one
`if` in the one place that can perform it:

```cpp
GameEngine game;                  // the local the game thread owns; `engine` is
                                  // the member the GUI thread may read
// ... addObserver(gameBridge.get()), game.start(std::move(setup)) ...

{
    std::lock_guard lock(engineMutex);
    engine = &game;
    if(stopRequested)             // stop() ran before there was an engine to tell
        game.requestStop();       // honoured at the first boundary run() reaches
}
game.run();
```

Publishing and reading back **under one lock** is what closes it: a `stop()` that arrives before
the lock is taken sets the flag and is read here, and one that arrives after it finds the pointer
and calls `requestStop()` itself. There is no third ordering, which is the only reason this is
two lines rather than a handshake.

Without it, `stop()` in that window is silently dropped — §6's null check saves it from
dereferencing nothing and leaves it with nothing to do. `abandon()` and `cancelPacing()` survive the
gap because both are latches on objects `prepare()` already built; **`requestStop()` is the only one
of the three with nowhere to land**, and it is the one that ends the game.

**What that costs is worst in phase A**, which is why this belongs in the phase that looks simplest.
A spectator game has no provider, so the latched `abandon()` that rescues phase B is not there;
`cancelPacing()` still lands and switches every beat off. The game then replays its whole schedule
unpaced, into a window that is closing, while the GUI thread waits in `thread.wait()` — and it ends
through `onGameOver()`. That is exactly the assertion phase A exists to make (§8), failing
intermittently and in the shape of "§6 was implemented backwards" rather than of a lost flag.

**The whole of step 4 goes in a `try`, and the `catch` is a catch-all.** Not only `GameAbandoned`:

```cpp
try
{
    // construct, addObserver, start, publish the pointer + honour a stop
    // already asked for, run()
}
catch(const GameAbandoned&)
{
    // The documented way out of a parked provider (4.2). Expected, not a
    // failure - the window is closing. Nothing to report.
}
catch(const std::exception& error)
{
    emit gameFailed(QString::fromUtf8(error.what()));
}
catch(...)
{
    emit gameFailed(tr("the game thread failed"));
}
// then, on every path: withdraw the engine pointer under the lock and destroy it
```

`GameAbandoned` is the only *expected* throw, and it is nowhere near the only possible one. A bet
the UI failed to constrain, a `std::logic_error` out of the engine's own re-entrancy guards, a
`std::bad_alloc` — the engine is explicit that a provider or observer throwing leaves it mid-round
and unresumable, and says to destroy it, which is what step 5 does either way. What is not optional
is catching: **an exception escaping a slot invoked from `QThread`'s event loop is `std::terminate`,
with no message and no stack anyone will read.** The one `catch(...)` is the difference between a
dialog and a silent abort.

`addObserver()` is not thread-safe either, so step 4 is the only place it may be called — before
`start()`, and on the thread that will own the engine. The Qt connections from step 2 are what make
that safe to do late: a `QObject` may be connected before it has been moved to its final thread, and
the connection type is resolved per emission, not at connect time.

### 4.4 `MainWindow`

```cpp
// All of this runs between GameController::prepare() and GameController::start(),
// on the GUI thread, while the bridge still lives here. Connecting after the
// thread is running loses onGameStarted's snapshot - see section 4.3.
connect(bridge, &GameBridge::stateChanged,  this, &MainWindow::render);
connect(bridge, &GameBridge::betRequested,  this, &MainWindow::showBetPrompt);
connect(bridge, &GameBridge::cardRequested, this, &MainWindow::showPlayPrompt);
connect(bridge, &GameBridge::trickWon,      this, &MainWindow::flashTrickWinner);
connect(bridge, &GameBridge::gameOver,      this, &MainWindow::showStandings);
// NOT MainWindow::close directly: closing the window is what calls stop(),
// stop() is what produces gameStopped, and close() would then re-enter
// closeEvent(). The slot checks a `closing` flag first and only forces the
// close when the stop came from somewhere else.
connect(bridge, &GameBridge::gameStopped,   this, &MainWindow::onGameStopped);
// From the CONTROLLER, not the bridge: the throw happens outside run(), so no
// observer callback fires and nothing on the bridge knows about it (4.3, 6).
connect(controller, &GameController::gameFailed, this, &MainWindow::showFailure);

// Field names follow the terminal's GameView (src/GameView.h), which is what
// this type is modelled on - see section 9.
void MainWindow::render(const GameSnapshot& s)      // a copy; the engine is untouched
{
    roundLabel->setText(tr("Round %1 of %2").arg(s.roundNumber).arg(s.roundCount));
    trumpWidget->setCard(s.hasTrump, s.trump);      // no trump in an 8-card round
    for(const SeatView& seat : s.seats)
        seatPanels[seat.seatIndex]->update(seat);
    trickWidget->setCards(s.table);                 // PlayedCardView: seat, card, isWinning

    // DISABLED, always. A snapshot's hand is what the player looks at between
    // turns; it carries no legality and it is empty in a blind round, so it can
    // never be the thing they click (4.0). showPlayPrompt() is what makes a
    // hand clickable, and it draws its own.
    handWidget->setEntries(s.hand);
    handWidget->setEnabled(false);
}

// The engine is parked in playCard() until submit() answers. Everything needed
// to answer it is in the prompt - which is the point of the prompt.
void MainWindow::showPlayPrompt(const PlayPrompt& prompt)
{
    activePrompt = prompt;                          // by value, for the click below

    if(prompt.blind)
        // Forehead or Hidden: one card, forced, and not ours to look at. A
        // face-down back is still a button.
        handWidget->setFaceDown(prompt.handIndex.size());
    else
        // Legality that means something: illegal cards dimmed, unclickable.
        handWidget->setEntries(prompt.entries);

    handWidget->setEnabled(true);
}

void MainWindow::onCardClicked(std::size_t displaySlot)
{
    // Legality, checked HERE and again in playCard(). Not redundant: this one
    // is the UI's own rule about what a click means, the other is the provider
    // refusing to hand the engine something it will throw on. A blind round has
    // no entries at all and every slot is playable, which is why the check is
    // written around `entries` being empty rather than assuming it is filled.
    if(!activePrompt.entries.empty() && !activePrompt.entries[displaySlot].legal)
        return;

    handWidget->setEnabled(false);                  // until the next prompt

    // Display slot -> the engine's own hand position, through the mapping that
    // travelled with this prompt. Not through a snapshot, and not through
    // anything cached from one: the prompt cannot be a turn out of date with
    // itself.
    //
    // Disabling the widget is not enough on its own - the promptId is what
    // actually gates a stale click. See submit() in 4.2.
    provider->submit(activePrompt.promptId, activePrompt.handIndex[displaySlot]);
}

// The other half of the same channel, and the one place in the client where a
// bet and a hand index are the same `std::size_t` going to the same function.
// There is no type to tell them apart - `submit(promptId, 3)` is a bet of three
// or the fourth card depending only on which prompt is live - so the two call
// sites are kept apart by name and by widget instead, and neither is reachable
// from the other's prompt.
void MainWindow::showBetPrompt(const BetPrompt& prompt)
{
    activeBetPrompt = prompt;                       // by value, same as the play prompt

    // NOT a modal exec() - see below. A panel, enabled and disabled like the
    // hand is.
    betPanel->setRange(0, static_cast<unsigned int>(prompt.handSize));
    betPanel->setForbidden(prompt.forbiddenBet);    // disabled, with a tooltip saying why
    betPanel->setHand(prompt.hand);                 // empty in a blind round (4.2)
    betPanel->setEnabled(true);
}

void MainWindow::onBetChosen(unsigned int bet)
{
    betPanel->setEnabled(false);
    provider->submit(activeBetPrompt.promptId, bet);
}
```

> **The bet prompt is a panel, not a modal dialog, and that is a shutdown
> decision rather than a taste one.** `QDialog::exec()` spins a nested event loop on the GUI thread.
> That is safe for the engine in a way §2's rejected middle path is not — the engine is on the other
> thread and no engine frame is on this stack — but it is the state the window is in for exactly as
> long as the game thread is parked in `makeBet()`, which is unbounded. A modal dialog takes the
> close button with it: `MainWindow::closeEvent` does not run while it is up, so `stop()` is never
> called, so `abandon()` never wakes the provider. **That is precisely the path Phase B's shutdown
> test exercises**, and with a modal bet dialog the test cannot even be performed.
>
> If a modal dialog is wanted anyway, the close has to be routed around it: `QDialog::reject()` on
> the main window's close, then `stop()`, in that order. A panel needs none of that, which is why
> §7 specifies one.

> **Three orderings meet in the hand widget, and no two of them coincide.** The terminal hit this
> first and solved it in `HandLayout` (`src/HandLayout.h`), whose header comment enumerates them:
>
> 1. the hand as the **engine** holds it — deal order, and what `playCard()`'s index must refer to;
> 2. **display** order — trump suit first, then a fixed alternating-colour order, high rank first;
> 3. the **clickable legal subset**, which is what the player actually picks from.
>
> `layOutHand()` returns cards in display order plus `choices`, where `choices[n - 1]` is the hand
> position for the number the player types. **A GUI needs the mapping one step earlier:** a click
> lands on a display slot, not on a typed number, so what this client wants is (2) → (1) over the
> whole hand — `PlayPrompt::handIndex` — with (3) reduced to a `legal` flag that decides whether the
> slot is clickable at all. `HandEntry` already carries that flag.
>
> Same idea, one indirection fewer. Carrying positions rather than cards is the whole trick:
> sorting for display is fine and desirable, and it is only unsafe if the way back is not carried
> with it. Getting it wrong means the player clicks one card and plays another, which no rule in
> the engine would object to.
>
> **And the way back travels with the prompt, not with the snapshot.** Putting `handIndex` on
> `GameSnapshot` and letting `render()` cache it looks equivalent — queued events are delivered in
> emission order, so the `stateChanged` carrying a hand always arrives before the `cardRequested`
> carrying its `promptId` — but ordering was never the hard part: a snapshot's hand carries no
> legality and is *empty* in the two round types that end every default game, so it could not have
> been clicked in the first place (§4.0). One prompt carrying entries, legality and positions
> together is both the simpler object and the only one that is ever complete (Appendix A.4).

---

## 5. Qt specifics worth getting right

**Register the metatypes.** Queued connections need custom payload types registered:

```cpp
Q_DECLARE_METATYPE(GameSnapshot)
Q_DECLARE_METATYPE(romanian_whist::Card)
Q_DECLARE_METATYPE(BetPrompt)
Q_DECLARE_METATYPE(PlayPrompt)

// Easy to miss, because it is the only payload that is not one of this
// client's own types: gameOver() carries TWO of these across the boundary.
Q_DECLARE_METATYPE(std::vector<romanian_whist::Standing>)

// in main(), before any connect():
qRegisterMetaType<GameSnapshot>("GameSnapshot");
qRegisterMetaType<std::vector<romanian_whist::Standing>>("std::vector<romanian_whist::Standing>");
```

**Qt 6 changed what these are for.** `QMetaType` is built from any complete type now, so a queued
connection usually resolves a custom payload with neither line present, and **omitting
`Q_DECLARE_METATYPE` is not a compile error** (Appendix A.3).

Write both anyway. What has not changed is the failure mode wherever auto-registration does not
reach: the silent *"cannot queue argument of type X"* on stderr, and a signal that quietly never
arrives. Two lines, against a dropped event with no crash to point at.

**Payload types must be copyable and default-constructible.** `GameSnapshot` already is — it is
plain data. Keep it that way; do not add references or `unique_ptr` members to it. `Card` qualifies
too (it has a default constructor). **`Seat` and `Standing` do not**: `Seat` is declared
`explicit constexpr Seat(unsigned int)` with no default, and `Standing` holds one. So a bare `Seat`
can never be a queued argument — carry `seat.index` instead. A `std::vector<Standing>` is fine,
because the *vector* is default-constructible.

**The GUI thread calls no engine method — none.** Not a getter, not `getStandings()`. Everything it
needs arrives in a snapshot. `requestStop()` is the sole exception in the entire class, and even
`addObserver()`/`removeObserver()` are explicitly not thread-safe.

**The engine also guards against re-entrancy, and a GUI can trip it.** Both throw
`std::logic_error` rather than corrupt state:

- `addObserver()` / `removeObserver()` from inside any callback — the engine is mid-iteration over
  its observer list. An observer that wants to detach sets a flag and is detached between rounds.
- `run()` / `playRound()` from a move provider, or from any callback except `onGameStarted()`. A
  callback that wants the game to end calls `requestStop()`.

**Do not subclass `QThread`.** Worker-object pattern.

**Nothing can be sent *into* the game thread by signal, ever.** The run slot sits inside `run()`
for the whole game, so that thread's event loop never spins and a queued connection targeting
`GameBridge` is delivered only once the game has ended — which is to say, never usefully. This
costs nothing today, because the one inbound path is `QtMoveProvider::submit()` and it deliberately
goes around Qt entirely. But it decides how anything later has to be built: **a pause button, a
skip key, a "deal faster" control and an animation-finished handshake are all mutex-and-condition-
variable affairs on the model of `submit()` and `cancelPacing()`, not slots.** Phase D is where
this comes due.

**`stateChanged` is not coalesced, and today it does not need to be.** Every one of the twelve
publishing callbacks deep-copies a whole `GameSnapshot` — six names, six hands, the table — into a
queued event, and nothing merges two of them. That is affordable only because `pace()` blocks the
game thread right after most of them: the queue drains during a beat, and the game thread is the
slower of the two by construction. **Phase D's pacing controls are what remove that.** A skip key
or a "deal faster" control shortens or deletes the beat, and the same twelve publishes per trick
then arrive with nothing throttling them — on a thread that, per the paragraph above, cannot be
signalled back to ask it to slow down. The fix when it comes is on the emitting side, not the
receiving one: publish on state that actually changed, or drop the intermediate snapshot when
another is already queued. Budget for it with the animation handshake; it is the same phase and the
same cause.

**Signals from the game thread are safe; direct method calls on GUI objects are not.** Never call
`widget->setText()` from the game thread. If you find yourself wanting to, you need another signal.

---

## 6. Shutdown and cancellation

This is the part that bites, and it is worth writing first rather than last.

Closing the window while the game thread is parked inside `cv.wait` needs both halves:

```cpp
void GameController::stop()
{
    {
        // Under the lock, and null-checked: the engine is built by the run slot
        // on the other thread, so a window closed in the gap between
        // thread.start() and that construction finds nothing here - and one
        // closed after a game that ended on its own would otherwise dereference
        // a pointer the game thread has already destroyed. requestStop() is the
        // one engine member callable from here at all.
        std::lock_guard lock(engineMutex);

        // SET FIRST, and set unconditionally. The null case is not "nothing to
        // do", it is "nobody to tell yet": there is no engine whose flag could
        // be raised, so the request is recorded here and the run slot honours it
        // under this same lock the moment it publishes the pointer (4.3).
        // Dropping it instead is the one path on which a closed window lets the
        // game play itself out to onGameOver().
        stopRequested = true;

        if(engine != nullptr)
            engine->requestStop();   // ends cleanly at the next round, bid or trick boundary
    }

    // NULL-CHECKED, and not defensively: a spectator game has no human seat and
    // so no provider at all (4.3), and phase A is a spectator game - so the
    // very first game this client ever plays reaches this line with nothing
    // there. An unguarded call here crashes on closing phase A's window, which
    // is the one thing phase A exists to verify.
    if(movePlayer)
        movePlayer->abandon(); // unblocks a provider parked mid-prompt

    // Guarded too, for a narrower reason: prepare() builds it, so this is null
    // only if stop() is reached before prepare() ever ran - a window closed
    // during setup, or ~GameController() on a controller that was never used.
    if(gameBridge)
        gameBridge->cancelPacing(); // unblocks an interruptible pace(), which RETURNS - see below

    // Note quit() does nothing until run() has returned: the slot is blocking
    // that event loop, which is the whole point of the calls above. They are
    // what ends the game; this pair only joins the thread afterwards.
    thread.quit();
    thread.wait();             // join BEFORE anything else is destroyed
}
```

**`stop()` is called more than once and must not care.** A window close calls it, and then
`~GameController()` calls it again (§4.3). Everything above is idempotent already — a second
`requestStop()` on a stopped game is a no-op the engine documents, `abandon()` and `cancelPacing()`
only re-set flags that are already set, and `quit()`/`wait()` on a finished thread return at once.
Do not add a `stopped` flag to guard it; there is nothing to guard.

`stopRequested` is not that flag and does not make this one. It gates nothing here — `stop()` sets
it every time, unconditionally — and it is read on the other thread, once, by a run slot that has
just published the engine. Setting an already-set bool is exactly as idempotent as the three calls
above it.

`stop()` must be safe to call **when the game is already over**, because that is the ordinary case:
the player reads the standings and then closes the window. By then the run slot has returned and the
engine is destroyed, so every line above is either guarded or operating on an object the controller
still owns. That is why §4.3 moved the provider's ownership out of the engine — with the engine
owning it, `movePlayer->abandon()` on this path is a use-after-free, and so is any click that lands
while the game-over dialog is up.

It must also be safe to call **when the game never started**, and safe when there is **no provider
at all**. Three separate pointers, three separate reasons, and only one of them was ever about the
engine:

| Pointer | Null when | Guarded because |
|---|---|---|
| `engine` | before the run slot constructs it, and after it destroys it | it lives on the other thread; read under `engineMutex` |
| `movePlayer` | **every spectator game — phase A** | there is no human seat for a provider to serve |
| `gameBridge` | `stop()` before `prepare()` | a window closed during setup, or an unused controller destroyed |

**A guarded pointer is not the same as a handled case, and `engine` is where the two come apart.**
The other two rows are objects `prepare()` built on this thread, and `abandon()` and
`cancelPacing()` are latches on them: called before the game thread ever looks, they are still set
when it does. `requestStop()` has no such object to latch on — the flag it raises is a member of an
engine the run slot has not constructed yet — so skipping the call on null does not defer the
request, it discards it. `stopRequested` on the controller (§4.3) is the latch the other two already
had, and step 4's read-back is where it is spent.

`requestStop()` alone is not enough — if the game thread is blocked waiting for a human bet, it
never reaches a boundary at which to check the flag. The engine says so itself: *"It cannot
interrupt a move provider already parked waiting for a human... Unparking that provider is the
client's job, and throwing from it is the supported way out."* Equally, `abandon()` alone leaves a
thread sleeping in `pace()` for up to `trickDwell`.

**Which of the two lands decides what the observer sees**, and this is worth getting straight:

| How it ended | What fires | State |
|---|---|---|
| `requestStop()` caught at a boundary — **the ordinary case, including a close landing in `pace()`** | `onGameStopped()` — **not** `onGameOver()` | status `Stopped`, round left unscored, engine not resumable |
| `GameAbandoned` thrown from a parked provider — only a close landing on a human's turn | nothing; the exception unwinds `run()` | engine mid-round, not resumable |
| The game simply finished | `onGameOver()` | status `Finished` |

Which row you land on is decided by where the game thread was standing, and `pace()` returning
rather than throwing is what keeps the first row the common one — see below.

So a window closed mid-game produces `onGameStopped()` or an exception, and *never*
`onGameOver()` — a client that only handles `onGameOver()` is silent on the exact path it was
written for. Note also that a stop requested once the final round has been scored is a **no-op**:
the game stays `Finished` and `onGameOver()` is what fired.

`pace()` must be an interruptible wait, not `QThread::sleep` or `std::this_thread::sleep_for`:

```cpp
void GameBridge::pace(std::chrono::milliseconds delay)
{
    std::unique_lock lock(pacingMutex);
    pacingCv.wait_for(lock, delay, [this]{ return pacingCancelled; });

    // RETURNS on cancel. Does NOT throw - see below, this is deliberate and it
    // is the difference between the two rows of the table above.
}
```

> **`pace()` returns; only the provider throws.** The distinction is the whole reason the table
> above has two rows, and getting it backwards makes the clean path unreachable.
>
> The provider has no choice: `IMoveProvider` has no non-throwing way to say "abandon", because
> every other return value is a move and `std::nullopt` means "no legal play", which the engine
> treats as a bug. Throwing is the engine's own documented way out, and the cost is an engine left
> mid-round and not resumable.
>
> **`pace()` has a choice, and should take the other one.** Returning early puts the game thread
> back in `GameEngine::run()` within microseconds, at which point it reaches its next round, bid or
> trick boundary, reads the flag `stop()` already set, and ends through `onGameStopped()` — status
> `Stopped`, engine cleanly finished rather than unwound mid-frame.
>
> If `pace()` threw instead, **the clean path would almost never run.** A spectator game spends
> nearly all of its wall-clock time asleep in `pace()` — 250ms after every play, 1200ms after every
> trick and every round, with bot decisions taking no measurable time in between — so
> `cancelPacing()` would win the race against `requestStop()` essentially every time, and phase A's
> shutdown test would be verifying the exception path while claiming to verify the clean one. The
> exception path stays reserved for the case that genuinely needs it: a provider parked on a human,
> which is phase B.

When `GameAbandoned` *is* thrown — from the provider — it propagates through `GameEngine::run()` and
must be caught in the thread's run slot. Per the engine's own contract, an engine that throws out of
`run()` is **not resumable**: catch it outside `run()`, destroy the engine, and restore whatever you
set up before the call, because nothing else will. Neither `onGameStopped()` nor `onGameOver()`
fires on that path, so the run slot is the only place that knows the game ended — which is what
`GameController::gameFailed` (§4.3) exists to say.

**And it is not the only thing that can be thrown there.** §4.3's `catch` is a catch-all for the
reason the table above has three rows rather than one: the interesting failure is always the one
nobody wrote a row for. A bet the UI failed to constrain, a `std::logic_error` out of the engine's
re-entrancy guards, a `std::bad_alloc` — all of them unwind `run()` exactly as `GameAbandoned` does,
all of them leave the engine unresumable in the same way, and any of them escaping the slot is
`std::terminate` rather than a message. `GameAbandoned` is caught separately only because it is the
one that is *expected*, and so the one that reports nothing.

---

## 7. UI surface

Minimum to play a full game:

| Widget | Reads from snapshot |
|---|---|
| Setup dialog | *(writes)* builds a `GameSetup` — names, bot levels, `structure`, `endWithForeheadAndHidden`, `all1GamesAreForehead`, and optionally `shuffleSeed`. `SetupWizard.h`'s `GameConfig` is the shape to copy |
| Seat panel × N | `seats[i]`: `name`, `bet` (only when `hasBet`), `tricksWon`, `cardsLeft`, `roundScore`/`totalScore`, `winStreak`/`lossStreak`, `bidOrder` |
| Trick area | `table` (`PlayedCardView`: `seatIndex`, `card`, `isWinning`) in play order; trump from `hasTrump`/`trump` |
| Hand | Two sources, never one. Between turns: `hand` in display order, drawn **disabled** — a snapshot carries no legality and is empty in a blind round (§4.0). On your turn: `PlayPrompt::entries`, illegal cards dimmed and non-clickable — or `handIndex.size()` face-down cards when `blind`. Other seats' hands come from `seats[i].hand`, non-empty only where `canSeeHand()` allows — a Forehead round |
| Bet prompt | A **panel, not a modal dialog** (§4.4) — a modal one takes the close button with it for as long as the game thread is parked, which is Phase B's shutdown test. `0..prompt.handSize`; `forbiddenBet` disabled with a tooltip explaining why. **Blind in Forehead and Hidden** — `prompt.hand` is empty there, so there is nothing to show the bidder back even by accident |
| Scoreboard | per-round bid/actual/score; `roundScored` is the signal to refresh it |
| Status bar | round number, trick number, `roundType`, whose turn (`turnChanged`) |
| Game over dialog | `getStandings()` for the table **and `getWinners()` for the headline**. A drawn game has more than one winner and `standings.front()` names only one of them; the terminal renders it as a draw |

**Every user-facing string goes through `tr()`.** The scaffold already ships
`i18n/romanian_whist_desktop_ro_RO.ts`, so this is not a someday concern: a string added without
`tr()` is one `lupdate` will not see and no one will notice is missing until the Romanian build is
read. That includes the ones that do not look like UI text — the round-type names
(Forehead/Hidden/Normal), the standings' place labels, and `GameController::gameFailed`'s message,
which is the one string here already written with `tr()` (§4.3) and the one most likely to be read
by someone who cannot act on it. Card names are the exception: they are pixmaps, and
`rank_of_suit.png` is a filename rather than a label (below).

**Widgets vs QML:** `QGraphicsScene` / `QGraphicsView` is the natural fit for a card table — card
items are movable, animatable, and hit-testable for free, and `QGraphicsItem` animation covers
dealing and trick collection without hand-rolled painting. Plain `QWidget` with a custom
`paintEvent` is simpler but you will write the layout maths yourself. QML is the nicest for
animation but adds a second language and a QML/C++ bridge on top of the thread bridge you already
have. **Recommendation: `QGraphicsView` for the table, ordinary widgets for panels and dialogs.**

**Card art: already decided and already wired.** The 52 PNGs live in `resources/images/cards/`,
listed by `resources/cards.qrc`, which aliases each one flat under the `/cards` prefix — so code
asks for `:/cards/ace_of_spades.png`, and moving the files on disk does not move the URLs. That
path is the whole of what this document needs to say about them. How they are embedded, why not
through AUTORCC, and how to tell when nothing was embedded at all are in `CMakeLists.txt`'s own
comments, which are the authority on it (§1) — read them before touching that block.

Naming is a uniform `rank_of_suit.png` with **no exceptions** — no `ace_of_spades2.png` to
special-case, and nothing else in the set that needs a branch. Build the filename mechanically, but
write the rank and suit map yourself, because **`Card::toString()` is not it**: the files spell both out
(`jack_of_hearts.png`, `10_of_clubs.png`) and `toString()` gives `"J"`, `"Q"`, `"K"`, `"A"` and
one-letter suits, which is the compact form a terminal wants. Two `switch`es in whichever widget
loads the pixmaps.

What is left to watch is DPI. PNGs mean one raster size to look right on every screen — check them
on a HiDPI display early, and hold the option of `@2x` variants or an SVG set behind `QSvgRenderer`
if they soften. Only a subset is ever dealt: the engine trims low ranks as the table grows,
`initializeDeck()` starting at rank `1 + (6 - playerCount) * 2`, which is `Three` even at six
players — so the four deuces are never dealt at any table size, and a two-player deck starts at
`Jack`. They are shipped and embedded regardless: 52 files is what the art set came as, four unused
PNGs cost nothing, and a card widget that can draw any card is one fewer thing to be wrong about.

---

## 8. Phases

Each ends in something runnable.

**Phase A — spectator window.** All-bot game, no input. Setup hardcoded, with a fixed
`shuffleSeed` so a misrender is reproducible. `GameController` + `GameBridge` +
`MainWindow::render`. Proves the thread boundary, the snapshot marshalling and the metatype
registration — the three things most likely to be subtly wrong. **Do the whole of §4.3's ordering
and §6's shutdown here**, not later: prepare/connect/start as three steps, the guarded engine
pointer, the provider owned by the controller. They are cheap now and each one is a heisenbug once
there is a UI on top of them. *Verify:* a full game plays out on
screen, the window stays responsive throughout, and closing it mid-game exits cleanly through
`onGameStopped()` with no leaked thread. **That `onGameStopped()` is a real assertion, not a
formality:** a spectator game is asleep in `pace()` almost all the time, so this close will land
there, and it reaches the clean path only because `pace()` returns instead of throwing (§6). If you
see `GameAbandoned` here instead, `pace()` is throwing and §6 has been implemented backwards. Note
the parked-provider half of §6 cannot be exercised here — an all-bot game never parks — which is
why B repeats the shutdown test.

Three things phase A needs that are easy to skip because a spectator game seems simpler:

- `humanSeat` is `std::nullopt`, **not** some arbitrary seat. Pass one and that bot silently plays
  without a beat, and `canSeeHand()` is asked about a viewer who is not there (§4.0).
- The pacing that follows from it is `spectatorBeat`, not `beat` (§4.1).
- **There is no provider**, so `stop()` runs with `movePlayer == nullptr` (§4.3, §6). This is not a
  case to handle later: it is the state phase A is in for its entire life, and the crash it causes
  lands on the close that phase A exists to test.
- **`stopRequested` is latched by `stop()` and read back by the run slot** (§4.3, §6). Same reason,
  one step earlier: with no provider there is no latched `abandon()` to fall back on, so a close
  landing before the engine is published is dropped outright and the game plays itself out to
  `onGameOver()`. It is rare enough to look like flakiness and common enough to see, since a window
  closed at once is the quickest way to test any of this.

**Phase B — the human seat.** `QtMoveProvider`, both prompts, the clickable hand. This is the hard
part; A exists to make it the only hard part. *Verify:* play a full game; confirm illegal cards
cannot be clicked, the forbidden bid is disabled, the card clicked is the card played (the
`HandLayout` translation of §4.4), and closing the window **while the bet prompt is open** exits
cleanly — that is the `GameAbandoned` path, the only one that should produce it, and the reason §6
keeps a throw at all. That last one is the test the bet panel's non-modality exists for (§4.4): a
`QDialog::exec()` here swallows the close and the test cannot be run at all, which reads as a
shutdown bug rather than as the dialog it actually is.

**Play the game to the end, and do not stop at the last 8-card round.** With
`endWithForeheadAndHidden` left at its default, the schedule finishes with a Forehead round and then
a Hidden one, and **those two rounds are where a hand widget driven by snapshots stops working** —
the snapshot's hand is empty in both, so there is nothing on screen to click and the engine parks on
a card it will never be given (§4.0). *Verify specifically:* in each of the last two rounds the hand
shows one face-down card, clicking it plays the round out, and the game reaches `onGameOver()`. A
run that stalls there has the prompt/snapshot split wrong, not the blind rendering.

**Phase C — setup dialog and scoreboard.** `GameSetup` from a real dialog, per-round score table,
game-over standings and winners. *Verify:* a game deliberately ended in a draw announces both
seats.

A setup dialog also introduces the **second game**, which is the first thing here that reuses a
`GameController`. It is not a special path so much as §4.3's ordering run again from the top:
`stop()` and join, then a fresh `GameBridge` and `QtMoveProvider` from `prepare()`, then reconnect
`MainWindow`, then `start()`. Nothing is reset in place. `QtMoveProvider::abandoned` in particular
is latched and never cleared, so a reused provider refuses every prompt of the second game
instantly, which looks like the bots playing a game by themselves at full speed.
`GameController::stopRequested` (§4.3) is the same hazard one level up and the one thing here that
is *not* solved by rebuilding, because the controller is the object being reused: `prepare()` clears
it, and a `prepare()` that forgets to leaves game two dealing a round and stopping in it.
`GameSetup` is spent by the move into the first `start()` (§4.3), so the dialog builds a new one
each time regardless. *Verify:* two games back to back in one process, the second as playable as
the first.

**Phase D — polish.** Card animation, pacing controls, round-type presentation (Forehead/Hidden),
keyboard shortcuts, window state persistence via `QSettings`. Two things here are threading work
wearing a UI hat, and both are §5's: every control is a condition variable rather than a slot,
because nothing can be signalled into a thread parked in `run()`; and shortening the beat is what
first makes `stateChanged`'s lack of coalescing visible, because the beat is the only thing
throttling it today.

### What to test, as opposed to what to look at

Every *verify* above is something a person watches happen, which is the right check for a card table
and the wrong one for the three types in §4 — they are where the bugs are, they are the least
observable part of the client, and a threading bug that reproduces once in twenty runs is not
something anyone will catch by playing. The engine and the terminal both carry a Catch2 suite; this
client should take one from phase A, alongside the window rather than after it.

Most of it needs no GUI and no engine at all:

- **`QtMoveProvider`, headless.** The generation gate is pure C++ with a mutex: a `submit()` for a
  stale generation is dropped; a second `submit()` for the same one is dropped; `submit()` with
  nothing parked is dropped and does not answer the *next* prompt (this is the bug §4.2's gate
  exists for, and the one that will otherwise be found in a real game as "it played a card I did
  not click"); `abandon()` on a parked provider throws `GameAbandoned`; `abandon()` before it parks
  still throws rather than hanging.
- **`GameBridge::pace()`.** Returns early on `cancelPacing()` and **does not throw** — §6's whole
  shutdown story is that one fact, and it is one assertion.
- **`GameController` shutdown**, against a real engine and scripted bot providers: `stop()` before
  `prepare()`, `stop()` on a spectator game (the null `movePlayer` of phase A), `stop()` twice,
  `stop()` after the game has finished on its own. Each is a line, and each is a crash the first
  time it is met in a running window instead.
- **`stop()` between `start()` and the engine being published**, which is the one of those that is
  not a crash and so the one a running window will not report. Hold the run slot at the top with a
  scripted provider or a test hook, call `stop()`, release it: the game must end through
  `onGameStopped()` rather than play its schedule out to `onGameOver()`. Asserting on *which* of the
  two fired is the test — a spectator game with pacing cancelled finishes either way, and quickly,
  so "it terminated" says nothing. Then the same flag from the other side: `stop()`, `prepare()`,
  `start()` again, and game two must reach `onGameOver()` on its own rather than stopping in its
  first round (§4.3). One assertion each, and between them they pin both halves of a latch that is
  wrong when it is missing and wrong when it is left set.
- **The hand round trip**, which needs neither thread: for a hand and a legal set, the display slot
  a player clicks maps back through `handIndex` to the card they saw. Property-shaped — deal a
  hand, lay it out, assert every slot's `entries[i].card` equals `hand[handIndex[i]]`. Then the
  guards that back it: a `submit()` naming an illegal slot re-prompts rather than returning, and one
  naming a slot past the end of the hand does the same (§4.2). Both are one assertion and both are
  otherwise found as `gameFailed` with an engine message.
- **The snapshot, against a seeded engine.** `refreshFromEngine()` at four points — after `start()`,
  mid-bidding, mid-trick, after the final round is scored — asserted against literals. This is the
  anti-drift test §9 asks for, and the two that matter most are the ones §4.0 turns on: `humanSeat`
  set before the refresh, and `hand` empty in a Forehead or Hidden round.

What is left for a person is the part that genuinely needs eyes: that the window stays responsive,
that the pacing is watchable, and that the right card is on screen.

> **The one place the threading tax grows.** `pace()` is open-loop: the game thread sleeps for a
> guessed duration while the GUI animates, and nothing checks that the card actually landed. That
> is fine for a dwell and wrong for "wait until the card has finished flying". Closing the loop
> means a second condition variable on the bridge, notified from the animation's `finished()`
> signal on the GUI thread — the same shape as `submit()` and `cancelPacing()`, for the reason §5
> gives: no slot call will ever reach the game thread while `run()` is in flight. Pacing controls
> and a skip key are the same mechanism. Budget for it rather than discovering it.

---

## 9. A design question this raises for the engine

**Still open.** Nothing was promoted into the engine during v4, so the snapshot type still lives
only in the terminal — as `struct GameView` with `refreshFromEngine(GameView&, const GameEngine&)`
in `GameView.{h,cpp}`. Note the names: `GameSnapshot` is this client's type for its own copy (§4.0),
and the terminal's is `GameView`. Nothing called `GameSnapshot` exists outside this repository.

**With a second client, sharing it stops being obviously optional.** Both need the same snapshot of
the same state, and this one needs it specifically because it must not read the engine off-thread.

But `GameView` carries fields the engine has no business owning, so the answer is not simply "move
it":

| Engine-derivable — could move | Client-only — could not |
|---|---|
| round number/count, trick number/count, `roundType` | `message`, `hint`, `error` |
| `hasTrump`/`trump`, `leadSuit` | `elapsedMinutes` (the engine has no wall clock) |
| per-seat name, bet, tricks won, cards left, scores, streaks, bid order | `HandEntry::choice` (a typed keyboard number) |
| `table` from `getCurrentTrick()`, with the `isWinning` highlight | `Phase`, a UI phase distinct from `GamePhase` |
| per-seat visible hands via `canSeeHand()` | `humanSeat`, `activeSeat`, display ordering |

`refreshFromEngine()`'s own header comment already draws that line: *"A pure function of the engine.
What is left to the caller is only what the engine does not model."*

**Revised recommendation: promote the left column only**, as `romanian_whist::GameSnapshot` plus a
`snapshotFrom(const GameEngine&)`, and let each client wrap it in whatever view type it needs. The
left column is exactly what "read the state safely off-thread" means; the right column is UI and
belongs in the UI. `layOutHand()` stays client-side with it — the display ordering it produces is a
presentation choice, not an engine one.

**§4.0 takes the client-side-first order, so this is no longer a decision phase A waits on.** This
client writes its own `GameSnapshot` and its own `refreshFromEngine(GameSnapshot&, const
GameEngine&)`, ported from `GameView.cpp`, and §9 stays open behind it. That is the legitimate order
and it keeps the promotion honest: by the time there are two real implementations of the left column
side by side, what genuinely belongs in the engine is a question of fact rather than of taste.

The cost of deferring is one porting job now and one deduplication later, and the thing to watch is
that they do not drift in the meantime — if this client's copy grows a field the terminal's lacks,
that field is evidence for the right-hand column, not the left.

**"Watch that they do not drift" is not a mechanism, so here is one.** Two copies of the same
function in two repositories, maintained by nobody in particular, drift by default. What catches it
cheaply is a test in *this* repo, listed with the others below: run the client's
`refreshFromEngine(GameSnapshot&, const GameEngine&)` against a seeded engine at a few known points
— after `start()`, mid-bidding, mid-trick, after the final round is scored — and assert the
left-column fields against literals. It does not compare the two implementations, which would mean
depending on the terminal; it pins this one, so that a change made to match a terminal fix has to be
made deliberately and a change made by accident fails. The literals also double as the specification
of what §9 would be promoting, which is the thing that has to be agreed before it can be.

---

## 10. Comparison: the three clients on one engine

Terminal exists; Qt is this document; **web is still hypothetical** and its column is what the
shape would be, not a description of code.

| | Terminal *(built)* | Qt *(this document)* | Web *(hypothetical)* |
|---|---|---|---|
| Engine runs on | main thread | `QThread` worker | session thread |
| Observer does | draws directly | snapshot → signal | snapshot → JSON |
| Marshalling | none needed | `QueuedConnection` | post to the event loop |
| Provider blocks on | `std::cin` | condition variable | condition variable |
| Unblocked by | keypress | click | WebSocket frame |
| Cancellation | EOF → exception | `requestStop()` + `GameAbandoned` | disconnect → exception |
| Rules implemented | none | none | none |

The last row is the point.

---

## 11. Reference files

**Engine (the submodule at `libs/RomanianWhistEngine`), at 4.1.0:**

- `README.md` — the API surface this is written against. "Threading, stopping, and
  failure" is the contract §3 and §6 work through, and it points back here for the worked example;
  "Abandoning a game whose provider is parked" is §4.2's exception, under its canonical name;
  "Watching a game" is the callback order.
- `CHANGELOG.md` — 4.1.0 added `Standing::place` and `getWinners()`, which §7's
  game-over row depends on.
- `include/romanian_whist/IGameObserver.h` — all thirteen callbacks, and exactly what is readable
  from each. §4.1 overrides all thirteen; nothing is left defaulted.
- `include/romanian_whist/Seat.h` — why a seat is a struct, and why that is deliberate.
- `include/romanian_whist/GameEngine.h` — `GameSetup`, `Standing`, and the re-entrancy rules on
  `addObserver()`/`run()` noted in §5.
- `ENGINE_V4_PLAN.md` — why the engine has that shape; §3 is the design
  rationale behind the README's surface.

**Terminal client (`../romanian_whist_terminal`), at 2.2.0:**

- `src/GameView.{h,cpp}` — `struct GameView` and `refreshFromEngine()`: the snapshot this client's
  `GameSnapshot` is modelled on (see §9).
- `src/HandLayout.{h,cpp}` — `layOutHand()` and `HandLayout::choices`, the display-order↔engine-index
  translation §4.4 depends on. Its header comment is the best statement of the problem anywhere in
  either repository.
- `src/Pacer.{h,cpp}` — the pacing behaviour §4.1's `Pacing` struct is modelled on. Its delays are
  700ms between plays with a human at the table, 250ms in a spectator game and 1200ms of trick
  dwell; §4.1 takes all three. Also the skip mechanism this client does not have yet, and
  `resetSkip()`'s placement at the end of `onBettingComplete()` — `TerminalRomanianWhist.cpp:166`,
  with the comment explaining why it is not in `onRoundStarted()`.
- `src/TerminalRomanianWhist.cpp` — the post-v4 observer shape in its simplest form, with no thread
  boundary. Note it draws the game-over screen *after* `run()` returns rather than from
  `onGameOver()`, which a GUI cannot do.
- `src/ConsoleMoveProvider.cpp` — the other `IMoveProvider`, including the blind bid prompt that
  §7's Forehead/Hidden row calls for.
- `src/SetupWizard.h` — `GameConfig`, the shape the setup dialog builds.
- `CMakeLists.txt` — the submodule guard + `add_subdirectory` + `RomanianWhist::engine` pattern this
  repo's own `CMakeLists.txt` now copies, down to the HTTPS submodule URL. Still current; the two
  build files should stay in step.

---

## Appendix A. Corrections

Four things this document asserted at some point and no longer does. They are kept because each one
is a plausible answer that a reader — or a later draft — will arrive at again from the same
reasoning, and because three of the four are cases where **no compiler and no test will say
otherwise**. They are here rather than in the sections above so that §§2–6 can read as assertions.

**A.1 — "Inverting the loop is expensive because of the mechanics." Backwards.** The claim was that
`playRound()` keeps its state in the locals of nested loops, so a `playStep()` would mean unpicking
them. `GameEngine.h` says the opposite outright, and `playRound()` bears it out: the phase, the
active seat, the trick number and the in-flight trick are all members, kept that way deliberately
*so that* a non-blocking step function stays an additive change. What is left in locals is two seat
rotations, their counters, and the loop indices. The real cost is the stop contract, which is what
§2 argues instead — a much narrower objection, and one that points at the web backend rather than
at this client.

**A.2 — "A non-QObject cannot emit another object's signal; the compiler will stop you." False
since Qt 5.** `Q_SIGNALS` is `public`, defined that way in `qtmetamacros.h` so that
`&Class::signal` works in the pointer-to-member `connect` syntax. `emit bridge->betRequested(...)`
from inside `QtMoveProvider` compiles cleanly. §4.1's `raiseBetRequested()` / `raiseCardRequested()`
forwarders therefore enforce nothing — they are a convention, and one that has to be held to on
purpose.

**A.3 — "Omitting `Q_DECLARE_METATYPE` is a compile error." True in Qt 4 and 5, not in Qt 6.**
`QMetaType` is built from any complete type now, so a queued connection usually resolves a custom
payload with the macro absent. Same vintage of knowledge as A.2, and the same hazard: the failure
mode where auto-registration does *not* reach is a line on stderr and a signal that silently never
arrives. §5 writes both lines anyway, for that reason and not because anything demands them.

**A.4 — "`handIndex` belongs on `GameSnapshot`, cached by `render()`."** The supporting argument was
that queued events are delivered in emission order, so the `stateChanged` carrying a hand always
arrives before the `cardRequested` naming its `promptId`. That is true, and it is not the problem.
The problem is that a snapshot's hand carries no legality and is *empty* in the two round types that
end every default game (§4.0), so it was never a thing that could be clicked. Ordering was the
wrong axis to argue on. A related draft prohibited re-sorting the hand and pointed at an
`orderHandForDisplay()` on the engine side; **no such function exists**, and the prohibition was the
wrong fix for the right worry — sorting for display is fine, and unsafe only when the way back is
not carried with it.
