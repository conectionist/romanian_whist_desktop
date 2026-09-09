#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "paint/Theme.h"
#include "table/HandItem.h"
#include "table/TableScene.h"
#include "table/TableView.h"
#include "ui/BidStrip.h"
#include "ui/CardArtWindow.h"
#include "ui/InfoStrip.h"
#include "ui/MainMenuPage.h"
#include "ui/NewGameDialog.h"
#include "ui/PauseDialog.h"
#include "ui/PromptBar.h"
#include "ui/ScoreboardWidget.h"
#include "ui/SnapshotLog.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <QBoxLayout>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStringList>
#include <QResizeEvent>
#include <QStyleHints>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <utility>

using namespace romanian_whist;

namespace
{
// PHASE A's HARDCODED GAME IS GONE, and this is what replaced it: nothing. The
// seed, the seat and buildSetup() all lived here, and D1's dialog answers all
// three - so the client no longer has an opinion about what game to play until
// somebody asks for one.
//
// THE SEAT INDEX IS WORTH ONE LINE ON ITS WAY OUT. It was 1 rather than 0, and
// deliberately: seatlayout draws seat 0 at bottom-centre for a spectator too, so
// a client that dropped humanSeat entirely would have looked correct at 0. The
// dialog seats the human at 0 now - the terminal does the same - and the
// property that index was standing in for is a test in NewGameTests instead.
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // TO THE CONTROLLER, never to the bridge. The bridge is per-game and
    // prepare() destroys it; the controller's mirrored signals outlive a game,
    // which is the whole reason the relay exists.
    //
    // AND BEFORE prepare(), which costs nothing here and is the habit worth
    // keeping: onGameStarted() fires from inside GameEngine::start(), so the
    // first snapshot is published before run() is entered.
    connect(&controller, &GameController::stateChanged, this, &MainWindow::render);
    connect(&controller, &GameController::gameStopped,  this, &MainWindow::onGameStopped);
    connect(&controller, &GameController::gameOver,     this, &MainWindow::onGameOver);
    connect(&controller, &GameController::gameFailed,   this, &MainWindow::showFailure);

    // THE TWO PROMPTS, connected here with the rest and for the same reasons.
    // They carry BetPrompt and PlayPrompt, which main() registered as metatypes
    // before this constructor ran - an unregistered one is dropped with a line
    // on stderr and no crash, which reads as the game hanging on your turn.
    connect(&controller, &GameController::betRequested,  this, &MainWindow::onBetRequested);
    connect(&controller, &GameController::cardRequested, this, &MainWindow::onCardRequested);

    // NO UNDO HISTORY. The log is read-only and append-only, and a full game
    // appends about 1300 lines - each one recorded as an undo command nothing in
    // this window can ever replay. The unlimited maximumBlockCount in the .ui is
    // deliberate (a whole game has to stay scrollable), so capping the blocks is
    // not the lever here even though a cap would disable undo as a side effect.
    ui->log->setUndoRedoEnabled(false);

    // The title lives in MainWindow.ui, not here. Setting it in both puts a
    // string in the translation catalogue that no one can ever see the effect of
    // translating, because whichever runs second wins.

    buildTable();
    buildLogDock();
    buildViewMenu();

    // NO GAME AT LAUNCH, which is what phase D's "the app never needs a command
    // line" actually means and what the terminal does - its startGame() runs the
    // setup wizard before a card exists. Phases A to C dealt a hardcoded game
    // from right here.
    //
    // AND NO DIALOG FROM A CONSTRUCTOR EITHER. The menu is a page in the window,
    // so this is a stack index rather than an exec() on a window that has not
    // been shown yet.
    showMainMenu();
}

// Section 12's four regions, minus the two that are not B2b's: the prompt bar,
// which has no prompts to carry until phase C, and the elapsed clock, which
// section 19 keeps off the snapshot and phase E puts on a QTimer.
//
// THE PALETTE IS RESOLVED ONCE, here, and handed to all three. Each widget
// asking QGuiApplication for the scheme itself would work today and would be
// three places to change when section 20's preference arrives.
void MainWindow::buildTable()
{
    theme = Theme::forScheme(QGuiApplication::styleHints()->colorScheme());

    scene = new TableScene(this);
    table = new TableView(scene, this);
    infoStrip = new InfoStrip(this);
    scoreboard = new ScoreboardWidget(this);

    // OVER the table rather than in the layout below it: a strip that appears
    // only on your turn would move everything beneath it every round, which is
    // the defect B2b's review recorded against the info strip. Parented and
    // placed by the view - see TableView::setBottomOverlay().
    bidStrip = new BidStrip(table);
    bidStrip->hide();
    table->setBottomOverlay(bidStrip);

    connect(bidStrip, &BidStrip::bidChosen, this, &MainWindow::onBidChosen);

    // OWNED BY THE SCENE, not by this window: the fan is a QGraphicsObject in the
    // table rather than a widget, because section 18.2 animates a card from it to
    // the table on CardItem::pos. This is a handle for driving its second source
    // - the prompt - which is the one thing a snapshot cannot supply.
    hand = scene->handItem();

    connect(hand, &HandItem::cardClicked, this, &MainWindow::onCardPicked);

    promptBar = new PromptBar(this);

    connect(promptBar, &PromptBar::menuRequested, this, &MainWindow::showPauseMenu);

    scene->setTheme(theme);
    infoStrip->setTheme(theme);
    scoreboard->setTheme(theme);
    bidStrip->setTheme(theme);
    promptBar->setTheme(theme);

    // The strip full width across the top, then the table and the scoreboard
    // side by side. The table takes the stretch: the scoreboard is as wide as
    // its widest name and no wider, and everything left over is green.
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addWidget(table, 1);
    body->addWidget(scoreboard);

    auto* central = new QWidget(this);

    auto* stack = new QVBoxLayout(central);
    stack->setContentsMargins(0, 0, 0, 0);
    stack->setSpacing(0);
    stack->addWidget(infoStrip);
    stack->addLayout(body, 1);

    // SECTION 12'S FOURTH REGION, full width beneath the table AND the
    // scoreboard, which is where that section's mockup draws it: it is where the
    // game talks to you, and one line always in the same place is the whole
    // point of it.
    stack->addWidget(promptBar);

    // TWO PAGES, and the table keeps its own state between games. The menu is
    // page 0 because that is what the window opens on; nothing is destroyed when
    // the pages change, so a second game repaints the same scene rather than
    // building another.
    menuPage = new MainMenuPage(this);
    menuPage->setTheme(theme);

    connect(menuPage, &MainMenuPage::newGameRequested, this, &MainWindow::showNewGameDialog);

    // close() rather than qApp->quit(), so Quit goes through closeEvent() and
    // therefore through stop() - section 12's rule that abandoning, quitting and
    // closing the window are the same three calls in the same order. There is no
    // game running when this button is reachable, which makes the stop a no-op
    // and the uniformity free.
    connect(menuPage, &MainMenuPage::quitRequested, this, &MainWindow::close);

    pages = new QStackedWidget(this);
    pages->addWidget(menuPage);
    pages->addWidget(central);

    setCentralWidget(pages);
}

void MainWindow::showMainMenu()
{
    pages->setCurrentWidget(menuPage);

    // The log dock belongs to the table, not to the menu - it is a diagnostic
    // for a game, and there is none here. Its View action is left alone, so a
    // dock the player opened comes back with the next game.
    if(logDock)
        logDock->hide();

    statusBar()->clearMessage();
}

void MainWindow::showTable()
{
    pages->setCurrentWidget(pages->widget(1));
}

void MainWindow::showNewGameDialog()
{
    // ON THE LAST ACCEPTED CONFIG, so play again pre-fills with the settings the
    // last game was dealt from (section 16.2). Held here rather than in
    // QSettings, which is section 20 and phase E.
    NewGameDialog dialog(lastConfig, this);

    if(dialog.exec() != QDialog::Accepted)
        return;

    lastConfig = dialog.config();

    // A FRESH GameSetup EVERY TIME, never a stashed one: it is move-only and
    // spent by the move into start(), so even the path where start() throws
    // needs the dialog's config re-built rather than a copy kept (section 13).
    startGame(newgame::buildSetup(lastConfig),
              newgame::humanSeatOf(lastConfig),
              newgame::pacingFor(lastConfig.botSpeed));
}

void MainWindow::showPauseMenu()
{
    // NOTHING TO PAUSE ON THE MENU PAGE. Esc is bound on the window, so it
    // arrives here whichever page is showing.
    if(!gameRunning)
        return;

    // RAII, so the pause is lifted on every path out of exec() - the two
    // buttons, Esc, the title bar's close, and a throw. A pause that outlived
    // this scope would leave the game thread parked in pace() with nothing on
    // screen to release it.
    struct PauseGuard
    {
        GameController& controller;
        explicit PauseGuard(GameController& _controller) : controller(_controller)
        {
            controller.setPaused(true);
        }
        ~PauseGuard() { controller.setPaused(false); }
    };

    const PauseGuard guard(controller);

    PauseDialog dialog(this);

    if(dialog.exec() != PauseDialog::MainMenu)
        return;

    // SET BEFORE THE STOP, exactly as closeEvent() sets `closing` before its
    // own - onGameStopped() closes the window for any stop it cannot account
    // for, and this is the second such stop the client has ever produced.
    returningToMenu = true;

    // The guard lifts the pause as this scope ends, and stop() has already woken
    // the paced thread with cancelPacing() by then - so neither depends on the
    // other having run first.
    controller.stop();

    gameRunning = false;
    showMainMenu();
}

void MainWindow::capScoreboard()
{
    if(!scoreboard)
        return;

    // A THIRD OF THE WINDOW at most, which is about what section 12's mockup
    // gives it. Never below the panel's own minimum, which is the numbers in
    // full plus enough name to elide - past that the panel would be lying
    // rather than merely cramped.
    const int wanted = width() / 3;
    const int floor = scoreboard->minimumSizeHint().width();

    scoreboard->setMaximumWidth(std::max(wanted, floor));
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    capScoreboard();
}

// THE LOG SURVIVES PHASE B, demoted rather than deleted. A5 asked for this
// outright: it is the thing to reach for when something looks wrong on the
// table, and the elapsed column in it is the only record of the gaps between
// frames - which is what settled two pacing questions in A5 and is about to be
// asked to settle a third.
//
// A QDockWidget rather than a widget in the central layout, for one reason worth
// the class: toggleViewAction() IS the View menu's Log entry. It is already
// checkable, already named, already keeps itself in sync with the dock being
// closed from its own title bar, and it means this window holds no "is the log
// showing" state of its own to get wrong.
void MainWindow::buildLogDock()
{
    logDock = new QDockWidget(tr("Log"), this);
    logDock->setObjectName(QStringLiteral("logDock"));
    logDock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::TopDockWidgetArea);

    // Reparented out of the .ui's central layout and into the dock. Every
    // existing `ui->log` call site keeps working, which is the point of moving
    // the widget rather than replacing it.
    logDock->setWidget(ui->log);

    addDockWidget(Qt::BottomDockWidgetArea, logDock);

    // Hidden by default: section 12's window is four regions and a log is not
    // one of them. It is a diagnostic, and a diagnostic that is always on screen
    // is just furniture.
    logDock->hide();
}

void MainWindow::buildViewMenu()
{
    // The menu bar is in MainWindow.ui and is empty; this is the first thing put
    // into it. In code rather than in the .ui because the action needs a
    // connection either way, and a designer-declared action that is only ever
    // reached from here would be split across two files for nothing.
    QMenu* view = menuBar()->addMenu(tr("&View"));

    // Qt's own action for the dock, not one of ours - see buildLogDock().
    QAction* logAction = logDock->toggleViewAction();
    logAction->setText(tr("&Log"));
    view->addAction(logAction);

    view->addSeparator();

    QAction* cardArtAction = view->addAction(tr("&Card art..."));
    connect(cardArtAction, &QAction::triggered, this, &MainWindow::showCardArt);
}

void MainWindow::showCardArt()
{
    if(!cardArt)
        cardArt = new CardArtWindow(this);

    cardArt->show();

    // Both, and in this order: show() on an already-open window that is behind
    // another one does nothing visible, and a minimized one needs the state
    // cleared before raising means anything.
    cardArt->setWindowState(cardArt->windowState() & ~Qt::WindowMinimized);
    cardArt->raise();
    cardArt->activateWindow();
}

void MainWindow::startGame(GameSetup setup, std::optional<Seat> humanSeat, Pacing pacing)
{
    // CLEARED HERE, not in the constructor: the elapsed column is measured from
    // a game's first snapshot, so the origin has to be re-armed by whatever
    // starts a game rather than once by whatever builds the window.
    haveStarted = false;
    haveShownStatus = false;

    // AND THE LOG WITH IT, for the same reason. The elapsed column only means
    // anything against the origin it was measured from, so leaving the previous
    // game's lines in place puts a [ 412.900] immediately above a [   0.000]
    // with nothing to say the clock restarted - and reading a gap across that
    // boundary is the one thing the column exists for.
    ui->log->clear();

    // EVERYTHING BELOW IS GAME ONE'S, and none of it was cleared before D1
    // because phase A never started a second game. The controller resets nothing
    // in place - prepare() builds a fresh bridge and provider - but the WINDOW is
    // reused whole, and every one of these outlives a game that has ended.
    //
    // The two cached prompts matter most: they name a promptId belonging to a
    // provider prepare() is about to destroy. The generation gate would drop a
    // click carrying one (section 4.2), so this is belt and braces rather than
    // the defence - but a stale PlayPrompt is also what the prompt bar and the
    // fan would be redrawn from.
    activeBetPrompt = BetPrompt{};
    activePrompt = PlayPrompt{};
    lastSnapshot.reset();

    bidStrip->hide();
    hand->clear();
    hand->setPlayable(false);
    promptBar->setFields(promptbar::idle());

    // Cleared here rather than in showPauseMenu(), so that the flag is false for
    // exactly one stop and cannot survive into a game it was not raised for.
    returningToMenu = false;
    gameRunning = true;

    showTable();

    // Replaced by the first snapshot, which arrives from inside start() below -
    // so this is on screen only for as long as the deal takes.
    statusBar()->showMessage(tr("Dealing..."));

    // A REAL SEAT since C1, where phases A and B passed std::nullopt. The one
    // optional decides three things at once and they must not be separable:
    // GameBridge stamps it onto every snapshot as humanSeat, which is what gives
    // canSeeHand() a viewer to be asked about; GameController builds a
    // QtMoveProvider from it and fills that seat in start(); and botBeat() reads
    // it to choose which beat to pace at.
    //
    // THE GAME IS SLOWER FROM HERE, and correctly so. Pacing{} is unchanged, but
    // botBeat() picks spectatorBeat (250ms) only when nobody is seated and
    // beat (700ms) when somebody is - section 4.1's split, which phases A and B
    // only ever saw one side of. A spectator watches; a player is waited for.
    // FROM THE DIALOG SINCE D1, both of them. The one optional decides three
    // things at once and they must not be separable: GameBridge stamps it onto
    // every snapshot as humanSeat, which is what gives canSeeHand() a viewer;
    // GameController builds a QtMoveProvider from it and fills that seat in
    // start(); and botBeat() reads it to choose which beat to pace at.
    //
    // NO MODAL DIALOG MAY RUN BETWEEN prepare() AND HERE, and that is the one
    // ordering trap in the reuse. prepare() calls stop() and then purges this
    // object's posted MetaCalls - and THAT PURGE is what drops game one's queued
    // gameStopped before it reaches onGameStopped(). Anything spinning the event
    // loop in that gap delivers the relay instead, and the window closes.
    controller.prepare(humanSeat, pacing);
    controller.start(std::move(setup));
}

MainWindow::~MainWindow()
{
    // BEFORE `ui` is deleted below. The destructor body runs before any member
    // is destroyed, so leaving the join to ~GameController would tear the log
    // widget down while the game thread was still publishing into it.
    controller.stop();

    delete ui;
}

void MainWindow::appendLine(const QString& line)
{
    // append() rather than setPlainText(): the document grows to a few thousand
    // lines over a full game and rebuilding it every frame would be the one slow
    // thing in the process.
    //
    // AND IT ALREADY FOLLOWS THE TAIL. QPlainTextEdit::appendPlainText() notes
    // whether the view was at the bottom before inserting and scrolls only if it
    // was, which is exactly the behaviour wanted here: a reader sitting at the
    // bottom is following the game, and one who has scrolled up is reading
    // something and must not be yanked back within half a second. Doing it again
    // around this call is the same logic written twice, on a slightly different
    // test - Qt's also requires the widget to be visible, and this window starts
    // its game before main() calls show().
    ui->log->appendPlainText(line);
}

void MainWindow::render(std::shared_ptr<const GameSnapshot> snapshot)
{
    if(!snapshot)
        return;

    const auto now = std::chrono::steady_clock::now();

    // From the FIRST snapshot rather than from process start: what is being
    // read off this column is the gap between frames, and a constant offset
    // only makes them harder to subtract by eye.
    if(!haveStarted)
    {
        started = now;
        haveStarted = true;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - started);

    appendLine(snapshotlog::format(*snapshot, elapsed));

    lastSnapshot = snapshot;

    // THE TABLE AND THE LOG READ THE SAME FRAME, which is what makes the log a
    // diagnostic for the table rather than a second opinion about it: a card in
    // the wrong place with the right card in the log is a layout bug, and the
    // two being on one line together is what says so.
    //
    // The fan goes with it, inside setSnapshot(): section 4.4's "DISABLED,
    // always". A snapshot's hand is what a player LOOKS at between turns - it
    // carries no legality and is empty in a blind round, so it can never be the
    // thing they click.
    scene->setSnapshot(snapshot);
    hand->setPlayable(false);
    infoStrip->setSnapshot(*snapshot);
    scoreboard->setSnapshot(*snapshot);

    updateStatus(*snapshot);
}

void MainWindow::updateStatus(const GameSnapshot& snapshot)
{
    // Only when it moved. render() runs on every frame a game publishes - about
    // 1300 of them - and the status bar says nothing new on most of those, so
    // building the string every time would be work nobody reads. The round type
    // is not compared: it changes only when the round does.
    if(haveShownStatus && snapshot.roundNumber == shownRound
                       && snapshot.trickNumber == shownTrick)
    {
        return;
    }

    haveShownStatus = true;
    shownRound = snapshot.roundNumber;
    shownTrick = snapshot.trickNumber;

    // Trick 0 is a real state - dealt and bid, nothing played yet - rather than
    // an off-by-one, so it is named rather than printed as "0/8".
    const QString trick = snapshot.trickNumber == 0
                              ? tr("bidding")
                              : tr("trick %1/%2").arg(snapshot.trickNumber)
                                                 .arg(snapshot.trickCount);

    // THE ROUND TYPE COMES FROM infostrip, not from a switch of its own. This
    // window had a third spelling of the same three words until B2b: the log's
    // fixed ASCII, the info strip's, and this. Two is the right number and the
    // comment on snapshotlog says why - one for machines, one for people - so
    // the people-facing one is shared and the log's stays separate.
    statusBar()->showMessage(tr("Round %1/%2 - %3 (%4)")
                                 .arg(QString::number(snapshot.roundNumber),
                                      QString::number(snapshot.roundCount),
                                      trick,
                                      infostrip::roundTypeText(snapshot.roundType)));
}

// THE GAME THREAD IS PARKED IN makeBet() FROM HERE UNTIL onBidChosen(), which is
// an unbounded stretch - as long as the player takes to decide.
//
// THAT IS WHY THE STRIP IS A PANEL AND NOT A QDialog. exec() would spin a nested
// event loop for the whole of that wait and take the close button with it:
// closeEvent() would not run, so stop() would never be called, so abandon()
// would never wake the provider. C5 is the test for exactly that path and a
// modal dialog makes it unperformable. See section 4.4.
void MainWindow::onBetRequested(BetPrompt prompt)
{
    appendLine(snapshotlog::format(prompt));

    // BY VALUE, and this is the first cached prompt in the client. C1 answered
    // from inside this function, so nothing could be stale; the answer now
    // arrives whenever the player clicks, and the promptId is what has to
    // survive until then.
    activeBetPrompt = prompt;

    bidStrip->setPrompt(prompt);
    bidStrip->show();

    // So the digit keys work without the player clicking the strip first.
    // Section 15.1 asks for them and a strip that never gets focus never sees a
    // key press - which looks finished, because the buttons still work.
    bidStrip->setFocus();
}

void MainWindow::onBidChosen(unsigned int bet)
{
    bidStrip->hide();

    // NULL IN A SPECTATOR GAME, which this window no longer starts but phase D
    // can start in the same process - "watching" is one of the setup dialog's
    // options. A spectator raises no prompts, so this is unreachable rather than
    // merely rare; it is checked because the alternative to an unreachable
    // branch is an unchecked dereference.
    if(!controller.provider())
        return;

    // THE promptId IS THE GATE, not the hiding above. submit() drops an answer
    // naming any generation but the live one, which is what stops a stray click
    // - a double click, or one that lands after the game has moved on - from
    // arming the NEXT prompt with an answer chosen for this one. Hiding the
    // widget narrows that window; only the generation closes it (section 4.2).
    controller.provider()->submit(activeBetPrompt.promptId, bet);
}

// THE ENGINE IS PARKED IN playCard() FROM HERE UNTIL onCardPicked(), which is
// the other half of the wait section 4.4 is about - and the one C5's remaining
// case closes over, because it is a different park from the bid's.
void MainWindow::onCardRequested(PlayPrompt prompt)
{
    appendLine(snapshotlog::format(prompt));

    // BY VALUE, and it is handIndex that has to survive rather than only the
    // promptId: the mapping from what the player pointed at back to what the
    // engine holds travels with the prompt and with nothing else.
    activePrompt = prompt;

    // THE SECOND SOURCE. Between turns render() draws this fan from the
    // snapshot, disabled; this is the redraw that gives it legality and makes it
    // clickable - and in a blind round, the backs that are all there is to draw
    // (section 17).
    hand->setPrompt(prompt);
    hand->setPlayable(true);

    promptBar->setFields(promptbar::fieldsFor(prompt, lastSnapshot ? *lastSnapshot : GameSnapshot{}));
}

void MainWindow::onCardPicked(std::size_t slot)
{
    // THE UI'S OWN RULE ABOUT WHAT A CLICK MEANS, and not the same check as the
    // provider's. HandItem refuses an illegal slot already; this is the window
    // saying so too, because the digit keys reach here by another road and a
    // guard that lives only in the item is one the keyboard can walk around.
    // The provider checks again for its own reasons - it will not hand the engine
    // something it would throw on.
    //
    // Written around `entries` being empty rather than assuming it is filled: a
    // blind round has no entries at all and every slot is playable.
    if(!activePrompt.entries.empty() &&
       (slot >= activePrompt.entries.size() || !activePrompt.entries[slot].legal))
    {
        return;
    }

    // Never empty in a raised prompt - playCard() returns without raising one
    // when there is nothing legal to play, and a blind round still carries one
    // position for the card the player may not look at. Guarded because the
    // indexing below is what "the card played is not the card clicked" is made
    // of, and an out-of-range read is the loudest possible version of it.
    if(slot >= activePrompt.handIndex.size())
        return;

    hand->setPlayable(false);
    promptBar->setFields(promptbar::idle());

    if(!controller.provider())
        return;

    // THROUGH handIndex, never the slot itself. A display slot is a position in
    // the fan; the engine wants a position in the hand AS IT GAVE IT, and the
    // prompt is the only object carrying both. Submitting the slot compiles,
    // runs, and plays a different card than the one the player pointed at -
    // which no rule in the engine would object to.
    //
    // Straight to the provider rather than through a signal: submit() touches
    // only a mutex, so it needs no event loop and the game thread wakes now
    // rather than at the next turn of one.
    controller.provider()->submit(activePrompt.promptId, activePrompt.handIndex[slot]);
}

// THE THREE ENDINGS ALL HIDE THE STRIP, and it is one line in each rather than
// one in a shared helper because they are reached from different places and two
// of them are not reached at all on the path the third is. Between them they
// close the same hole: a strip left standing over a finished game is a live
// button whose click would submit into a provider whose engine is gone. The
// promptId gate would drop that answer, so this is the belt to its braces - but
// a control that looks live and does nothing is its own defect.
void MainWindow::keyPressEvent(QKeyEvent* event)
{
    // BEFORE THE MODIFIER GUARD AND BEFORE THE DIGITS. Esc opens the pause
    // dialog, which is the keyboard half of the prompt bar's Menu button - and
    // it is checked first because the digit path below subtracts Key_1 from
    // whatever arrives and would otherwise have to reason about the result.
    if(event->key() == Qt::Key_Escape)
    {
        showPauseMenu();
        return;
    }

    // UNMODIFIED DIGITS ONLY, the guard C2's review put on the bid strip and for
    // the same reason: a played card is irrevocable, and Ctrl+1 belongs to
    // whatever the window binds it to. The keypad's own modifier is masked back
    // in, because keypad 1 is the same 1 to the person pressing it.
    if((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // ONE-BASED, which is section 15.2's own numbering and the terminal's: the
    // leftmost card is 1, not 0. The bid strip's digits are ZERO-based because a
    // bid of zero is a real bid - the two are different questions and the
    // off-by-one between them is deliberate rather than an oversight.
    const int typed = event->key() - Qt::Key_1;

    if(typed < 0 || !hand)
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // ASKED OF THE FAN, not re-decided here. isPlayable() is the rule the mouse
    // goes through, so a key cannot play a card a click would refuse.
    if(!hand->isPlayable(static_cast<std::size_t>(typed)))
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    // Through activate() rather than straight to onCardPicked(), so both roads
    // run the item's own rule and the fan can never be told about a play it did
    // not make.
    hand->activate(static_cast<std::size_t>(typed));
}

void MainWindow::onGameStopped()
{
    bidStrip->hide();
    hand->setPlayable(false);
    promptBar->setFields(promptbar::idle());

    appendLine(tr("-- stopped --"));

    // The close this window started: closeEvent() has already accepted, and
    // calling close() again from here would re-enter it.
    if(closing)
        return;

    // THE SECOND STOP THIS WINDOW CAUSES, and D1 is what introduced it. Without
    // this branch, abandoning a game to the main menu closes the application -
    // the flag above is the only thing that ever guarded that, and phase A had
    // no other producer to guard against.
    if(returningToMenu)
        return;

    // A stop from somewhere else again - now genuinely nothing, since both of
    // this client's own stops are accounted for above. Kept because leaving the
    // branch out would make the first caller that does produce one look like a
    // hang.
    close();
}

void MainWindow::onGameOver(std::vector<Standing> standings, std::vector<Standing> winners)
{
    bidStrip->hide();
    hand->setPlayable(false);
    promptBar->setFields(promptbar::idle());

    appendLine(tr("-- game over --"));

    for(const Standing& row : standings)
    {
        // One arg() taking three, not three chained: a player who names
        // themselves "%3" would otherwise have their own name swallow the score
        // that follows it. Same reason as SnapshotLog.cpp's table entries.
        appendLine(tr("   %1. %2  %3")
                       .arg(QString::number(row.place),
                            QString::fromStdString(row.name),
                            QString::number(row.score)));
    }

    // getWinners(), not standings.front(). A drawn game has more than one
    // winner and the engine is what decides that; reading the first row quietly
    // names one of them, which is a bug that shipped once already in the
    // terminal client's 2.1.0.
    QStringList names;
    for(const Standing& winner : winners)
        names << QString::fromStdString(winner.name);

    statusBar()->showMessage(names.size() == 1
                                 ? tr("%1 wins").arg(names.front())
                                 : tr("Drawn between %1").arg(names.join(tr(", "))));

    // THE GAME IS OVER BUT THE TABLE STAYS UP, which is what section 16.2's
    // panel is drawn over and what D2 builds. Until then the standings are in
    // the status bar and the log, and the way on is the Menu button.
    //
    // Cleared so that the pause cannot be opened on a finished game: there is
    // nothing to hold, and Resume would promise a game that has already ended.
    gameRunning = false;
}

void MainWindow::showFailure(QString what)
{
    // Neither onGameStopped() nor onGameOver() fires on this path, so the flag
    // they clear has to be cleared here too - otherwise the pause dialog is
    // still reachable on a game whose thread has already unwound.
    gameRunning = false;

    bidStrip->hide();
    hand->setPlayable(false);
    promptBar->setFields(promptbar::idle());

    appendLine(tr("-- the game thread failed: %1 --").arg(what));
    statusBar()->showMessage(tr("The game ended in an error: %1").arg(what));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // SET FIRST, before stop(). stop() ends the game, which produces
    // gameStopped, which reaches onGameStopped() - and that slot has to be able
    // to tell this close from one it should start itself.
    closing = true;

    // Ends the game and joins the thread. Idempotent, so ~MainWindow calling it
    // again costs nothing.
    controller.stop();

    event->accept();
}
