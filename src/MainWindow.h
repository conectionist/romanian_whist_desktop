#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "core/GameSnapshot.h"
#include "engine/GameController.h"
#include "engine/Prompts.h"
#include "engine/RoundResult.h"
#include "paint/Theme.h"
#include "table/Motion.h"
#include "ui/LastTrick.h"
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
class QAction;
class QDockWidget;
class QStackedWidget;
QT_END_NAMESPACE

class BidStripItem;
class CardArtWindow;
class GameOverPanel;
class HandItem;
class HistorySheet;
class LastTrickSheet;
class MainMenuPage;
class MenuBar;
class Overlay;
class PauseOverlay;
class RoundSheet;
class RulesSheet;
class SettingsSheet;
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
    // terminal's numbering for anyone switching between the two clients. And
    // section 15.1's, 0-8 bid while the strip is up.
    //
    // ON THE WINDOW rather than on the fan or the strip. A
    // QGraphicsObject has no focus of its own worth building here, and the fan is
    // not the only thing on screen that could want a key - so this is the one
    // place a play key is read, and it forwards to the item's own rule rather
    // than re-deciding what is playable. Arrows and Enter need a selection to
    // move, which is phase E's keyboard map.
    void keyPressEvent(QKeyEvent* event) override;

    // Closing the window is what ends the game. Sets `closing` first, then
    // stops - see the flag below for why the order matters.
    void closeEvent(QCloseEvent* event) override;

    // TAB STAYS INSIDE THE SHEET ON TOP (PR #35's review, finding #4). Every
    // widget's Tab comes up to here unless something below takes it, so this
    // also catches Tab from the menu bar and the log. The bar is still reached
    // by its Alt shortcuts.
    bool focusNextPrevChild(bool next) override;

    // A sheet's showing, for leaveTable(): when it covers a table no other sheet
    // already covers.
    bool eventFilter(QObject* watched, QEvent* event) override;


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

    // A bid on the strip was clicked, or its digit was pressed. Answers the
    // prompt cached by onBetRequested().
    void onBidChosen(unsigned int bet);

    // Section 13's dialog, opened from the main menu, from the menu bar's New
    // Game, or from the game-over panel's Play again. Builds a GameSetup from what
    // it was told and starts a game on it - which for the second and every later
    // game is section 4.3's ordering run again from the top, and which from the
    // menu bar may replace a game still running (prepare() stops and joins it).
    //
    // A RUNNING GAME IS HELD WHILE THE DIALOG IS UP, by the owner's call of
    // 2026-10-21, and runs on if the dialog is cancelled. A finished game's
    // standings come down while it is up, and go back if it is cancelled.
    void showNewGameDialog();

    // THE PAUSE, raised. Delegates to setPauseMenuVisible() below; it exists as
    // its own slot because that is what the menu bar's Exit connects to.
    void showPauseMenu();

    // Abandons the running game and returns to the main menu. The second stop
    // this window produces, and the reason `returningToMenu` exists.
    void abandonToMenu();

    // The menu bar's Rules: the sheet for the game lastConfig deals, over
    // whatever is showing, holding a running game while it is up.
    void showRules();

    // The menu bar's Settings, the same way, opened on lastConfig's pace.
    void showSettings();

    // A control on the Settings sheet moved. Written into lastConfig, which the
    // next New Game opens on, and - live, by the owner's call of 2026-10-21 -
    // into the game in play. The animations' pace goes to the table at once and
    // is held here, apart from lastConfig: it is not a property of a game.
    void applySettings(int botSpeed, bool waitAfterEachRound, motion::Pace animations);

    // Closes the menu sheet that is up, releasing its hold and giving the
    // keyboard back to what is beneath.
    void hideMenuSheets();

    // THE TABLE'S CORNER BUTTONS (the redesign's phase 11), as the menu bar's
    // sheets: over whatever is showing, holding a game in play while up. Round
    // history also opens from the round sheet and the standings, over them.
    void showHistory();
    void showLastTrick();

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

    // Builds section 2.12's menu bar - New Game, Settings, Rules, View and Exit -
    // and installs it with setMenuWidget(). Must run AFTER buildLogDock(), which
    // owns an action this adds to View.
    //
    // NEVER CALL menuBar() AFTER IT. QMainWindow::menuBar() makes a QMenuBar when
    // the menu widget is not one, and installing that deletes this bar.
    void buildMenuBar();

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

    // WHETHER THE PAUSE MENU MAY GO UP NOW: a game playing, and no round sheet
    // holding it. What setPauseMenuVisible() refuses on, and what Exit is greyed
    // by (updatePauseAction()).
    bool canPause() const;
    void updatePauseAction();

    // THE ONLY CALLER OF GameController::setPaused(). Two things can want a game
    // held now - the pause menu and the round sheet - and two callers writing
    // one flag is the defect setPauseMenuVisible() was shaped to prevent. See
    // the body.
    void applyHold();

    void showRoundSheet(const RoundResult& result);
    void hideRoundSheet();

    // Fills the history sheet from roundHistory and the table's names, without
    // opening it: showHistory() opens it, and a round scored under it refills it.
    void fillHistory();

    // THE MENU BAR'S SHEET THAT IS UP, one of menuSheets, or null. They open
    // over everything else and close back to it, as the mock-up's do, so
    // whatever raises itself while one is up puts it back on top
    // (keepMenuSheetOnTop()).
    Overlay* openMenuSheet() const;
    void keepMenuSheetOnTop();

    // Shows one of menuSheets over everything else, in place of any other, and
    // holds a game in play while it is up. Opening one over another, or over
    // itself, never lets the game go in between - see the body.
    void showMenuSheet(Overlay* sheet);

    // Hides every menu sheet and drops their hold, WITHOUT releasing the game:
    // the caller decides that - hideMenuSheets() by applyHold(), a stop by
    // ending the game.
    void dropMenuSheets();

    // GIVES THE KEYBOARD TO WHATEVER IS NOW ON TOP, after something over it went:
    // the sheet on top (activeOverlay()), the table while the bid strip asks, or
    // else the page itself - the table, or the menu page's New Game. Each takes
    // focus when it is raised, and hiding what took it from them does not give
    // it back - which looks finished, because the mouse still works.
    //
    // THE PAGE ONLY IF `hadKeyboard`: if what went had the keyboard. A round
    // sheet that does not wait never takes it, and goes by itself every round;
    // falling back to the table then took the keyboard from wherever the player
    // had put it, the log for one, once a round. Read it with holdsKeyboard()
    // BEFORE the hide, which is when Qt moves the keyboard on.
    //
    // AND THE SHEET BENEATH AT ITS DEFAULT, when what went had the keyboard -
    // not wherever Qt's hide put it, which is the first control in that sheet: the
    // round sheet's Round history since phase 11, so Enter after closing the
    // history opened it again rather than Continuing (found in the running app).
    void restoreFocus(bool hadKeyboard);

    // THE TABLE LETS GO OF THE CURSOR when a sheet comes over it: the scene is
    // told the cursor has left, so nothing stays lit under the dim - a corner
    // button above all, which its own sheet opens over while the cursor rests on
    // it. The scene hears of a cursor only from the view, and the view hears
    // nothing until the mouse next moves (found in the running app, phase 11).
    // eventFilter() sends it once the event under way is done.
    //
    // ONCE, AND ONLY WHILE THE SHEET IS UP (PR #37's review, finding #2). A sheet
    // over another sends nothing: the table had its leave, and another would close
    // the tooltip of a shortened name in the history. Nor does a sheet gone again by
    // then - an unwaited round sheet at top speed - which would put out a card lit
    // under a cursor that never left it.
    void leaveTable();

    // Whether the keyboard is on `widget` or inside it. Null holds nothing.
    bool holdsKeyboard(const QWidget* widget) const;

    // THE SHEET THE KEYBOARD BELONGS TO, top first: Rules or Settings, the
    // pause menu, the game-over panel, or a round sheet that is holding the
    // game. Null when none is up - an unwaited round sheet asks for nothing.
    Overlay* activeOverlay() const;

    // Raises section 16.2's panel over the table it was filled from. The three
    // callers are the two endings - onGameOver() and showFailure() - and a New
    // Game dialog the player then cancelled, which must put back what it took
    // down.
    //
    // FILLS NOTHING. The panel keeps its face between a hide and a show, so this
    // is show, raise and focus - the same three the pause menu does.
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

    // Section 15.1's strip, on the felt since the table redesign's phase 10 and
    // owned by the scene: a handle, as `hand` is, for its second source - the
    // prompt for your bid. Shown only while the engine is parked waiting for one.
    BidStripItem* bidStrip = nullptr;

    // SECTION 12'S FOURTH REGION, "where the game talks to you", which is the
    // trick panel in the table's top-left corner (top-right from the redesign's phase
    // 8 until the owner's call of 2026-10-24).
    // Owned by the scene; a handle, as `hand` is, for the same second source.
    TrickPanelItem* trickPanel = nullptr;

    // The pause menu, a child of the stack rather than a page in it or a dialog
    // of its own - it covers whichever page is showing and dims it. Its
    // visibility and GameController's pause are one fact; see
    // setPauseMenuVisible().
    PauseOverlay* pauseOverlay = nullptr;

    // Section 2.12's bar, installed as the window's menu widget, and its items.
    // Exit raises the pause menu, whose Main menu is the way out; it replaced
    // phase 8's temporary Menu item.
    MenuBar* bar = nullptr;
    QAction* newGameAction = nullptr;
    QAction* settingsAction = nullptr;
    QAction* rulesAction = nullptr;
    QAction* exitAction = nullptr;

    // Section 16.2's panel, the same shape as the pause menu and for the same
    // reason - it is drawn OVER the final state rather than replacing it, which
    // is what section 4.1's "publish the last snapshot before emitting gameOver"
    // exists to make possible.
    GameOverPanel* gameOverPanel = nullptr;

    // Section 16.1's sheet, raised over the round it reports.
    RoundSheet* roundSheet = nullptr;

    // The menu bar's Rules and Settings, the same arrangement again.
    RulesSheet* rulesSheet = nullptr;
    SettingsSheet* settingsSheet = nullptr;

    // The table's Round history and Last trick (phase 11), the same arrangement
    // again: opened from its corner buttons, and the history from the round sheet
    // and the standings too.
    HistorySheet* historySheet = nullptr;
    LastTrickSheet* lastTrickSheet = nullptr;

    // EVERY SHEET THE MENU BAR AND THE CORNER BUTTONS OPEN, in one list, so that
    // one more is added here and nowhere else: showMenuSheet(), dropMenuSheets()
    // and openMenuSheet() all go through it.
    std::vector<Overlay*> menuSheets;

    // AND EVERY SHEET AT ALL, those and the pause menu, the round sheet and the
    // standings: the seven whose showing covers the table (eventFilter()).
    std::vector<Overlay*> sheets;

    // THE GAME'S ROUNDS SO FAR, one RoundResult each, as onRoundScored() delivers
    // them: the only record of what a round was worth, since the engine keeps
    // none (RoundResult.h). Here rather than on the bridge, which would be writing
    // it on the game thread for this one to read. Emptied by startGame().
    std::vector<RoundResult> roundHistory;

    // THE LAST TRICK FINISHED IN THE GAME, in whichever round, read off the frames
    // render() draws. Emptied by startGame(), and by nothing else: a new game's
    // round 1 has the old one's number.
    lasttrick::Tracker lastTrick;

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
    //
    // AND THE PACE SETTINGS HAS SET SINCE: its bot speed and round wait are
    // written here as they change, so the next game opens on them.
    newgame::Config lastConfig = newgame::defaults();

    // HOW MUCH THE TABLE MOVES: Settings' "Card animations", in memory like the
    // rest (QSettings is section 20). The mock-up's own lengths until a player
    // changes them.
    motion::Pace animationPace = motion::defaultPace;

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

    // THE REASONS A GAME CAN BE HELD, kept apart so that releasing one does not
    // release the other. applyHold() is what turns them into the single flag
    // GameBridge owns.
    bool pauseMenuHolding = false;
    bool roundSheetHolding = false;

    // TWO MORE, since the redesign's phase 9: the setup dialog, opened from the
    // menu bar over a game still running, and the bar's Rules and Settings
    // sheets (the owner's call, 2026-10-21: the bots wait while you read).
    bool dialogHolding = false;
    bool sheetHolding = false;

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
