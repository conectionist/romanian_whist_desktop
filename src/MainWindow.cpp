#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "ui/SnapshotLog.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/GameEngine.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>

#include <QCloseEvent>
#include <QPlainTextEdit>
#include <QScrollBar>
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
    // Hidden one, and those two rounds are where a snapshot-driven hand stops
    // working. Phase A wants to see them.
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

    setWindowTitle(tr("Romanian Whist - spectating"));
    statusBar()->showMessage(tr("Dealing..."));

    // std::nullopt, NOT an arbitrary seat. Pass one and that bot silently plays
    // without a beat, and canSeeHand() is asked about a viewer who is not
    // there. Pacing{} is right as it stands - botBeat() picks spectatorBeat on
    // its own when there is no human seat.
    controller.prepare(std::nullopt, Pacing{});
    controller.start(buildSpectatorSetup());
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
    // lines over a full game and rebuilding it every frame would be the one
    // slow thing in the process.
    ui->log->appendPlainText(line);

    // Follow the tail. The scrollbar is what a reader is actually watching, and
    // a log that stops following after the first screen is a log nobody reads.
    QScrollBar* bar = ui->log->verticalScrollBar();
    bar->setValue(bar->maximum());
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
        appendLine(tr("   %1. %2  %3")
                       .arg(row.place)
                       .arg(QString::fromStdString(row.name))
                       .arg(row.score));
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
