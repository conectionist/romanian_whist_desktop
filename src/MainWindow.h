#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "core/GameSnapshot.h"
#include "engine/GameController.h"

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
class TableScene;
class TableView;

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

private:
    void appendLine(const QString& line);

    // The scene and its view, into the central widget. Phase B is this.
    void buildTable();

    // Moves the .ui's log into a dock, hidden by default. B2's half of A5's
    // "keep the QTextEdit, put it behind a View menu" - which could not be done
    // in B1, because the log was still the only thing in the window and an
    // action that empties the window is not a diagnostic.
    void buildLogDock();

    // Builds the View menu. Must run AFTER buildLogDock(), which owns the
    // action this adds.
    void buildViewMenu();

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

    // The round type as a PLAYER reads it, which is why it is here and not
    // shared with the log's spelling of the same three words. A second switch
    // over RoundType is a real cost - it is one more thing to remember the day a
    // fourth type is added - but the alternative is worse: the log's words are
    // fixed ASCII that tests assert on and saved logs are read with, and this
    // one has to go through tr() like every other string a user sees, or a
    // Romanian status bar reads "Runda 3/26 - levata 2/4 (Forehead)".
    QString roundTypeText(romanian_whist::RoundType type) const;

    Ui::MainWindow *ui;

    // The table. Qt owns both - the scene is parented to this window and the
    // view is the central widget - so these are handles, not ownership.
    TableScene* scene = nullptr;
    TableView* table = nullptr;

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
