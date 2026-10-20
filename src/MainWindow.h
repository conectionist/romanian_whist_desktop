#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "core/GameSnapshot.h"
#include "engine/GameController.h"
#include "engine/Prompts.h"
#include "engine/RoundResult.h"
#include "paint/Theme.h"
#include "ui/NewGameDialog.h"

#include <QMainWindow>
#include <QString>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

QT_BEGIN_NAMESPACE
class QDockWidget;
class QStackedWidget;
QT_END_NAMESPACE

class BidStrip;
class CardArtWindow;
class GameOverPanel;
class HandItem;
class MainMenuPage;
class PauseOverlay;
class RoundSheet;
class TableScene;
class TableView;
class TrickPanelItem;

// Phase C's window: phase B's table with a human in one of the seats, and the
// two things that let them answer for themselves - section 15.1's bid strip and
// section 14's hand fan.
//
// THE TWO PROMPTS ARE THE SHAPE OF THIS CLASS. Everything else here draws a
// snapshot and forgets it; the prompts are cached, because the engine is parked
// in the provider until a click answers, and the answer arrives long after the
// signal that asked for it.
//
// Phase B's table, with phase A's log demoted behind View > Log.
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
    // Section 15.2's digit keys: 1-8 play by position in the fan, matching the
    // terminal's numbering for anyone switching between the two clients.
    //
    // ON THE WINDOW rather than on the fan, unlike the bid strip's digits. A
    // QGraphicsObject has no focus of its own worth building here, and the fan is
    // not the only thing on screen that could want a key - so this is the one
    // place a play key is read, and it forwards to the item's own rule rather
    // than re-deciding what is playable. Arrows and Enter need a selection to
    // move, which is phase E's keyboard map.
    void keyPressEvent(QKeyEvent* event) override;

    // Closing the window is what ends the game. Sets `closing` first, then
    // stops - see the flag below for why the order matters.
    void closeEvent(QCloseEvent* event) override;

    // Keeps the overlays over the whole page - see the body.
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

    // Section 16.1's sheet, from the payload section 21.3 asked for. The only
    // slot in this window that receives a round rather than a frame - which is
    // why it is here and not down with the two prompts, whose caching story
    // below has nothing to do with it.
    void onRoundScored(RoundResult result);

    // THE ENGINE IS PARKED IN THE PROVIDER FROM EITHER OF THESE UNTIL ITS ANSWER,
    // which is as long as the player takes. Both take their prompt BY VALUE -
    // what a queued signal delivers - and both cache it, because the answer
    // arrives from a click rather than from inside the emission.
    //
    // C1 answered inline and cached nothing, which was right while nothing could
    // answer later than the signal. C2 added activeBetPrompt with the panel that
    // needed it and C3 activePrompt with the fan, and in both the cached promptId
    // is what actually gates a stale submission - not the widget being disabled.
    void onBetRequested(BetPrompt prompt);
    void onCardRequested(PlayPrompt prompt);

    // A card in the fan was clicked, or a digit was pressed. Answers the prompt
    // cached by onCardRequested(), through the one mapping that decides whether
    // the card played is the card clicked.
    void onCardPicked(std::size_t slot);

    // The bid strip was clicked, or a digit was pressed on it. Answers the
    // prompt cached by onBetRequested().
    void onBidChosen(unsigned int bet);

    // Section 13's dialog, opened from the main menu. Builds a GameSetup from
    // what it was told and starts a game on it - which for the second and every
    // later game is section 4.3's ordering run again from the top.
    void showNewGameDialog();

    // THE PAUSE, raised. Delegates to setPauseMenuVisible() below; it exists as
    // its own slot because that is what the menu bar's Menu item connects to.
    void showPauseMenu();

    // Abandons the running game and returns to the main menu. The second stop
    // this window produces, and the reason `returningToMenu` exists.
    void abandonToMenu();

private:
    void appendLine(const QString& line);

    // Section 12's window: the table, with the scoreboard, the trick panel and the
    // corner buttons pinned inside it since the table redesign's phase 8.
    void buildTable();

    // Moves the .ui's log into a dock, hidden by default. B2's half of A5's
    // "keep the QTextEdit, put it behind a View menu" - which could not be done
    // in B1, because the log was still the only thing in the window and an
    // action that empties the window is not a diagnostic.
    void buildLogDock();

    // Builds the menu bar: the temporary Menu item and the View menu. Must run
    // AFTER buildLogDock(), which owns an action this adds.
    void buildViewMenu();

    // Shows the card-art window, creating it the first time. Parented to this
    // window with Qt::Window, so it is a top-level window that still dies with
    // its owner rather than outliving the client.
    void showCardArt();

    // The two pages of section 12's window. The menu is what the client opens
    // on and what abandoning a game returns to; the table is everything phases B
    // and C built, kept whole between games rather than rebuilt.
    void showMainMenu();
    void showTable();

    // SHOWS THE PAUSE MENU AND HOLDS THE GAME, OR NEITHER. The two are set here
    // together and nowhere else, so no caller can put them out of step - the
    // hardening phase E's box asks for on the fan, applied to the pause before
    // it has had a chance to go wrong.
    //
    // This replaced an RAII guard around a modal QDialog::exec(). The dialog
    // went because Qt drops window-system events to a window a modal blocks, so
    // the title bar's X did nothing while the pause was up - see PauseOverlay.h.
    void setPauseMenuVisible(bool visible);

    // THE ONLY CALLER OF GameController::setPaused(). Two things can want a game
    // held now - the pause menu and the round sheet - and two callers writing
    // one flag is the defect setPauseMenuVisible() was shaped to prevent. See
    // the body.
    void applyHold();

    void showRoundSheet(const RoundResult& result);
    void hideRoundSheet();

    // Raises section 16.2's panel over the table it was filled from. The three
    // callers are the two endings - onGameOver() and showFailure() - and a Play
    // again the player then cancelled, which must put back what it took down.
    //
    // FILLS NOTHING. The panel keeps its face between a hide and a show, so this
    // is geometry, show, raise and focus - the same four the pause menu does.
    void showGameOverPanel();

    // Prepares the controller and starts a game, and - the reason this is a
    // function rather than two calls in the constructor - clears the elapsed
    // origin first. That origin belongs to a GAME, not to the window: leaving
    // it set would make a second game's elapsed column continue from the first
    // one's start.
    //
    // AND SINCE D1, EVERYTHING ELSE A SECOND GAME INHERITS. The controller
    // resets nothing in place, but this window is reused whole - the two cached
    // prompts, the last snapshot, the bid strip's visibility and the fan all
    // belong to the game that has ended. See the body for the list.
    //
    // The seat and the pacing come from the dialog rather than from a literal,
    // which is the whole of what D1 changed about starting a game.
    //
    // A GameDeal RATHER THAN A GameSetup, because a seat's strategy can also be
    // an observer the engine has to be given before it starts - see GameDeal in
    // engine/GameController.h. This window only carries it from buildDeal() to
    // start(); it registers nothing and looks inside nothing.
    void startGame(GameDeal deal,
                   std::optional<romanian_whist::Seat> humanSeat,
                   Pacing pacing);

    Ui::MainWindow *ui;

    // ONE PALETTE, owned here and handed to everything, rather than each widget
    // resolving the system scheme for itself. Phase E's preference is a single
    // setter on this member and a call out of it per widget.
    Theme theme = Theme::light();

    // The table. Qt owns it through the widget tree, so these are handles rather
    // than ownership. The scoreboard and the trick panel are items in the scene.
    TableScene* scene = nullptr;
    TableView* table = nullptr;

    // Section 15.1's strip, floated over the bottom of the table rather than
    // laid out beneath it - see TableView::setBottomOverlay() for why. Hidden
    // except while the engine is parked waiting for your bid.
    BidStrip* bidStrip = nullptr;

    // SECTION 12'S FOURTH REGION, "where the game talks to you", which is the
    // trick panel in the table's top-right corner since the redesign's phase 8.
    // Owned by the scene; a handle, as `hand` is, for the same second source.
    TrickPanelItem* trickPanel = nullptr;

    // The pause menu, a child of the stack rather than a page in it or a dialog
    // of its own - it covers whichever page is showing and dims it. Its
    // visibility and GameController's pause are one fact; see
    // setPauseMenuVisible().
    PauseOverlay* pauseOverlay = nullptr;

    // Section 16.2's panel, the same shape as the pause menu and for the same
    // reason - it is drawn OVER the final state rather than replacing it, which
    // is what section 4.1's "publish the last snapshot before emitting gameOver"
    // exists to make possible.
    GameOverPanel* gameOverPanel = nullptr;

    // Section 16.1's sheet, raised over the round it reports.
    RoundSheet* roundSheet = nullptr;

    // THE FAN, owned by the scene rather than by this window - it is a
    // QGraphicsObject in the table, not a widget. Held as a handle because this
    // is what drives its second source: the snapshot reaches it through
    // TableScene, and the PROMPT reaches it from here.
    HandItem* hand = nullptr;

    // THE PROMPT THE STRIP IS CURRENTLY SHOWING, held by value.
    //
    // C1 deliberately had no such member, because it answered inline from inside
    // the signal and nothing could be a turn out of date. C2 answers whenever
    // the player clicks, which is exactly when a cached promptId starts earning
    // its keep - and it is the promptId, not the widget being hidden, that
    // actually gates a stale submission (section 4.2). C3 adds the play-side
    // twin on the same reasoning.
    BetPrompt activeBetPrompt;

    // THE PLAY-SIDE TWIN, and the second cached prompt in the client. Same
    // reasoning as the bet one: the answer arrives whenever the player clicks, so
    // the promptId and - decisively - handIndex have to survive until then.
    //
    // NEVER READ FROM A SNAPSHOT INSTEAD. A snapshot's hand carries no legality
    // and is empty in a blind round, so it cannot supply either (section 4.0).
    PlayPrompt activePrompt;

    // Section 12's window, as two pages. Page 0 is the menu and page 1 the
    // table composite buildTable() assembles.
    QStackedWidget* pages = nullptr;
    MainMenuPage* menuPage = nullptr;

    // WHAT THE LAST GAME WAS DEALT FROM, so New Game pre-fills with it - section
    // 16.2's "play again reopens the setup dialog pre-filled with the same
    // settings". In memory rather than in QSettings, which is section 20 and
    // phase E.
    newgame::Config lastConfig = newgame::defaults();

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

    // Set by closeEvent() before it stops the game, so onGameStopped() can tell
    // a stop it caused from one it did not.
    bool closing = false;

    // ITS TWIN, and D1 is what made a second one necessary. Set by
    // showPauseMenu() before ITS stop, for the same reason and read in the same
    // place: without it, abandoning a game to the main menu closes the
    // application. Cleared by startGame(), so it is true for exactly one stop.
    bool returningToMenu = false;

    // THE TWO REASONS A GAME CAN BE HELD, kept apart so that releasing one does
    // not release the other. applyHold() is what turns them into the single flag
    // GameBridge owns.
    bool pauseMenuHolding = false;
    bool roundSheetHolding = false;

    // Does the round sheet WAIT? From the dialog, per game - section 16.1's
    // dismissal, and off by default.
    bool waitAfterEachRound = false;

    // WHAT THE TABLE IS SHOWING, and the three states are not two. This was a
    // bool through D1 and the bool is what put the client in a dead end: a
    // finished game cleared it, the pause menu is gated on it, and so Menu and
    // Esc both did nothing on the one screen that needed a way off it.
    //
    // "Is there a game to hold" and "has a game ended" are different questions -
    // Playing answers the first, Finished the second, and NoGame is the menu
    // page, where neither applies. A Resume offered on a finished game would
    // still promise something that cannot happen, which is why setPauseMenu-
    // Visible() asks for Playing exactly rather than for "not NoGame".
    enum class TableState
    {
        NoGame,
        Playing,
        Finished
    };

    TableState tableState = TableState::NoGame;

    // DECLARED LAST, so it is destroyed FIRST: ~GameController joins the game
    // thread, and that has to happen before the widgets its slots write to are
    // gone. ~MainWindow stops it explicitly as well, because the destructor
    // BODY runs before any member is destroyed - deleting `ui` there would
    // otherwise outrun the join.
    GameController controller;
};
#endif // MAIN_WINDOW_H
