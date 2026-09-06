#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "ui/CardArtWindow.h"
#include "ui/SnapshotLog.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <QCloseEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <utility>

using namespace romanian_whist;

namespace
{
// Phase A's game, hardcoded. Phase D replaces this with the setup dialog.
//
// A FIXED SEED, so a misread frame is reproducible: the same deal and the same
// bot choices every run, which is what makes "that card is wrong" a thing that
// can be looked at twice.
constexpr std::uint32_t kSeed = 42;

GameSetup buildSpectatorSetup()
{
    GameSetup setup;

    // Deterministic strategies only. RandomCardStrategy would need its own seed
    // to be reproducible at all, and its draw count depends on what every other
    // seat bids - so any behavioural change desyncs its stream and turns a
    // located difference into "something changed somewhere".
    setup.seats.push_back(SeatSetup{ "Ana",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Bogdan",
        std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Cristina",
        std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>()) });
    setup.seats.push_back(SeatSetup{ "Dan",
        std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()) });

    setup.structure = GameStructure::S_181;

    // Left at its default deliberately: it appends a Forehead round and then a
    // Hidden one. In THIS phase they differ only in the round-type column - the
    // log prints no hand, and a spectator game fills none for anyone anyway
    // (Snapshot.cpp populates seat hands only once a human is seated). So the
    // reason to keep them is narrower than a hand: a client that called every
    // round Normal would hide the two rounds phase C stalls in, and the column
    // is where that would show. Whether a hand survives them is phase B's
    // question, asked once there is a table to draw one on.
    setup.endWithForeheadAndHidden = true;

    setup.shuffleSeed = kSeed;

    return setup;
}
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

    // NO UNDO HISTORY. The log is read-only and append-only, and a full game
    // appends about 1300 lines - each one recorded as an undo command nothing in
    // this window can ever replay. The unlimited maximumBlockCount in the .ui is
    // deliberate (a whole game has to stay scrollable), so capping the blocks is
    // not the lever here even though a cap would disable undo as a side effect.
    ui->log->setUndoRedoEnabled(false);

    // The title lives in MainWindow.ui, not here. Setting it in both puts a
    // string in the translation catalogue that no one can ever see the effect of
    // translating, because whichever runs second wins.

    buildViewMenu();

    // Replaced by the first snapshot, which arrives from inside start() below -
    // so this is on screen only for as long as the deal takes.
    statusBar()->showMessage(tr("Dealing..."));

    startGame(buildSpectatorSetup());
}

void MainWindow::buildViewMenu()
{
    // The menu bar is in MainWindow.ui and is empty; this is the first thing put
    // into it. In code rather than in the .ui because the action needs a
    // connection either way, and a designer-declared action that is only ever
    // reached from here would be split across two files for nothing.
    QMenu* view = menuBar()->addMenu(tr("&View"));

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

void MainWindow::startGame(GameSetup setup)
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

    // std::nullopt, NOT an arbitrary seat. Pass one and that bot silently plays
    // without a beat, and canSeeHand() is asked about a viewer who is not
    // there. Pacing{} is right as it stands - botBeat() picks spectatorBeat on
    // its own when there is no human seat.
    controller.prepare(std::nullopt, Pacing{});
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

    statusBar()->showMessage(tr("Round %1/%2 - %3 (%4)")
                                 .arg(QString::number(snapshot.roundNumber),
                                      QString::number(snapshot.roundCount),
                                      trick,
                                      roundTypeText(snapshot.roundType)));
}

QString MainWindow::roundTypeText(RoundType type) const
{
    // Its own switch rather than the log's - see the header for why the
    // duplication is the lesser cost. No default case, so adding a fourth type
    // is a warning here rather than a silent "?" in the status bar.
    switch(type)
    {
        case RoundType::Normal:   return tr("normal");
        case RoundType::Forehead: return tr("forehead");
        case RoundType::Hidden:   return tr("hidden");
    }

    return tr("unknown");
}

void MainWindow::onGameStopped()
{
    appendLine(tr("-- stopped --"));

    // The close this window started: closeEvent() has already accepted, and
    // calling close() again from here would re-enter it.
    if(closing)
        return;

    // A stop from somewhere else. Nothing in phase A produces one - there is no
    // menu and no second game - but leaving the branch out would make the first
    // caller that does produce one look like a hang.
    close();
}

void MainWindow::onGameOver(std::vector<Standing> standings, std::vector<Standing> winners)
{
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
}

void MainWindow::showFailure(QString what)
{
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
