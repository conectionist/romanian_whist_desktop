#include <catch2/catch_test_macros.hpp>

#include "MainWindow.h"
#include "WindowDriving.h"
#include "table/HandItem.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"
#include "table/TableView.h"
#include "ui/BidStrip.h"
#include "ui/GameOverPanel.h"
#include "ui/MenuBar.h"
#include "ui/NewGameDialog.h"
#include "ui/PauseOverlay.h"
#include "ui/RoundSheet.h"
#include "ui/RulesSheet.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QString>
#include <QTimer>

// D1's flow, driven through the real window: menu, deal, pause, resume, abandon,
// and a second game. Constructible because TestApp is a QApplication on the
// offscreen platform - real widgets, a real scene, a real game thread, and no
// display.
//
// THIS IS THE ONE TEST THAT EXERCISES THE REUSE END TO END. Everything under it
// is covered piecewise - GameControllerTests has the latches, GameBridgeTests
// the pause, NewGameTests the setup - and none of that would notice a window
// that closed itself on the way back to the menu, or one that started game two
// without clearing game one.
//
// AND IT REPLACES THE by-eye PASS FOR THE PARTS THAT HAVE A RIGHT ANSWER, which
// is C3's finding: driving the running client with xdotool sent keystrokes into
// whatever had focus once the app exited, and there is no way to aim them. A
// person still has to look at the table; a person should not have to be the
// thing that notices the log stopped moving.
namespace
{
// The event-loop helpers and the bid lookup live in WindowDriving.h, shared with
// tests/ScreenshotTool.cpp.
using driving::bidButton;
using driving::pump;
using driving::pumpUntil;

// Answers the setup dialog FROM INSIDE ITS NESTED EVENT LOOP. QDialog::exec()
// does not return until something answers it, so a test that clicked New Game
// and then looked would never reach the looking - the only way in is a timer
// that fires within that loop.
//
// THE PAUSE MENU NEEDS NONE OF THIS, which is what changed when it stopped
// being a modal dialog: it is a child widget, so its buttons are clicked
// directly and the assertion after the click just runs.
//
// Flips the dialog to "I'm watching" on the way past.
//
// A SPECTATOR GAME, AND THAT IS NOT INCIDENTAL: a seated game CANNOT show the
// pause working. At round 1 the game thread is parked in makeBet() waiting for a
// bid, so nothing advances whether it is paused or not - measured, and it cost
// this file its first run. The pause holds pace(), and only a game with bots to
// pace is ever inside one.
void answerModalWatching(int afterMs)
{
    QTimer::singleShot(afterMs, []
    {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)
            return;

        if(QRadioButton* watch = dialog->findChild<QRadioButton*>("watching"))
            watch->setChecked(true);

        dialog->done(QDialog::Accepted);
    });
}

// THE SHORTEST GAME THIS CLIENT CAN BE ASKED FOR, and the only reason to want
// one: a test about the END of a game has to get there. Watched, so the pause
// and the ending both work without a seat to answer prompts from; two bots,
// which is the smallest table and an 18-round schedule; and the slider at
// FastestSpeed, where pacingFor() scales every beat by zero.
//
// Every control is reached by NAME, which is D1's own finding - a positional
// findChildren<QLineEdit*>() typed a bot name into the seed box and asserted
// nothing, because every QSpinBox owns an editable QLineEdit of its own.
void answerModalShortWatchedGame(int afterMs)
{
    QTimer::singleShot(afterMs, []
    {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)
            return;

        if(QRadioButton* watch = dialog->findChild<QRadioButton*>("watching"))
            watch->setChecked(true);

        // AFTER the radio, which is what moves the spinner's range: a watched
        // game seats two bots at minimum, and setting the value first would set
        // it against the playing bounds.
        if(QSpinBox* opponents = dialog->findChild<QSpinBox*>("opponents"))
            opponents->setValue(2);

        if(QSlider* speed = dialog->findChild<QSlider*>("speed"))
            speed->setValue(newgame::FastestSpeed);

        dialog->done(QDialog::Accepted);
    });
}

// A PLAYED game, dealt as fast as the client can be asked to deal one. The
// dialog's defaults are already a seated game, so this leaves the radio alone
// and moves only the slider - where pacingFor() scales every beat by zero.
//
// THE SPEED IS NOT A CONVENIENCE HERE. With a human seated GameBridge::delayOf() picks 700ms
// rather than the spectator's 250 (section 4.1's split), so three bots bidding
// is some two seconds of a test waiting for nothing before the human is asked
// for a card.
void answerModalFastSeatedGame(int afterMs)
{
    QTimer::singleShot(afterMs, []
    {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)
            return;

        if(QSlider* speed = dialog->findChild<QSlider*>("speed"))
            speed->setValue(newgame::FastestSpeed);

        dialog->done(QDialog::Accepted);
    });
}

// A game that stops at every round sheet, dealt as fast as it can be. Section
// 16.1's dismissal setting, which is off by default - so this is the only
// helper that turns it on.
//
// WATCHED, AND THAT IS NOT A SHORTCUT AROUND THE SEAT. A seated game reaches no
// round sheet without somebody answering both prompts - round 1 is one card, so
// the human bids AND plays before anything is scored - and a case about the
// HOLD would then be half a case about answering prompts. The setting is not
// seated-only; a player watching bots may want to read each round just as much.
void answerModalWaitingWatchedGame(int afterMs)
{
    QTimer::singleShot(afterMs, []
    {
        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if(!dialog)
            return;

        if(QRadioButton* watch = dialog->findChild<QRadioButton*>("watching"))
            watch->setChecked(true);

        if(QSpinBox* opponents = dialog->findChild<QSpinBox*>("opponents"))
            opponents->setValue(2);

        if(QSlider* speed = dialog->findChild<QSlider*>("speed"))
            speed->setValue(newgame::FastestSpeed);

        if(QCheckBox* wait = dialog->findChild<QCheckBox*>("waitEachRound"))
            wait->setChecked(true);

        dialog->done(QDialog::Accepted);
    });
}

}

TEST_CASE("The client opens on the menu with no game running", "[main-window]")
{
    // Phases A to C dealt a hardcoded game from the constructor. Section 8's
    // phase D is "the app never needs a command line", and this is the half of
    // that which is not the dialog: at launch there is no game, no thread and
    // nothing on the table.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    REQUIRE(pages != nullptr);
    REQUIRE(pages->currentIndex() == 0);

    REQUIRE(window.findChild<QPushButton*>("newGame") != nullptr);
    REQUIRE(window.findChild<QPushButton*>("quit") != nullptr);

    // NOTHING TO PAUSE, and Exit says so rather than doing nothing when
    // clicked (PR 34's review, finding #4).
    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);
    REQUIRE_FALSE(exitItem->isEnabled());
}

TEST_CASE("Esc on the menu page does nothing", "[main-window]")
{
    // The pause is bound on the window, so it arrives whichever page is showing
    // - and the menu page is exactly where there is nothing to pause. Without
    // the guard this opens a modal dialog over a window with no game behind it,
    // whose Resume resumes nothing and whose Main menu stops a controller that
    // was never prepared.
    //
    // A HANG IF IT IS WRONG, not a failure: the dialog would exec() and nothing
    // in this test would answer it. That is the honest shape here, and the
    // reason nothing is armed to rescue it - an answerModal() standing by would
    // make the case pass either way.
    MainWindow window;
    window.show();
    pump(100);

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &escape);

    pump(100);
    SUCCEED("Esc with no game did not open a dialog");
}

TEST_CASE("A game paused from Exit stops, and resumes", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(log != nullptr);
    REQUIRE(newGame != nullptr);

    answerModalWatching(300);
    newGame->click();
    pump(3000);

    REQUIRE(log->blockCount() > 1);

    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);
    REQUIRE(exitItem->isEnabled());

    // MEASURED ACROSS THE MIDDLE OF THE PAUSE, not from the click. The pause
    // lands at the next pace(), so a frame or two still arrives after the button
    // - what it claims is that the game is stopped THEREAFTER. The gap sampled
    // is 1.8s of a 250ms spectator beat, which is seven frames that must not
    // appear.
    exitItem->trigger();

    PauseOverlay* overlay = window.findChild<PauseOverlay*>();
    REQUIRE(overlay != nullptr);
    REQUIRE(overlay->isVisible());

    pump(600);
    const int settled = log->blockCount();

    pump(1800);
    const int held = log->blockCount();

    INFO("settled " << settled << ", held " << held);
    REQUIRE(settled > 0);
    REQUIRE(held == settled);

    QPushButton* resume = window.findChild<QPushButton*>("resume");
    REQUIRE(resume != nullptr);
    resume->click();

    REQUIRE_FALSE(overlay->isVisible());
    const int afterResume = log->blockCount();

    // And it is a pause rather than a stop: the game goes on afterwards.
    pump(2000);
    REQUIRE(log->blockCount() > afterResume);
}

TEST_CASE("The window can be closed while the game is paused", "[main-window]")
{
    // THE DEFECT THAT REPLACED THE MODAL DIALOG. Qt drops window-system events
    // to a window blocked by a modal, so the title bar's X did nothing while the
    // pause menu was up - not a hang, and recoverable by resuming first, but a
    // dead close button all the same.
    //
    // WHAT THIS CAN AND CANNOT SEE. A test cannot press the X: that is the
    // window manager's route, and the very thing Qt was filtering. What it can
    // pin is the half that made the filtering possible - that nothing here is
    // application-modal while the pause is up - and then that the close itself
    // completes, which is section 15.3's hazard from the window's end: an
    // indefinite pace() that cancelPacing() does not reach hangs in
    // thread.wait() forever.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    answerModalWatching(300);
    window.findChild<QPushButton*>("newGame")->click();
    pump(2000);

    window.findChild<QAction*>("exitAction")->trigger();
    pump(200);

    REQUIRE(window.findChild<PauseOverlay*>()->isVisible());

    // NOTHING IS MODAL. This is the assertion the dialog would fail, and it is
    // what makes the window manager's close reach closeEvent() at all.
    REQUIRE(QApplication::activeModalWidget() == nullptr);

    // And the close completes rather than hanging on the join. If the paused
    // wait ever stops testing pacingCancelled, this does not fail - it never
    // returns, which is the honest shape and is why the case says so.
    REQUIRE(window.close());
    REQUIRE_FALSE(window.isVisible());
}

TEST_CASE("The window can be closed with the hand waiting on a card", "[main-window]")
{
    // C5'S OWN SENTENCE, at the layer it was written about: "close the window
    // with the panel open". Its sibling above closes a WATCHED game, where
    // nothing is ever parked in a provider - the game thread is asleep in
    // pace(), which cancelPacing() returns from. This one is the other half of
    // section 6, and the only path in the client that should produce
    // GameAbandoned: a seated game parked in playCard() with the fan lit, woken
    // by abandon(), which THROWS.
    //
    // AND IT IS THE ONE PATH NOTHING AUTOMATED HAD EVER TAKEN. C2 measured the
    // bid-strip close by hand at 39 ms and D1 added this file, but every close
    // in it until now was a watched game.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    window.activateWindow();
    pump(200);

    answerModalFastSeatedGame(300);
    window.findChild<QPushButton*>("newGame")->click();

    BidStrip* bidStrip = window.findChild<BidStrip*>();
    REQUIRE(bidStrip != nullptr);
    REQUIRE(pumpUntil(10000, [bidStrip]{ return bidStrip->isVisible(); }));

    // A BID OF 0, which is always legal for the opener - getForbiddenBet() is
    // engaged for the LAST bidder only, and the human opens at seat 0. Answering
    // is what gets the game past bidding and into the play the rest of this case
    // is about.
    //
    // FOUND BY THE BID IT OFFERS, not by position: findChildren() documents no
    // ordering, and a case that clicks whichever button comes back first would
    // go on passing while quietly exercising a different bid.
    QPushButton* zero = bidButton(*bidStrip, 0u);
    REQUIRE(zero != nullptr);
    zero->click();

    // A PLAYABLE FAN IS THE OBSERVABLE FORM OF "parked in playCard()". Nothing
    // else makes the hand clickable - a snapshot's hand carries no legality and
    // is drawn disabled (section 4.4) - so this is true exactly while the engine
    // is waiting on a card.
    TableScene* scene = window.findChild<TableScene*>();
    REQUIRE(scene != nullptr);

    REQUIRE(pumpUntil(10000, [scene]{ return scene->handItem()->isPlayable(0); }));

    // THE CLOSE. closeEvent() sets `closing`, calls stop(), and stop() calls
    // abandon() on a provider that is parked right now.
    //
    // A FAILURE HERE IS A HANG, NOT A MESSAGE, and that is chosen rather than
    // overlooked. If abandon() stops reaching the parked provider, close()
    // blocks in thread.wait() on this very thread, and no rescue is reachable
    // from outside: MainWindow's controller is private, and the event loop that
    // a timer would need is the thing that is blocked. The obvious way in - a
    // test-only accessor on MainWindow - is production API that only tests call,
    // which C2 deliberately moved OUT of src/ when chooseBet() became
    // fixtures::lowestLegalBid().
    //
    // GameControllerTests' "A stop while the game is parked on a CARD ends it"
    // is what turns that same failure into a printed message, with an off-thread
    // stop and a rescue. This case is the layer above it, and inherits its
    // diagnosis rather than repeating its machinery.
    REQUIRE(window.close());
    REQUIRE_FALSE(window.isVisible());
}

TEST_CASE("Abandoning to the main menu does not close the window", "[main-window]")
{
    // THE FLAG D1 HAD TO ADD. onGameStopped() closes the window for any stop it
    // cannot account for, which through phase C was every stop but the window's
    // own - there was no menu and no second game to produce another. Abandoning
    // is the second, and without `returningToMenu` beside `closing` it closes
    // the application instead of showing the menu.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(pages != nullptr);
    REQUIRE(newGame != nullptr);

    answerModalWatching(300);
    newGame->click();
    pump(2000);
    REQUIRE(pages->currentIndex() == 1);

    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);

    exitItem->trigger();
    pump(100);

    QPushButton* mainMenu = window.findChild<QPushButton*>("mainMenu");
    REQUIRE(mainMenu != nullptr);
    mainMenu->click();
    pump(500);

    REQUIRE(pages->currentIndex() == 0);
    REQUIRE(window.isVisible());
    REQUIRE_FALSE(window.findChild<PauseOverlay*>()->isVisible());
    REQUIRE_FALSE(exitItem->isEnabled());
}

TEST_CASE("A second game is dealt, and is neither stopped nor sprinting", "[main-window]")
{
    // SECTION 4.3's ORDERING RUN AGAIN FROM THE TOP, through the window that
    // does it. The two latches have their own controller tests; what those
    // cannot see is a window that reuses the controller correctly and then draws
    // game one over it, or one that never reaches start() at all.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(log != nullptr);
    REQUIRE(pages != nullptr);
    REQUIRE(newGame != nullptr);

    answerModalWatching(300);
    newGame->click();
    pump(2000);

    const int gameOne = log->blockCount();
    REQUIRE(gameOne > 1);

    window.findChild<QAction*>("exitAction")->trigger();
    pump(100);
    window.findChild<QPushButton*>("mainMenu")->click();
    pump(500);

    // ---- game two ----
    answerModalWatching(300);
    newGame->click();
    pump(800);

    REQUIRE(pages->currentIndex() == 1);

    // THE LOG IS CLEARED BY startGame(), so this is game two's own count from
    // zero - which is also what proves game one's lines went with it.
    const int early = log->blockCount();
    REQUIRE(early < gameOne + 5);

    pump(3000);
    const int later = log->blockCount();
    INFO("game two: " << early << " then " << later);

    // Not stopped after one round, which is GameController::stopRequested left
    // set; and not sprinting to the end, which is QtMoveProvider::abandoned
    // latched and reused. Both are named in the build order's symptom table.
    REQUIRE(later > early);
    REQUIRE(later < 400);
}

TEST_CASE("A watched game arrives at the scene as one", "[main-window]")
{
    // SPECTATOR MODE IS RE-ENABLED BY D1 AND NOTHING HAS RUN IT SINCE B2 - C2's
    // overlay band, C3's hand strip and C4's plaque cards all landed in between.
    //
    // NOT A SECOND COPY OF TableSceneTests' "A spectated table draws no hand",
    // which asserts the same fact against a hand-built snapshot. What that
    // one cannot see is the CHAIN: the dialog's radio to humanSeatOf() to
    // prepare(std::nullopt) to GameBridge stamping -1 onto every snapshot. Any
    // link seating somebody anyway leaves the scene correct and the game wrong -
    // and section 8 warns that a stray humanSeat makes a bot play with no beat
    // and asks canSeeHand() about a viewer who is not there.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(newGame != nullptr);

    answerModalWatching(300);
    newGame->click();
    pump(2000);

    TableScene* scene = window.findChild<TableScene*>();
    REQUIRE(scene != nullptr);

    // Nobody seated on the frame the scene drew, and so no hand drawn - section
    // 14's "the bottom hand area is not drawn at all", from the dialog's radio
    // all the way down.
    REQUIRE(scene->lastSnapshot() != nullptr);
    REQUIRE(scene->lastSnapshot()->humanSeat == -1);
    REQUIRE_FALSE(scene->handItem()->isVisible());
}

TEST_CASE("An abandoned game does not repaint the menu behind it", "[main-window]")
{
    // THE SECOND MOMENT AN OLD GAME'S QUEUE IS WORTHLESS, and until D1's review
    // only prepare() knew about the first. stop() joins the game thread, so
    // every frame published between the click and the join is already in this
    // thread's queue - and it is delivered AFTER showMainMenu(), drawing a dead
    // game into the log and onto the table behind the menu page. (Into the status
    // bar too, until the table redesign's phase 8 took the status bar away.)
    //
    // A WATCHED GAME, which is what puts frames in the queue at all: a seated one
    // is parked in the provider waiting for a bid and has published everything it
    // is going to.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(pages != nullptr);
    REQUIRE(newGame != nullptr);

    answerModalWatching(300);
    newGame->click();
    pump(2000);
    REQUIRE(pages->currentIndex() == 1);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    TableScene* scene = window.findChild<TableScene*>();
    REQUIRE(log != nullptr);
    REQUIRE(scene != nullptr);

    // Mid-game, with the bots pacing - so there is a backlog to leak.
    REQUIRE(scene->lastSnapshot() != nullptr);

    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);

    // BOTH CLICKS IN ONE TURN OF THE EVENT LOOP, and that is the whole setup:
    // pausing parks the game thread at the next pace(), and a pump between the
    // two clicks would deliver the backlog before the abandon and leave nothing
    // to leak. A player clicking Main menu straight after Menu is the ordinary
    // case, not a contrived one - the queue is only ever drained by the window
    // going idle.
    exitItem->trigger();
    window.findChild<PauseOverlay*>()->findChild<QPushButton*>("mainMenu")->click();

    // THE PUMP IS THE MEASUREMENT. Everything the abandon does is synchronous,
    // so an assertion that ran straight away would pass with the whole backlog
    // still queued behind it; what has to be given the chance to happen is the
    // delivery.
    const int atAbandon = log->blockCount();
    const std::shared_ptr<const GameSnapshot> drawn = scene->lastSnapshot();

    pump(500);

    REQUIRE(pages->currentIndex() == 0);

    // Nothing was drawn from a game that no longer exists: not a line in the
    // log, and not a frame on the table waiting behind the menu page.
    REQUIRE(log->blockCount() == atAbandon);
    REQUIRE(scene->lastSnapshot() == drawn);
}

TEST_CASE("Resuming gives the keyboard back to the bid strip", "[main-window]")
{
    // THE PAUSE TAKES FOCUS AND NOTHING GIVES IT BACK. focusResume() puts the
    // keyboard on the Resume button so that Enter resumes; hiding that button
    // does not return it, so a pause taken while the strip was up left its digit
    // keys dead afterwards - with the buttons still working, which is exactly
    // the shape that looks finished.
    //
    // A SEATED GAME, and the only case in this file that needs one: the strip is
    // shown while the engine is parked in the provider waiting for a bid, which
    // is a state a watched game never has. The pause CANNOT be measured here -
    // the thread is in the provider, not in pace() - and that is fine, because
    // what is being measured is the focus.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    window.activateWindow();
    pump(200);

    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(newGame != nullptr);

    // The dialog's defaults are a played game, so this accepts them unchanged.
    QTimer::singleShot(300, []
    {
        if(QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            dialog->done(QDialog::Accepted);
    });

    newGame->click();

    BidStrip* bidStrip = window.findChild<BidStrip*>();
    REQUIRE(bidStrip != nullptr);

    // Round 1 is one card and the human bids first at seat 0, so the strip is up
    // as soon as the deal is.
    REQUIRE(pumpUntil(10000, [bidStrip]{ return bidStrip->isVisible(); }));

    // focusWidget(), NOT hasFocus(). hasFocus() is false for every widget in an
    // inactive window, and the offscreen platform never activates one - so the
    // stronger-looking assertion is the one that cannot be made here. This is
    // the question that is actually being asked anyway: which widget the window
    // would deliver a key press to.
    REQUIRE(window.focusWidget() == bidStrip);

    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);

    exitItem->trigger();
    pump(100);
    REQUIRE(window.focusWidget() != bidStrip);

    window.findChild<PauseOverlay*>()->findChild<QPushButton*>("resume")->click();
    pump(100);

    // The strip is still the thing being waited on, so it is still the thing the
    // digits belong to.
    REQUIRE(bidStrip->isVisible());
    REQUIRE(window.focusWidget() == bidStrip);
}

TEST_CASE("The log dock survives a trip to the menu", "[main-window]")
{
    // QDockWidget::hide() UNCHECKS ITS OWN toggleViewAction(), and that two-way
    // sync is the whole reason buildLogDock() uses Qt's action rather than one of
    // ours. showMainMenu() hid the dock on the way to the menu and its comment
    // claimed the dock came back with the next game; what actually happened is
    // that View > Log turned itself off and nothing ever turned it back on.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QDockWidget* logDock = window.findChild<QDockWidget*>("logDock");
    REQUIRE(logDock != nullptr);

    QAction* logAction = logDock->toggleViewAction();
    REQUIRE_FALSE(logAction->isChecked());

    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    REQUIRE(newGame != nullptr);

    answerModalWatching(300);
    newGame->click();
    pump(2000);

    // The player opens it, the way the View menu does.
    logAction->trigger();
    REQUIRE(logDock->isVisible());

    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);
    exitItem->trigger();
    pump(100);
    window.findChild<PauseOverlay*>()->findChild<QPushButton*>("mainMenu")->click();
    pump(500);

    // Still open, and still checked - the two are one fact, which is why the
    // action is Qt's own.
    REQUIRE(logAction->isChecked());
    REQUIRE(logDock->isVisible());
}

TEST_CASE("A finished game has a way back to the menu", "[main-window]")
{
    // THE DEAD END D1's REVIEW FOUND, and the one thing under this whole panel
    // that a unit test of the panel cannot see. onGameOver() cleared the flag
    // setPauseMenuVisible() is gated on, so on a finished game the Menu button
    // and Esc were both dead and the menu bar held nothing but View: the only
    // way out of a completed game was closing the window, which also made "play
    // again" - the thing D1 exists to build - unreachable from the ending it
    // exists for.
    //
    // A REAL GAME PLAYED TO ITS END, not a signal delivered by hand. What is
    // being tested is the chain from gameOver through the window to a button
    // that works, and every link that could drop it - the state the pause reads,
    // the panel's geometry, the connection to abandonToMenu() - is in between.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    QPushButton* newGame = window.findChild<QPushButton*>("newGame");
    GameOverPanel* panel = window.findChild<GameOverPanel*>();
    REQUIRE(pages != nullptr);
    REQUIRE(newGame != nullptr);
    REQUIRE(panel != nullptr);

    REQUIRE_FALSE(panel->isVisible());

    answerModalShortWatchedGame(300);
    newGame->click();

    // 18 rounds of two bots with every beat scaled to zero. The deadline is
    // generous against a loaded machine and is not the measurement - a game that
    // has not ended in thirty seconds has stopped, which is a failure and not a
    // slow pass.
    REQUIRE(pumpUntil(30000, [panel]{ return panel->isVisible(); }));

    // Over the table, not instead of it: section 16.2 draws the standings on the
    // final state, which section 4.1 publishes before it emits gameOver.
    REQUIRE(pages->currentIndex() == 1);
    REQUIRE(panel->geometry() == pages->rect());

    QLabel* announcement = panel->findChild<QLabel*>("announcement");
    REQUIRE(announcement != nullptr);
    REQUIRE_FALSE(announcement->text().isEmpty());

    // AND THE PAUSE STAYS SHUT UNDER IT. There is nothing left to hold, and a
    // Resume raised over a finished game would promise a game that has ended -
    // which is what the state this panel reads is for.
    window.findChild<PauseOverlay*>()->hide();
    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);
    REQUIRE_FALSE(exitItem->isEnabled());
    exitItem->trigger();
    pump(100);
    REQUIRE_FALSE(window.findChild<PauseOverlay*>()->isVisible());

    QPushButton* mainMenu = panel->findChild<QPushButton*>("gameOverMainMenu");
    REQUIRE(mainMenu != nullptr);

    mainMenu->click();
    pump(500);

    // The window is still up - the stop this button causes is the second one
    // onGameStopped() has to account for, and it accounts for it whether the
    // game was abandoned or had already ended.
    REQUIRE(pages->currentIndex() == 0);
    REQUIRE(window.isVisible());
    REQUIRE_FALSE(panel->isVisible());
}

TEST_CASE("A waited round sheet holds the game until Continue", "[main-window]")
{
    // SECTION 16.1's SHEET, and the hold that makes its Continue button mean
    // something. Measured the way D1's pause case is - the log stops appending
    // and starts again - because that is the only thing on screen that says
    // whether the game thread is moving.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    RoundSheet* sheet = window.findChild<RoundSheet*>();
    REQUIRE(log != nullptr);
    REQUIRE(sheet != nullptr);

    answerModalWaitingWatchedGame(300);
    window.findChild<QPushButton*>("newGame")->click();

    REQUIRE(pumpUntil(20000, [sheet]{ return sheet->isVisible(); }));

    // HELD. At FastestSpeed every beat is zero, so a game that was not held
    // would run to its end inside this window rather than sitting still.
    const int atSheet = log->blockCount();
    pump(1500);

    INFO("log at sheet " << atSheet << ", after waiting " << log->blockCount());
    REQUIRE(log->blockCount() == atSheet);
    REQUIRE(sheet->isVisible());

    // The sheet has the way on, so Exit is greyed while it holds.
    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem != nullptr);
    REQUIRE_FALSE(exitItem->isEnabled());

    QPushButton* continueButton = window.findChild<QPushButton*>("continueRound");
    REQUIRE(continueButton != nullptr);
    continueButton->click();

    REQUIRE_FALSE(sheet->isVisible());
    REQUIRE(exitItem->isEnabled());
    REQUIRE(pumpUntil(10000, [&]{ return log->blockCount() > atSheet; }));
}

TEST_CASE("An unwaited game runs to the end with nobody watching", "[main-window]")
{
    // THE DEFAULT, and the half that matters most: the sheet must not hold a
    // game whose player did not ask it to. A watched game with the box unticked
    // has to reach its own ending untouched, or every spectated game becomes a
    // sequence of clicks.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    GameOverPanel* panel = window.findChild<GameOverPanel*>();
    REQUIRE(panel != nullptr);

    answerModalShortWatchedGame(300);
    window.findChild<QPushButton*>("newGame")->click();

    // The whole 18-round schedule, unattended.
    REQUIRE(pumpUntil(60000, [panel]{ return panel->isVisible(); }));

    // AND THE SHEET IS NOT LEFT STANDING UNDER IT. The final round of an
    // unwaited game is skipped outright - its numbers are the ones the panel is
    // about to show in full - so there is nothing there to hide.
    REQUIRE_FALSE(window.findChild<RoundSheet*>()->isVisible());
}

TEST_CASE("The pause menu and the round sheet do not release each other",
          "[main-window]")
{
    // THE HAZARD D2 ADDED, and the reason applyHold() exists. GameController
    // holds ONE flag; the pause menu and the round sheet are two reasons to set
    // it, and a Resume that wrote that flag directly would release a sheet which
    // was holding the game as well as its own menu.
    //
    // The guard is that a HOLDING sheet refuses the pause menu outright - so
    // what this pins is the refusal, and that the game stays held through it.
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    RoundSheet* sheet = window.findChild<RoundSheet*>();
    PauseOverlay* overlay = window.findChild<PauseOverlay*>();
    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    REQUIRE(sheet != nullptr);
    REQUIRE(overlay != nullptr);
    REQUIRE(log != nullptr);

    answerModalWaitingWatchedGame(300);
    window.findChild<QPushButton*>("newGame")->click();

    REQUIRE(pumpUntil(20000, [sheet]{ return sheet->isVisible(); }));

    // Esc, which reaches setPauseMenuVisible() whichever page is showing.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &escape);
    pump(100);

    REQUIRE_FALSE(overlay->isVisible());
    REQUIRE(sheet->isVisible());

    // AND THE MENU ITEM, the other road, is greyed: the same refusal, shown before
    // anyone asks (PR 34's review, #4). It is not triggered here. Qt drops the
    // trigger of a disabled action before it reaches the window, so the refusal
    // would pass whatever setPauseMenuVisible() did; Esc above is what reaches it.
    REQUIRE_FALSE(window.findChild<QAction*>("exitAction")->isEnabled());

    // THE GAME IS STILL HELD after both attempts, which is what the hazard is
    // actually about: a menu that had opened and closed would have cleared the
    // one flag on its way out.
    const int held = log->blockCount();
    pump(800);
    REQUIRE(log->blockCount() == held);
}

// A READABLE TABLE AT ANY WINDOW SIZE. The table is a fixed space the view scales
// to fit, and nothing stopped that scale reaching a size nobody can read: a small
// window with the scoreboard beside it put the plaque names at six pixels. The
// view's minimum size is the floor, and the layouts carry it up to the window -
// so the window refuses the size rather than drawing the table smaller.
//
// THE TABLE PAGE, shown without a game: a stacked page that is not showing is not
// laid out, and starting a game for this would be three seconds spent on a
// question about geometry.
TEST_CASE("The window cannot shrink the table below a readable scale", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(100);

    TableView* table = window.findChild<TableView*>();
    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    REQUIRE(table != nullptr);
    REQUIRE(pages != nullptr);

    QWidget* page = table;
    while(page && page->parentWidget() != pages)
        page = page->parentWidget();

    REQUIRE(page != nullptr);
    pages->setCurrentWidget(page);

    window.resize(300, 200);
    pump(100);

    const qreal scale = tablespace::fitScale(table->viewport()->size());

    INFO("table " << table->viewport()->width() << "x" << table->viewport()->height()
                  << ", window " << window.width() << "x" << window.height());
    REQUIRE(scale >= tablespace::minimumScale - 1e-9);
}

// THE TABLE IS THE PAGE (the table redesign's phase 8). The info strip across its
// top, the scoreboard beside it and the prompt bar beneath it are the panels in
// its corners now, drawn by the scene; and the status bar, which said what the
// scoreboard and the game-over panel say, is gone, so the window is the menu bar
// and the table, as the mock-up is.
TEST_CASE("The table page is the table alone", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(100);

    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    TableView* table = window.findChild<TableView*>();

    REQUIRE(pages != nullptr);
    REQUIRE(table != nullptr);
    REQUIRE(table->parentWidget() == pages);

    REQUIRE(window.findChild<QStatusBar*>() == nullptr);

    // AND THE BAR ABOVE IT IS SECTION 2.12's, not Qt's (the redesign's phase 9):
    // the window's menu widget, with no QMenuBar left anywhere - menuBar() would
    // have made one, and installing it deletes ours.
    MenuBar* bar = window.findChild<MenuBar*>();
    REQUIRE(bar != nullptr);
    REQUIRE(window.menuWidget() == bar);
    REQUIRE(window.findChild<QMenuBar*>() == nullptr);

    // Its Exit is the pause menu's way in for the mouse.
    REQUIRE(window.findChild<QAction*>("exitAction") != nullptr);
}

// THE MENU BAR'S NEW GAME, over a game still running (the redesign's phase 9).
// Before it, the setup dialog opened only over the menu page or a game that had
// ended, so this is the first path on which prepare() stops a game in full flight
// from the window - and its gameStopped must not reach onGameStopped() and close
// the window, which is what prepare()'s purge is there to prevent.
TEST_CASE("New Game from the menu bar replaces a running game", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    QAction* newGameItem = window.findChild<QAction*>("newGameAction");
    REQUIRE(log != nullptr);
    REQUIRE(pages != nullptr);
    REQUIRE(newGameItem != nullptr);

    // Live on the menu page as well, where it is the page's own button again.
    REQUIRE(newGameItem->isEnabled());

    answerModalWatching(300);
    newGameItem->trigger();
    pump(2000);

    REQUIRE(pages->currentIndex() == 1);
    const int gameOne = log->blockCount();
    REQUIRE(gameOne > 1);

    // ---- game two, dealt over game one ----
    //
    // trigger() returns once the dialog is answered and startGame() has run, and
    // before any frame of game two has been delivered: so the log is empty here
    // only if game one was replaced rather than left running beside it.
    answerModalWatching(300);
    newGameItem->trigger();

    REQUIRE(log->blockCount() == 1);
    REQUIRE(log->toPlainText().isEmpty());

    pump(2000);

    REQUIRE(window.isVisible());
    REQUIRE(pages->currentIndex() == 1);
    REQUIRE(log->blockCount() > 1);
    REQUIRE(window.findChild<QAction*>("exitAction")->isEnabled());
}

// The owner's call of 2026-10-21: the bots wait while the setup dialog is open
// over a running game, and play on if it is cancelled. Measured as the pause case
// measures its hold - the log stops appending, then starts again.
TEST_CASE("A cancelled New Game holds the game while open and leaves it running", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    QAction* newGameItem = window.findChild<QAction*>("newGameAction");
    REQUIRE(log != nullptr);
    REQUIRE(newGameItem != nullptr);

    answerModalWatching(300);
    newGameItem->trigger();
    pump(2000);

    const int before = log->blockCount();
    REQUIRE(before > 1);

    // INSIDE THE DIALOG'S OWN EVENT LOOP, which is the only place a test can look
    // while it is open. Sampled across the middle of the hold, as the pause case
    // does: the hold lands at the next pace(), so a frame may still arrive just
    // after the dialog opens. 1.8 s of a 250 ms spectator beat is seven frames
    // that must not appear.
    int settled = 0;
    int held = 0;
    bool wasOpen = false;

    QTimer::singleShot(600, [&]{ settled = log->blockCount(); });

    QTimer::singleShot(2400, [&]
    {
        held = log->blockCount();

        QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        wasOpen = dialog != nullptr;

        if(dialog)
            dialog->reject();
    });

    newGameItem->trigger();

    INFO("settled " << settled << ", held " << held);
    REQUIRE(wasOpen);
    REQUIRE(settled >= before);
    REQUIRE(held == settled);

    // Cancelled, the same game runs on: nothing cleared the log, and it grows.
    pump(2000);
    REQUIRE(log->blockCount() > held);
    REQUIRE(window.isVisible());
}

// THE MENU BAR'S RULES (the redesign's phase 9): over a running game it holds the
// game, as the pause menu does (the owner's call, 2026-10-21), and Esc closes it -
// the sheet first, before the pause menu Esc otherwise toggles.
TEST_CASE("Rules holds a watched game and Esc closes it", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    RulesSheet* rules = window.findChild<RulesSheet*>();
    PauseOverlay* pause = window.findChild<PauseOverlay*>();
    QAction* rulesItem = window.findChild<QAction*>("rulesAction");
    REQUIRE(log != nullptr);
    REQUIRE(pages != nullptr);
    REQUIRE(rules != nullptr);
    REQUIRE(pause != nullptr);
    REQUIRE(rulesItem != nullptr);

    answerModalWatching(300);
    window.findChild<QPushButton*>("newGame")->click();
    pump(2000);
    REQUIRE(log->blockCount() > 1);

    REQUIRE(rulesItem->isEnabled());
    rulesItem->trigger();

    REQUIRE(rules->isVisible());
    REQUIRE(rules->geometry() == pages->rect());

    // Across the middle of the hold, as the pause case measures it.
    pump(600);
    const int settled = log->blockCount();
    pump(1800);
    const int held = log->blockCount();

    INFO("settled " << settled << ", held " << held);
    REQUIRE(held == settled);

    // Esc closes the sheet, and does not open the pause menu on the way.
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &escape);

    REQUIRE_FALSE(rules->isVisible());
    REQUIRE_FALSE(pause->isVisible());

    // And the game runs on.
    pump(2000);
    REQUIRE(log->blockCount() > held);
}

TEST_CASE("Exit over Rules raises the pause menu in its place", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(200);

    QPlainTextEdit* log = window.findChild<QPlainTextEdit*>();
    RulesSheet* rules = window.findChild<RulesSheet*>();
    PauseOverlay* pause = window.findChild<PauseOverlay*>();
    REQUIRE(log != nullptr);
    REQUIRE(rules != nullptr);
    REQUIRE(pause != nullptr);

    answerModalWatching(300);
    window.findChild<QPushButton*>("newGame")->click();
    pump(2000);

    window.findChild<QAction*>("rulesAction")->trigger();
    REQUIRE(rules->isVisible());

    QAction* exitItem = window.findChild<QAction*>("exitAction");
    REQUIRE(exitItem->isEnabled());
    exitItem->trigger();

    REQUIRE(pause->isVisible());
    REQUIRE_FALSE(rules->isVisible());

    // STILL HELD, by the pause now: the sheet's hold went only once the pause
    // menu's was taken.
    pump(600);
    const int settled = log->blockCount();
    pump(1800);
    REQUIRE(log->blockCount() == settled);
}

// The digit keys are read on the window, so the sheet covering the fan is not
// what keeps them off it: the guard in keyPressEvent() is.
TEST_CASE("No card is played from the keyboard under Rules", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    window.activateWindow();
    pump(200);

    answerModalFastSeatedGame(300);
    window.findChild<QPushButton*>("newGame")->click();

    BidStrip* bidStrip = window.findChild<BidStrip*>();
    REQUIRE(bidStrip != nullptr);
    REQUIRE(pumpUntil(10000, [bidStrip]{ return bidStrip->isVisible(); }));

    // 0 is always legal for the opener, which seat 0 is.
    QPushButton* zero = bidButton(*bidStrip, 0u);
    REQUIRE(zero != nullptr);
    zero->click();

    TableScene* scene = window.findChild<TableScene*>();
    REQUIRE(scene != nullptr);
    REQUIRE(pumpUntil(10000, [scene]{ return scene->handItem()->isPlayable(0); }));

    window.findChild<QAction*>("rulesAction")->trigger();

    RulesSheet* rules = window.findChild<RulesSheet*>();
    REQUIRE(rules->isVisible());

    QKeyEvent one(QEvent::KeyPress, Qt::Key_1, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &one);
    pump(200);

    // Still waiting on the card: the 1 under the sheet played nothing.
    REQUIRE(scene->handItem()->isPlayable(0));

    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &escape);
    REQUIRE_FALSE(rules->isVisible());

    // With the sheet gone the same key plays it.
    QCoreApplication::sendEvent(&window, &one);
    pump(200);
    REQUIRE_FALSE(scene->handItem()->isPlayable(0));
}

TEST_CASE("Rules opens on the menu page for the game New Game offers", "[main-window]")
{
    MainWindow window;
    window.resize(1000, 700);
    window.show();
    pump(100);

    QStackedWidget* pages = window.findChild<QStackedWidget*>();
    RulesSheet* rules = window.findChild<RulesSheet*>();
    QAction* rulesItem = window.findChild<QAction*>("rulesAction");
    REQUIRE(pages != nullptr);
    REQUIRE(rules != nullptr);
    REQUIRE(rulesItem != nullptr);

    // Live with no game, unlike Exit.
    REQUIRE(rulesItem->isEnabled());
    REQUIRE_FALSE(window.findChild<QAction*>("exitAction")->isEnabled());

    rulesItem->trigger();
    REQUIRE(rules->isVisible());
    REQUIRE(pages->currentIndex() == 0);

    // The dialog's defaults are the game it would deal.
    QLabel* text = rules->findChild<QLabel*>("rulesText");
    REQUIRE(text != nullptr);

    const QString first = rulessheet::linesFor(newgame::defaults()).front();
    REQUIRE(text->text().contains(first.toHtmlEscaped()));

    rules->findChild<QPushButton*>("rulesClose")->click();
    REQUIRE_FALSE(rules->isVisible());
    REQUIRE(pages->currentIndex() == 0);
}
