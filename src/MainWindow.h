#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "core/GameSnapshot.h"
#include "engine/GameController.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QMainWindow>
#include <QString>

#include <chrono>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

QT_BEGIN_NAMESPACE
class QDockWidget;
QT_END_NAMESPACE

class CardArtWindow;
class InfoStrip;
class ScoreboardWidget;
class TableScene;
class TableView;

// Phase C1's window: phase B's table, with a human in one of the seats.
//
// The seat is the whole of C1. Everything the human needs to ANSWER a prompt -
// the bid strip and the clickable hand - is C2 and C3, so between here and there
// the two slots below answer for them: see src/ui/PromptStandIn.h for why that
// is a step rather than a shortcut.
//
// Phase B's window: the table, with phase A's log demoted behind View > Log.
//
// The log is kept rather than deleted, which A5 asked for and B2 does: it is the
// thing to reach for when something looks wrong on the table, and it is the only
// place the gaps BETWEEN frames are recorded - which is what settled two of
// phase A's pacing questions and what B2 uses to settle the third.
//
// Both read the same snapshot, in that order, in render(). That is what makes
// the log a diagnostic for the table rather than a second opinion about it: a
// card drawn in the wrong place whose log line is right is a layout bug and
// nothing else, which is the property phase A's ugliness bought and phase B
// spends.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // Closing the window is what ends the game. Sets `closing` first, then
    // stops - see the flag below for why the order matters.
    void closeEvent(QCloseEvent* event) override;

    // Caps the scoreboard's share of the window - see capScoreboard().
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void render(std::shared_ptr<const GameSnapshot> snapshot);

    // NOT connected to close() directly. Closing the window calls stop(),
    // stop() is what produces gameStopped, and close() from here would re-enter
    // closeEvent(). This checks `closing` and forces the close only when the
    // stop came from somewhere else - which in phase A is nowhere, and in phase
    // C is a game abandoned from the far side.
    void onGameStopped();

    void onGameOver(std::vector<romanian_whist::Standing> standings,
                    std::vector<romanian_whist::Standing> winners);

    // The run slot caught something. Neither onGameStopped() nor onGameOver()
    // fires on that path, so this is the only report the window gets.
    void showFailure(QString what);

    // THE ENGINE IS PARKED IN THE PROVIDER WHILE EITHER OF THESE RUNS, waiting
    // on the submit() at the end of it. Both take their prompt BY VALUE, which
    // is what a queued signal delivers and what C2 and C3 will cache.
    //
    // C1 answers inline and caches nothing, so there is no activePrompt member
    // here yet. That is not an omission: nothing in this phase can answer LATER
    // than the signal, and a cached prompt with no click to outlive is a member
    // that could only be stale. C3 adds it with the widget that needs it.
    //
    // Answering from inside the emission is sanctioned rather than merely
    // tolerated - QtMoveProvider drops its lock across the emit precisely so
    // that submit() can be called on the emitting thread. See its header.
    void onBetRequested(BetPrompt prompt);
    void onCardRequested(PlayPrompt prompt);

private:
    void appendLine(const QString& line);

    // Section 12's window: the info strip across the top, the table and the
    // scoreboard side by side beneath it. Phase B is this.
    //
    // The central widget was the bare TableView until B2b, with no layout at
    // all, so this introduces the container the other two hang off.
    void buildTable();

    // Moves the .ui's log into a dock, hidden by default. B2's half of A5's
    // "keep the QTextEdit, put it behind a View menu" - which could not be done
    // in B1, because the log was still the only thing in the window and an
    // action that empties the window is not a diagnostic.
    void buildLogDock();

    // Builds the View menu. Must run AFTER buildLogDock(), which owns the
    // action this adds.
    void buildViewMenu();

    // HOW THE WINDOW DIVIDES, which is this window's decision and not the
    // panel's. ScoreboardWidget asks for the width its widest name and its
    // headings need, and that is right of it - but at a small window that width
    // is half the screen, and section 12 has the table as the region everything
    // else is arranged around. The panel yields; the green does not.
    void capScoreboard();

    // Shows the card-art window, creating it the first time. Parented to this
    // window with Qt::Window, so it is a top-level window that still dies with
    // its owner rather than outliving the client.
    void showCardArt();

    // Prepares the controller and starts a game, and - the reason this is a
    // function rather than two calls in the constructor - clears the elapsed
    // origin first. That origin belongs to a GAME, not to the window: leaving
    // it set would make a second game's elapsed column continue from the first
    // one's start. Phase A never starts a second game, so this is the one place
    // phase D's setup dialog has to go through for that to stay true.
    void startGame(romanian_whist::GameSetup setup);

    // The status bar, from the snapshot - but only when it would actually
    // change. See the shown* members below.
    void updateStatus(const GameSnapshot& snapshot);

    Ui::MainWindow *ui;

    // ONE PALETTE, owned here and handed to all three, rather than each widget
    // resolving the system scheme for itself. Phase E's preference is a single
    // setter on this member and three calls out of it.
    Theme theme = Theme::light();

    // The table and the two panels. Qt owns every one of them through the
    // widget tree, so these are handles rather than ownership.
    TableScene* scene = nullptr;
    TableView* table = nullptr;
    InfoStrip* infoStrip = nullptr;
    ScoreboardWidget* scoreboard = nullptr;

    // Holds the .ui's log widget from buildLogDock() onward.
    QDockWidget* logDock = nullptr;

    // Null until View > Card art is first chosen. Qt owns it through the parent,
    // so this is a handle for raising the existing window rather than opening a
    // second one - not an ownership claim.
    CardArtWindow* cardArt = nullptr;

    // Zero until the first snapshot arrives, which is what the log's elapsed
    // column is measured from. Cleared by startGame() above.
    std::chrono::steady_clock::time_point started;
    bool haveStarted = false;

    // What the status bar was last told, so it is rewritten only when the round
    // or the trick moves rather than on every one of a game's ~1300 frames.
    // haveShownStatus is what tells "round 0" apart from "nothing shown yet".
    bool haveShownStatus = false;
    unsigned int shownRound = 0;
    unsigned int shownTrick = 0;

    // Set by closeEvent() before it stops the game, so onGameStopped() can tell
    // a stop it caused from one it did not.
    bool closing = false;

    // DECLARED LAST, so it is destroyed FIRST: ~GameController joins the game
    // thread, and that has to happen before the widgets its slots write to are
    // gone. ~MainWindow stops it explicitly as well, because the destructor
    // BODY runs before any member is destroyed - deleting `ui` there would
    // otherwise outrun the join.
    GameController controller;
};
#endif // MAIN_WINDOW_H
