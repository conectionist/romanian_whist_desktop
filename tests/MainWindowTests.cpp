#include <catch2/catch_test_macros.hpp>

#include "MainWindow.h"
#include "paint/Theme.h"
#include "table/TableScene.h"
#include "ui/PauseDialog.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
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
// Runs the event loop for a while. Not a sleep: the queued snapshots this is
// waiting for are delivered by the loop, so a blocking wait would hold the very
// thread that has to deliver them - the asymmetry HumanSeatTests records as
// "anything answering a prompt has to pump, not wait".
void pump(int ms)
{
    QElapsedTimer clock;
    clock.start();

    while(clock.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

// Answers whichever modal dialog is up, FROM INSIDE ITS NESTED EVENT LOOP.
// QDialog::exec() does not return until something answers it, so a test that
// clicked the button and then looked would never reach the looking - the only
// way in is a timer that fires within that loop.
void answerModal(int afterMs, int result)
{
    QTimer::singleShot(afterMs, [result]
    {
        if(QDialog* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            dialog->done(result);
    });
}

// The same, flipping the setup dialog to "I'm watching" first.
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

TEST_CASE("A game paused from the menu button stops, and resumes", "[main-window]")
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

    QPushButton* menu = window.findChild<QPushButton*>("menu");
    REQUIRE(menu != nullptr);

    // MEASURED ACROSS THE MIDDLE OF THE PAUSE, not from the click. The pause
    // lands at the next pace(), so a frame or two still arrives after the button
    // - what it claims is that the game is stopped THEREAFTER. The gap sampled
    // is 1.8s of a 250ms spectator beat, which is seven frames that must not
    // appear.
    int settled = -1;
    int held = -1;

    QTimer::singleShot(800, [&]{ settled = log->blockCount(); });
    QTimer::singleShot(2600, [&]{ held = log->blockCount(); });
    answerModal(2800, PauseDialog::Resume);

    menu->click();
    pump(400);

    const int afterResume = log->blockCount();
    INFO("settled " << settled << ", held " << held << ", after resume " << afterResume);

    REQUIRE(settled > 0);
    REQUIRE(held == settled);

    // And it is a pause rather than a stop: the game goes on afterwards.
    pump(2000);
    REQUIRE(log->blockCount() > afterResume);
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

    QPushButton* menu = window.findChild<QPushButton*>("menu");
    REQUIRE(menu != nullptr);

    answerModal(300, PauseDialog::MainMenu);
    menu->click();
    pump(500);

    REQUIRE(pages->currentIndex() == 0);
    REQUIRE(window.isVisible());
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

    QPushButton* menu = window.findChild<QPushButton*>("menu");
    answerModal(300, PauseDialog::MainMenu);
    menu->click();
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
    // NOT A SECOND COPY OF TableSceneTests' "A spectator game reserves no strip",
    // which asserts the same two rects against a hand-built snapshot. What that
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

    const qreal cardHeight = Theme::light().cardHeightFor(scene->sceneRect().size().toSize());
    REQUIRE(cardHeight > 0.0);

    REQUIRE(scene->handRect(cardHeight).isEmpty());
    REQUIRE(scene->tableRect(cardHeight) == scene->sceneRect());
}
