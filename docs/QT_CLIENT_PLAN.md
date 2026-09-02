# Qt desktop client, on the v4 engine

A design sketch for a Qt 6 / C++ GUI client for Romanian Whist, written against the engine as it
exists after `ENGINE_V4_PLAN.md` — which has now landed in full. The engine is at 4.1.0 and every
prerequisite below is met, so this is buildable today.

> **This is the canonical copy.** It used to live in the engine repository, beside the v4 plan it
> depends on, and moved here when this repo was created; the engine's copy has been deleted. Engine
> and terminal files are therefore named by repository-relative path rather than linked — neither
> tree is reachable from here until the engine lands at `libs/RomanianWhistEngine` (§11).

---

## 1. Prerequisites

**All met.** This section used to say "do not start before v4 phase 3 lands". Phases 0-6 all
landed, the engine shipped 4.0.0 and is now at **4.1.0**, so nothing here is waiting on the engine
any more. What each phase gave this client, for orientation:

| v4 phase | What this client gets from it |
|---|---|
| 0 — tests | The engine under the GUI is pinned by a Catch2 suite |
| 1 — seats | `Seat` is the identity used throughout the UI. Note it is a **struct with an explicit constructor**, not an integer: `Seat{i}` to build one, `seat.index` to read one |
| 2 — loop + observer | **Load-bearing.** The entire design is `IGameObserver` + `run()` |
| 3 — `start(GameSetup)` | Setup dialog builds a `GameSetup` directly. `shuffleSeed` came with it, which makes a reproducible deal a one-line debugging aid |
| 4 — cards by value | `CardValidator::getLegalCards(hand, trump, leadSuit)` returns a `std::vector<Card>` by value, and the hand `PlayContext` hands over is a `const std::vector<Card>&` — cards by value inside it, where copying `Card*` was a bug. Copying one for a snapshot is now trivial |
| 5 — Forehead/Hidden | `canSeeHand()` and `RoundType`, which the table and the bid prompt both need |
| 4.1.0 (post-plan) | `Standing::place` and `getWinners()` — the engine ranks the standings, so no client decides a tie for itself |

Repository layout follows `romanian_whist_terminal`: the engine as a git submodule at
`libs/RomanianWhistEngine`, consumed with `add_subdirectory` and linked as `RomanianWhist::engine`,
behind the same guard that tells you to run `git submodule update --init --recursive` when it is
missing. Qt arrives via `find_package(Qt6 REQUIRED COMPONENTS Widgets)`.

**Two things in the Qt Creator scaffold currently disagree with that** and want fixing before the
submodule lands, since neither fails in a way that names itself:

- `CMakeLists.txt` sets `CMAKE_CXX_STANDARD 17`. The engine declares
  `target_compile_features(romanian_whist_engine PUBLIC cxx_std_20)`, so linking it raises the bar
  to 20 through the interface anyway. Say 20 outright rather than leaving two answers in the tree.
- `find_package(QT NAMES Qt6 Qt5 ...)` accepts Qt 5. This document is Qt 6 throughout. Drop the Qt 5
  fallback rather than half-supporting a configuration nothing is tested against.

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
there is no `step()`. Adding one means inverting `GameEngine::playRound()`, which nests three deep —
round, then trick, then seat — with live state in locals at every level: the trick counter,
`runBidding()`'s seat rotation and bid index, `playTrick()`'s per-seat trump, lead suit and legal
set. Every one of those becomes an explicit member and a state transition.

And the mechanics are the cheap half. Each stop check in that function carries a paragraph of
reasoning about exactly which boundary the flag may be read at and what fires when it lands there —
those comments are the specification for §6's table, and inverting the loop means re-deriving all of
them as state-machine invariants, against a shipped terminal client and a Catch2 suite both written
to the current contract.

So the trade is a few hundred lines of well-understood threading code **in this client**, against
rewriting the engine's core loop and breaking the one client that already works. The threading tax
is real, but it is paid once, in the three types of §4, and it leaks into no widget or dialog.

**The middle path is worse than either.** Keeping `run()` but calling it on the GUI thread, with the
provider spinning a nested `QEventLoop` until the click arrives, looks like it buys single-threaded
simplicity for nothing. It does not: a nested event loop delivers *every* pending event, including a
window close that destroys `MainWindow` while engine frames are still on the stack, and a second
click that re-enters code which calls back into the engine — precisely what §5's re-entrancy guards
throw on. That trades several understood problems for one that surfaces as stack corruption much
later.

If the loop is ever inverted, it should be an engine change motivated by the web backend, which
needs it far more than a desktop app with one game in flight does — not by this client.

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
    explicit GameBridge(Seat humanSeat, Pacing pacing);

    // ---- IGameObserver: all called on the GAME thread ----
    // The engine declares thirteen callbacks; these are the ones this client
    // acts on. The rest stay defaulted no-ops.

    // Fires while the engine is still idle - set up but not yet dealt. The one
    // callback from which run() may legally be called (see section 5).
    //
    // Note WHERE it fires from: the last thing GameEngine::start() does, not
    // the first thing run() does. So this snapshot is emitted before run() is
    // ever entered, and a MainWindow that connects to the bridge after the game
    // thread has been started misses it outright. See section 4.3 - the bridge
    // is built and connected on the GUI thread, before the thread starts.
    void onGameStarted(const GameEngine& e) override { publish(e); }

    void onRoundStarted(const GameEngine& e) override { publish(e); }

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
        if(seat != humanSeat) pace(pacing.beat);
    }
    void onBettingComplete(const GameEngine& e) override { publish(e); pace(pacing.beat); }

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
        if(seat != humanSeat) pace(pacing.beat);
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
    // QtMoveProvider is not a QObject and signals are protected, so
    // `emit provider->bridge->betRequested(...)` will not compile. These are
    // the seam. Both are called on the GAME thread, from inside the provider.
    void raiseBetRequested(BetPrompt prompt) { emit betRequested(std::move(prompt)); }
    void raiseCardRequested(PlayPrompt prompt) { emit cardRequested(std::move(prompt)); }

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
        snapshot.humanSeat = static_cast<int>(humanSeat.index);
        refreshFromEngine(snapshot, engine);

        emit stateChanged(snapshot);               // copied into the queued event
    }

    void pace(std::chrono::milliseconds delay);    // interruptible - see section 6

    Seat humanSeat;
    Pacing pacing;

    // Woken by cancelPacing() so a close does not wait out a trick dwell.
    std::mutex pacingMutex;
    std::condition_variable pacingCv;
    bool pacingCancelled = false;
};
```

**`Seat` is a struct, not an integer.** `Seat{i}` builds one, `seat.index` reads one, and `==` /
`!=` compare them. It has **no default constructor**, so it can never be a queued signal payload —
which is why every signal above carries `unsigned int seat` and every callback body spells
`seat.index`. Comparing seats (`seat != humanSeat`) is fine and clearer than comparing indices.

Because `GameBridge` has affinity to the game thread and `MainWindow` to the GUI thread, Qt's
default `AutoConnection` resolves to `QueuedConnection` and marshals for you — the same job a web
backend's event-loop post would do.

`pace()` inside a callback is deliberate, and mirrors the terminal's `Pacer`: blocking the observer
pauses the game, which is exactly what makes bot turns watchable. One note carried over from the
v4 plan: the terminal found that `Pacer::resetSkip()` belongs at the end of `onBettingComplete`,
not in `onRoundStarted`. If this client grows a skip key, it inherits that.

### 4.2 `QtMoveProvider` — the blocking seam

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
        std::unique_lock lock(mutex);

        // BetContext has no handSize: the hand's size IS the round's trick
        // count, and so the upper bound on a legal bid.
        //
        // roundType matters here beyond presentation. In Forehead and Hidden
        // the bidder is not entitled to their own hand's contents - the engine
        // still passes the real hand, having no other to give - so the prompt
        // must not show it back to them.
        const unsigned int prompt = ++generation;

        // The hand is left OUT of the prompt in a blind round rather than sent
        // and then not drawn. The engine passes the real hand because it has no
        // other one to give; a prompt that never carries it cannot leak it
        // through a tooltip, a debug overlay or a later refactor.
        const bool blind = context.roundType != RoundType::Normal;

        bridge->raiseBetRequested(BetPrompt{ prompt,
                                            context.hand.size(),
                                            context.forbiddenBet,
                                            context.roundType,
                                            context.isFirstPlayer,
                                            blind ? std::vector<Card>{}
                                                  : snapshotHand(context.hand) });
        cv.wait(lock, [this, prompt]{ return answeredPrompt == prompt || abandoned; });
        if(abandoned) throw GameAbandoned{};
        return static_cast<unsigned int>(*std::exchange(answer, std::nullopt));
    }

    std::optional<std::size_t> playCard(const PlayContext& context) override
    {
        CardValidator validator;
        const std::vector<Card> legal =
            validator.getLegalCards(context.hand, context.trump, context.leadSuit);
        if(legal.empty()) return std::nullopt;

        std::unique_lock lock(mutex);
        const unsigned int prompt = ++generation;

        bridge->raiseCardRequested(buildPlayPrompt(prompt, context, legal));
        cv.wait(lock, [this, prompt]{ return answeredPrompt == prompt || abandoned; });
        if(abandoned) throw GameAbandoned{};

        // std::exchange clears it. Leaving it set means the next turn returns
        // this same index without ever waiting, and the engine throws on the
        // illegal play - see README.md, "Abandoning a game whose provider is
        // parked".
        return *std::exchange(answer, std::nullopt);
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

`submit()` needs no signal or slot. It touches only a mutex, so calling it directly from a click
handler on the GUI thread is safe — and skipping Qt's event queue means the game thread wakes
immediately.

**Emitting a signal from a non-QObject:** `emit bridge->signalName(...)` will not compile, because
signals are protected. Hence the public `raiseBetRequested()` / `raiseCardRequested()` forwarders on
`GameBridge` (§4.1), which is why the calls above are plain method calls with no `emit`. Declaring
`QtMoveProvider` a friend would also work; the forwarder is cleaner.

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
    // bridge and the provider on the GUI thread and hands them back; the caller
    // connects; only then does start() spawn the thread.
    //
    // Doing it in one call cannot work: onGameStarted() fires from inside
    // GameEngine::start(), so the first stateChanged() is emitted before run()
    // is entered, and a MainWindow that connects afterwards has already missed
    // it. A queued connection made after the emit does not receive it.
    void prepare(Seat humanSeat, Pacing pacing);

    // GameSetup is MOVE-ONLY - it owns the seats' unique_ptr<IMoveProvider> -
    // so the call site reads start(std::move(setup)), and the setup is moved
    // again into the run slot's captures. It cannot travel through a queued
    // signal, which is why it goes into the lambda rather than a slot argument.
    //
    // The human seat's moveProvider is left NULL by the caller and filled in
    // here, since only this class has the provider. Note the caller's own
    // GameSetup is gutted by the move whatever happens, so a start() that
    // throws needs a freshly built setup to retry - not because the engine kept
    // anything (GameEngine::start() validates before it applies, and a rejected
    // one leaves the engine untouched and startable) but because a move-only
    // type passed by value is spent at the call site.
    void start(GameSetup setup);
    void stop();                                  // see section 6

    GameBridge* bridge() const;                   // built by prepare(); connect to it before start()

    // Owned HERE, not by the engine, and that is a lifetime fix rather than a
    // style choice. The engine owns whatever is in GameSetup and destroys it
    // with itself on the game thread - so a provider owned by the engine is a
    // dangling pointer the moment run() returns on its own, and a click that
    // arrives while the game-over dialog is up is a use-after-free that no
    // amount of stopping protects against. The engine gets a non-owning
    // forwarder instead; the real object outlives the thread.
    QtMoveProvider* provider() const;

private:
    QThread thread;
    std::unique_ptr<GameBridge> gameBridge;       // built on the GUI thread, then moveToThread()
    std::unique_ptr<QtMoveProvider> movePlayer;   // outlives the engine; see above

    // The engine is constructed, used and destroyed on the game thread, so the
    // GUI thread may not read this pointer unguarded - stop() would otherwise
    // race the run slot's construction (and find it null if the window is
    // closed in the gap between thread.start() and the slot running) and its
    // destruction. Published under the mutex once built, withdrawn under it
    // before destruction, and stop() calls requestStop() while holding it.
    // Cheap: the game thread takes this lock exactly twice per game.
    std::mutex engineMutex;
    GameEngine* engine = nullptr;
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
   exist — publishes the engine pointer under `engineMutex`, and calls `run()`.
5. On the way out, still on the game thread: withdraw the pointer under the lock, then destroy the
   engine. The provider is not destroyed with it; it belongs to the controller.

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
connect(bridge, &GameBridge::cardRequested, this, &MainWindow::enableHand);
connect(bridge, &GameBridge::trickWon,      this, &MainWindow::flashTrickWinner);
connect(bridge, &GameBridge::gameOver,      this, &MainWindow::showStandings);
// NOT MainWindow::close directly: closing the window is what calls stop(),
// stop() is what produces gameStopped, and close() would then re-enter
// closeEvent(). The slot checks a `closing` flag first and only forces the
// close when the stop came from somewhere else.
connect(bridge, &GameBridge::gameStopped,   this, &MainWindow::onGameStopped);

// Field names follow the terminal's GameView (src/GameView.h), which is what
// this type is modelled on - see section 9.
void MainWindow::render(const GameSnapshot& s)      // a copy; the engine is untouched
{
    roundLabel->setText(tr("Round %1 of %2").arg(s.roundNumber).arg(s.roundCount));
    trumpWidget->setCard(s.hasTrump, s.trump);      // no trump in an 8-card round
    for(const SeatView& seat : s.seats)
        seatPanels[seat.seatIndex]->update(seat);
    handWidget->setEntries(s.hand);                 // HandEntry, not Card - see below
    trickWidget->setCards(s.table);                 // PlayedCardView: seat, card, isWinning
}

// handIndex runs parallel to the displayed hand: handIndex[i] is the position
// in the ENGINE's hand of the card drawn at display slot i. Built alongside the
// snapshot, thrown away and rebuilt on the next one.
void MainWindow::onCardClicked(std::size_t displayPosition)
{
    handWidget->setEnabled(false);                  // until the next stateChanged arrives

    // promptId is the one carried by the PlayPrompt this hand was drawn for.
    // Disabling the widget is not enough on its own - see submit() in 4.2.
    provider->submit(promptId, handIndex[displayPosition]);
}
```

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
> whole hand — the `handIndex` vector above — with (3) reduced to a `legal` flag that decides
> whether the slot is clickable at all. `HandEntry` already carries that flag.
>
> Same idea, one indirection fewer. Carrying positions rather than cards is the whole trick:
> sorting for display is fine and desirable, and it is only unsafe if the way back is not carried
> with it. Getting it wrong means the player clicks one card and plays another, which no rule in
> the engine would object to.
>
> An earlier draft of this document said "never re-sort" and pointed at an
> `orderHandForDisplay()` on the engine side. **No such function exists**, and the prohibition was
> the wrong fix for the right worry.

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

The two halves do different jobs, and only one of them is compile-time:
`Q_DECLARE_METATYPE` is what makes the type usable as a queued argument at all, and failing to
write it is a compile error. `qRegisterMetaType` is a runtime call that names it for the
connection machinery; forgetting it is the silent *"cannot queue argument of type X"* warning on
stderr and a signal that quietly never arrives. Qt 6 auto-registers in many cases — write both
anyway, since the failure mode of the one it does not cover is a dropped event rather than a
crash.

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
        if(engine != nullptr)
            engine->requestStop();   // ends cleanly at the next round, bid or trick boundary
    }

    provider->abandon();       // unblocks a provider parked mid-prompt
    bridge->cancelPacing();    // unblocks an interruptible pace()

    // Note quit() does nothing until run() has returned: the slot is blocking
    // that event loop, which is the whole point of the three calls above. They
    // are what ends the game; this pair only joins the thread afterwards.
    thread.quit();
    thread.wait();             // join BEFORE anything else is destroyed
}
```

`stop()` must be safe to call **when the game is already over**, because that is the ordinary case:
the player reads the standings and then closes the window. By then the run slot has returned, the
engine is destroyed, and every line above is either guarded (`engine`) or operating on an object the
controller still owns (`provider`, `bridge`). That is why §4.3 moved the provider's ownership out of
the engine — with the engine owning it, `provider->abandon()` on this path is a use-after-free, and
so is any click that lands while the game-over dialog is up.

`requestStop()` alone is not enough — if the game thread is blocked waiting for a human bet, it
never reaches a boundary at which to check the flag. The engine says so itself: *"It cannot
interrupt a move provider already parked waiting for a human... Unparking that provider is the
client's job, and throwing from it is the supported way out."* Equally, `abandon()` alone leaves a
thread sleeping in `pace()` for up to `trickDwell`.

**Which of the two lands decides what the observer sees**, and this is worth getting straight:

| How it ended | What fires | State |
|---|---|---|
| `requestStop()` caught at a boundary | `onGameStopped()` — **not** `onGameOver()` | status `Stopped`, round left unscored, engine not resumable |
| `GameAbandoned` thrown from a parked provider | nothing; the exception unwinds `run()` | engine mid-round, not resumable |
| The game simply finished | `onGameOver()` | status `Finished` |

So a window closed mid-game usually produces `onGameStopped()` or an exception, and *never*
`onGameOver()` — a client that only handles `onGameOver()` is silent on the exact path it was
written for. Note also that a stop requested once the final round has been scored is a **no-op**:
the game stays `Finished` and `onGameOver()` is what fired.

`pace()` must be an interruptible wait, not `QThread::sleep` or `std::this_thread::sleep_for`:

```cpp
void GameBridge::pace(std::chrono::milliseconds delay)
{
    std::unique_lock lock(pacingMutex);
    pacingCv.wait_for(lock, delay, [this]{ return pacingCancelled; });
    if(pacingCancelled) throw GameAbandoned{};
}
```

`GameAbandoned` propagates out of the provider or observer, through `GameEngine::run()`, and is
caught in the thread's run slot. Per the engine's own contract, an engine that throws out of `run()`
is **not resumable**: catch it outside `run()`, destroy the engine, and restore whatever you set up
before the call, because nothing else will.

---

## 7. UI surface

Minimum to play a full game:

| Widget | Reads from snapshot |
|---|---|
| Setup dialog | *(writes)* builds a `GameSetup` — names, bot levels, `structure`, `endWithForeheadAndHidden`, `all1GamesAreForehead`, and optionally `shuffleSeed`. `SetupWizard.h`'s `GameConfig` is the shape to copy |
| Seat panel × N | `seats[i]`: `name`, `bet` (only when `hasBet`), `tricksWon`, `cardsLeft`, `roundScore`/`totalScore`, `winStreak`/`lossStreak`, `bidOrder` |
| Trick area | `table` (`PlayedCardView`: `seatIndex`, `card`, `isWinning`) in play order; trump from `hasTrump`/`trump` |
| Hand | `hand` (`HandEntry`: `card`, `legal`, `choice`) in display order; illegal cards dimmed and non-clickable. Other seats' hands come from `seats[i].hand`, non-empty only where `canSeeHand()` allows — a Forehead round |
| Bet prompt | `0..context.hand.size()`; `forbiddenBet` disabled with a tooltip explaining why. **Blind in Forehead and Hidden** — do not show the bidder their own hand back |
| Scoreboard | per-round bid/actual/score; `roundScored` is the signal to refresh it |
| Status bar | round number, trick number, `roundType`, whose turn (`turnChanged`) |
| Game over dialog | `getStandings()` for the table **and `getWinners()` for the headline**. A drawn game has more than one winner and `standings.front()` names only one of them; the terminal renders it as a draw |

**Widgets vs QML:** `QGraphicsScene` / `QGraphicsView` is the natural fit for a card table — card
items are movable, animatable, and hit-testable for free, and `QGraphicsItem` animation covers
dealing and trick collection without hand-rolled painting. Plain `QWidget` with a custom
`paintEvent` is simpler but you will write the layout maths yourself. QML is the nicest for
animation but adds a second language and a QML/C++ bridge on top of the thread bridge you already
have. **Recommendation: `QGraphicsView` for the table, ordinary widgets for panels and dialogs.**

**Card art: already decided.** A 52-card PNG set is committed under `res/cards/` and listed in
`CMakeLists.txt`, so this is settled and the rest of this note is only what to watch. PNGs mean one
raster size to look right at every DPI — check them on a HiDPI screen early, and hold the option of
`@2x` variants or an SVG set behind `QSvgRenderer` if they soften. Naming is `rank_of_suit.png` with
one exception, `ace_of_spades2.png`, which any filename-building code has to special-case; renaming
it now is cheaper than remembering it later. Only a subset is ever dealt — the engine trims low
ranks as the table grows, `initializeDeck()` starting at rank `1 + (6 - playerCount) * 2` — so a
full set is expected, not wasteful.

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
`onGameStopped()` with no leaked thread. Note the parked-provider half of §6 cannot be exercised
here — an all-bot game never parks — which is why B repeats the shutdown test.

**Phase B — the human seat.** `QtMoveProvider`, the bet prompt, the clickable hand. This is the
hard part; A exists to make it the only hard part. *Verify:* play a full game; confirm illegal
cards cannot be clicked, the forbidden bid is disabled, the card clicked is the card played (the
`HandLayout` translation of §4.4), and closing the window **while the bet prompt is open** exits
cleanly — that is the `GameAbandoned` path, and it is the one §6 exists for.

**Phase C — setup dialog and scoreboard.** `GameSetup` from a real dialog, per-round score table,
game-over standings and winners. *Verify:* a game deliberately ended in a draw announces both
seats.

**Phase D — polish.** Card animation, pacing controls, round-type presentation (Forehead/Hidden),
keyboard shortcuts, window state persistence via `QSettings`.

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
in `GameView.{h,cpp}`. (Earlier drafts of this document called it `GameSnapshot`; that is this
client's name for its own copy, not the terminal's.)

**With a second client, sharing it stops being obviously optional.** Both need the same snapshot of
the same state, and this one needs it specifically because it must not read the engine off-thread.

But `GameView` has since accumulated fields the engine has no business owning, and that changes the
answer from the one an earlier draft gave:

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

Worth deciding before this client starts, since it determines whether `GameSnapshot` here is a new
struct or a thin wrapper. It is not a blocker either way: building it client-side first and
promoting the common part afterwards is a legitimate order.

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

**Engine (`../RomanianWhist`; a submodule at `libs/RomanianWhistEngine` once this repo has one),
at 4.1.0:**

- `README.md` — the API surface this is written against. "Threading, stopping, and
  failure" is the contract §3 and §6 work through, and it points back here for the worked example;
  "Abandoning a game whose provider is parked" is §4.2's exception, under its canonical name;
  "Watching a game" is the callback order.
- `CHANGELOG.md` — 4.1.0 added `Standing::place` and `getWinners()`, which §7's
  game-over row depends on.
- `include/romanian_whist/IGameObserver.h` — all thirteen callbacks, and exactly what is readable
  from each. §4.1 implements twelve of them and defaults the rest.
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
- `CMakeLists.txt` — the submodule guard + `add_subdirectory` + `RomanianWhist::engine` pattern to
  copy, still current.
