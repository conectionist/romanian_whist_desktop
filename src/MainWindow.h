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

class CardArtWindow;

// Phase A's window: a QTextEdit and nothing else.
//
// That is not a placeholder to apologise for, it is what makes the phase
// diagnostic. With no scene, no card art and no layout in the process, a wrong
// card in the log MUST be a snapshot bug - there is nothing else it could be.
// Phase B draws the table and demotes this widget behind a View menu rather
// than deleting it, because it is the thing to reach for when something looks
// wrong on that table later.
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

    // Builds the View menu, which in B1 holds one action. The log's own View
    // toggle that A5 asked for is NOT here yet and waits for B2: the log is
    // still the only thing in this window, and an action that empties the
    // window is not a diagnostic, it is a way to lose one.
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
