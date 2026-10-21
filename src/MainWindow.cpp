#include "MainWindow.h"
#include "ui_MainWindow.h"

#include "paint/Theme.h"
#include "table/HandItem.h"
#include "table/TableScene.h"
#include "table/TableView.h"
#include "table/TrickPanelItem.h"
#include "ui/BidStrip.h"
#include "ui/CardArtWindow.h"
#include "ui/GameOverPanel.h"
#include "ui/MainMenuPage.h"
#include "ui/MenuBar.h"
#include "ui/NewGameDialog.h"
#include "ui/PauseOverlay.h"
#include "ui/RoundSheet.h"
#include "ui/RulesSheet.h"
#include "ui/SnapshotLog.h"

#include <romanian_whist/GameEngine.h>

#include <QCloseEvent>
#include <QKeyEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QMenu>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QResizeEvent>
#include <QShortcut>
#include <QStyleHints>

#include <cstdint>
#include <memory>
#include <utility>

using namespace romanian_whist;

namespace
{
// PHASE A's HARDCODED GAME IS GONE, and this is what replaced it: nothing. The
// seed, the seat and the setup building all lived here, and D1's dialog answers
// all three - so the client no longer has an opinion about what game to play
// until somebody asks for one. (It also took four strategy includes with it,
// which stayed behind at the top of this file until the Reckoner nearly became a
// fifth dead one.)
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

    // SECTION 16.1's SHEET, on the payload section 21.3 asked for. Registered as
    // a metatype in MetaTypes.cpp alongside the two prompts below, and for the
    // same reason.
    connect(&controller, &GameController::roundScored,   this, &MainWindow::onRoundScored);

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
    buildMenuBar();

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

// The table page: the table and nothing else. Since the table redesign's phase 8
// the scoreboard, the trick panel and the corner buttons are pinned to its corners
// inside it, drawn by the scene at the table's scale.
//
// THE PALETTE IS RESOLVED ONCE, here, and handed to everything. Each widget
// asking QGuiApplication for the scheme itself would work today and would be
// several places to change when section 20's preference arrives.
void MainWindow::buildTable()
{
    theme = Theme::forScheme(QGuiApplication::styleHints()->colorScheme());

    scene = new TableScene(this);
    table = new TableView(scene, this);

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

    // THE TRICK PANEL, owned by the scene as the fan is, and for the fan's reason a
    // handle here: it has the same second source, the prompt for your card.
    trickPanel = scene->trickPanelItem();

    // THE CORNER BUTTONS ARE LEFT UNCONNECTED, and the scene builds them disabled:
    // Round history and Last trick open the sheets phase 11 builds.

    scene->setTheme(theme);
    bidStrip->setTheme(theme);

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

    // THE TABLE TAKES THE PAGE. The info strip that ran across its top, the
    // scoreboard column beside it and the prompt bar beneath it are the
    // scoreboard and the trick panel in its corners now (sections 2.9 and 2.10).
    pages = new QStackedWidget(this);
    pages->addWidget(menuPage);
    pages->addWidget(table);

    setCentralWidget(pages);

    // A CHILD OF THE STACK, not a page in it, and not a dialog. It covers
    // whichever page is showing rather than replacing one - the point is to dim
    // the table and still show it. Hidden until a game is paused, and its
    // geometry is kept by resizeEvent() below.
    pauseOverlay = new PauseOverlay(pages);
    pauseOverlay->setTheme(theme);
    pauseOverlay->hide();

    connect(pauseOverlay, &PauseOverlay::resumeRequested,
            this, [this]{ setPauseMenuVisible(false); });

    connect(pauseOverlay, &PauseOverlay::mainMenuRequested, this, &MainWindow::abandonToMenu);

    // THE SAME ARRANGEMENT, for the same reason plus one of its own: section
    // 16.2 draws the standings OVER the final state, and section 4.1 publishes
    // the last snapshot before emitting gameOver precisely so that the table
    // underneath this is the game that just ended.
    // SECTION 16.1's SHEET, the third overlay and the same arrangement as the two
    // above it. Raised over the round it reports, which is the state the table is
    // still showing when onRoundScored() fires.
    roundSheet = new RoundSheet(pages);
    roundSheet->setTheme(theme);
    roundSheet->hide();

    connect(roundSheet, &RoundSheet::continueRequested, this, [this]
    {
        hideRoundSheet();

        // THE FINAL ROUND HANDS OVER rather than dealing anything - there is no
        // round 27, which is why the button says Continue in every round rather
        // than naming one. onGameOver() has already been delivered by then: the
        // game thread was held inside onRoundScored(), so releasing it runs the
        // rest of the game out and the panel arrives on its own.
        //
        // Nothing to do here for it. The state check is in onGameOver(), which
        // raises the panel over whatever is on screen.
    });

    gameOverPanel = new GameOverPanel(pages);
    gameOverPanel->setTheme(theme);
    gameOverPanel->hide();

    // THE MENU BAR'S RULES, the fourth overlay and the same arrangement. It opens
    // over any of the three above and closes back to it.
    rulesSheet = new RulesSheet(pages);
    rulesSheet->setTheme(theme);
    rulesSheet->hide();

    connect(rulesSheet, &RulesSheet::closeRequested, this, &MainWindow::hideMenuSheets);

    // THE SAME abandonToMenu() THE PAUSE MENU USES, and it needs nothing added
    // for a game that has already ended: stop() is idempotent, and a game that
    // reached gameOver emits no gameStopped for the returningToMenu latch to
    // have to catch - GameBridge::onGameStopped() fires only when a stop was
    // requested, and this game was not stopped, it finished.
    connect(gameOverPanel, &GameOverPanel::mainMenuRequested, this, &MainWindow::abandonToMenu);

    connect(gameOverPanel, &GameOverPanel::playAgainRequested, this, [this]
    {
        // HIDDEN FIRST, because what follows opens a modal dialog and this panel
        // would otherwise stand over the table behind it for as long as the
        // player takes to answer.
        gameOverPanel->hide();

        // SECTION 16.2's "play again reopens the setup dialog pre-filled with
        // the same settings" - which is exactly what this slot already does,
        // from lastConfig. It is section 4.3's ordering from the top: stop(),
        // join, prepare(), start(), with a freshly built GameSetup.
        //
        // The dialog's exec() runs BEFORE startGame(), so the trap that function
        // records - no modal event loop between prepare() and start() - holds
        // here as it does from the menu page.
        showNewGameDialog();

        // AND BACK UP IF THE PLAYER CANCELLED IT, which is the dead end this
        // whole panel exists to remove, walked into from the other side: a
        // dismissed dialog leaves a finished game on the table, and hiding the
        // panel on the way in would have left nothing to press. startGame() is
        // what moves the state on, so this reads the state rather than the
        // dialog's answer.
        if(tableState == TableState::Finished)
            showGameOverPanel();
    });
}

void MainWindow::showMainMenu()
{
    pages->setCurrentWidget(menuPage);

    // THE LOG DOCK IS LEFT EXACTLY AS THE PLAYER LEFT IT, and this used to hide
    // it here on the reasoning that a diagnostic for a game does not belong on a
    // page with no game. The comment claimed the dock came back with the next
    // one; it did not. QDockWidget::hide() unchecks its own toggleViewAction() -
    // that two-way sync is the entire reason buildLogDock() uses Qt's action
    // rather than one of ours - so hiding it here turned View > Log off, and
    // nothing turned it back on.
    //
    // Nothing else has to be added to fix it: startGame() already clears the log
    // for the elapsed column's sake, so a dock left open shows the new game from
    // its first line, and this window goes on holding no "is the log showing"
    // state of its own to get wrong.
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

    // A RUNNING GAME WAITS FOR THE ANSWER, which the menu bar's New Game made
    // possible: before the redesign's phase 9 the dialog opened only over the
    // menu page or a game that had ended. Cancelled, the game runs on from where
    // it stood; accepted, startGame() replaces it.
    dialogHolding = tableState == TableState::Playing;
    applyHold();

    const bool accepted = dialog.exec() == QDialog::Accepted;

    dialogHolding = false;

    if(!accepted)
    {
        applyHold();
        restoreFocus();
        return;
    }

    lastConfig = dialog.config();

    // A FRESH GameSetup EVERY TIME, never a stashed one: it is move-only and
    // spent by the move into start(), so even the path where start() throws
    // needs the dialog's config re-built rather than a copy kept (section 13).
    // THE SHEET'S DISMISSAL IS A PER-GAME SETTING, so it is read here with the
    // rest of what the dialog answered rather than kept on the Config the window
    // holds for pre-filling - those are the last game's settings, and this is
    // the game being dealt.
    waitAfterEachRound = lastConfig.waitAfterEachRound;

    // ON THE Pacing RATHER THAN ONLY HERE, because the bridge is what has to
    // hold the game - see Pacing::waitAfterEachRound. pacingFor() stays a pure
    // mapping of the slider, which is what its own tests assert.
    Pacing pacing = newgame::pacingFor(lastConfig.botSpeed);
    pacing.waitAfterEachRound = lastConfig.waitAfterEachRound;

    startGame(newgame::buildDeal(lastConfig),
              newgame::humanSeatOf(lastConfig),
              pacing);
}

bool MainWindow::canPause() const
{
    // NOTHING TO PAUSE ON THE MENU PAGE, or on a game that has already ended.
    // Esc is bound on the window, so it arrives at the pause whichever page is
    // showing.
    //
    // PLAYING EXACTLY, not "anything but NoGame". A finished game now has a
    // panel of its own with the two ways on, and a Resume raised over it would
    // promise a game that has already ended - which is the same reason the flag
    // this reads was cleared at gameOver before there was anywhere to go.
    if(tableState != TableState::Playing)
        return false;

    // NOT OVER A ROUND SHEET THAT IS HOLDING THE GAME. That sheet has its own way
    // on and its own hold; a pause menu over it would be a second overlay on the
    // first, and its Resume would look like it should dismiss the sheet. The
    // game-over panel is protected by tableState above - this one needs saying,
    // because a game showing a round sheet is still Playing.
    //
    // ON THE HOLD AND NOT ON isVisible(), which is the version this was written
    // as first and which made the Menu button DEAD for a third of a watched
    // game: with the sheet auto-dismissing it is on screen for the whole trick
    // dwell after every round, and refusing the menu there refuses it for over a
    // second at a time, ~26 times a game. Caught by D1's own pause case, which
    // clicked Menu and found nothing opened.
    //
    // A dwelling sheet is transient chrome, so the menu takes precedence and the
    // sheet goes on the way past (setPauseMenuVisible()).
    return !roundSheetHolding;
}

// EXIT SAYS WHAT A CLICK ON IT WOULD DO: greyed wherever canPause() would refuse
// it - on the menu page, over the game-over panel, under a round sheet that is
// holding the game - rather than live and doing nothing (PR 34's review, finding
// #4, made of the Menu item Exit replaced). Called wherever either of canPause()'s
// two facts changes.
void MainWindow::updatePauseAction()
{
    if(exitAction)
        exitAction->setEnabled(canPause());
}

void MainWindow::setPauseMenuVisible(bool visible)
{
    // ONE FACT, NOT TWO. "The menu is up" and "the game is held" are set here
    // together and nowhere else, so no caller can put them out of step - which
    // is the hardening phase E's box asks for on the fan's two sources, applied
    // to the pause before it has a chance to go wrong.
    //
    // NOTHING TO PAUSE, OR NOTHING TO RAISE IT OVER: see canPause(). Esc comes
    // here whatever Exit shows.
    if(visible && !canPause())
        return;

    if(visible && roundSheet->isVisible())
        hideRoundSheet();

    // WHEN THE OVERLAY WAS A MODAL QDialog THIS WAS AN RAII GUARD around exec(),
    // and losing that is the one thing this shape gives up: a pause left set
    // parks the game thread in pace() with nothing on screen to release it. So
    // the pause is raised only where it is also shown, and every path that ends
    // a game clears both - startGame() before it prepares, abandonToMenu() with
    // the stop, and closeEvent()'s stop() through cancelPacing() regardless.
    //
    // THROUGH applyHold() SINCE D2, because the pause menu is no longer the only
    // reason to hold a game - see there.
    pauseMenuHolding = visible;
    applyHold();

    if(!visible)
    {
        pauseOverlay->hide();

        // AND THE KEYBOARD BACK WHERE THE PAUSE TOOK IT FROM. focusResume() put
        // it on the Resume button, and hiding that button does not give it back
        // - so a pause taken at a bid prompt left the strip's digit keys dead
        // afterwards, with its buttons still working, which is the shape that
        // looks finished.
        //
        // ASKED OF WHAT IS SHOWING rather than remembered across the pause: the
        // strip is visible exactly while the engine is parked waiting for a bid,
        // which is the whole of the question.
        restoreFocus();

        return;
    }

    pauseOverlay->setGeometry(pages->rect());
    pauseOverlay->show();
    pauseOverlay->raise();
    pauseOverlay->focusResume();
}

void MainWindow::showPauseMenu()
{
    setPauseMenuVisible(true);

    // EXIT OVER RULES OR SETTINGS: the pause menu goes up and the sheet comes
    // down. In that order, so the game is never released between the two holds.
    if(pauseOverlay->isVisible())
        hideMenuSheets();
}

void MainWindow::showRules()
{
    hideMenuSheets();

    rulesSheet->showRules(lastConfig);

    rulesSheet->setGeometry(pages->rect());
    rulesSheet->show();
    rulesSheet->raise();

    // ONLY A GAME IN PLAY IS HELD. Over the menu page there is nothing to hold,
    // and over the game-over panel nothing left to.
    sheetHolding = tableState == TableState::Playing;
    applyHold();

    rulesSheet->focusClose();
}

void MainWindow::hideMenuSheets()
{
    if(!openMenuSheet() && !sheetHolding)
        return;

    rulesSheet->hide();

    sheetHolding = false;
    applyHold();

    restoreFocus();
}

QWidget* MainWindow::openMenuSheet() const
{
    if(rulesSheet && rulesSheet->isVisible())
        return rulesSheet;

    return nullptr;
}

void MainWindow::keepMenuSheetOnTop()
{
    // A round scored or a game ended under Rules: the hold lands at the game's
    // next pause, and the last trick of a round or a game can finish first. The
    // sheet the player is reading stays where it is.
    if(QWidget* sheet = openMenuSheet())
    {
        sheet->raise();
        restoreFocus();
    }
}

void MainWindow::applyHold()
{
    // THE ONLY CALLER OF setPaused() IN THE CLIENT, and that is the whole point
    // of it existing. (Since the redesign's phase 9 there is a third reason, the
    // setup dialog opened over a running game; it is one more member here, and
    // the argument below holds for it unchanged.)
    //
    // D1 had one reason to hold a game and set the flag where it showed the
    // menu. D2 adds a second - section 16.1's round sheet, for a player who
    // asked to be waited for - and two callers writing one flag is the defect
    // that shape was chosen to prevent, not a new one. They would collide the
    // first time they overlapped: `tableState` is Playing while the sheet is up,
    // so Esc opens the pause menu over it, and Resume would then clear the flag
    // and release the SHEET's hold along with its own.
    //
    // So the two reasons are two members, this computes the hold from both, and
    // GameBridge is untouched - the arbitration belongs to the one object that
    // knows every reason there is.
    controller.setPaused(pauseMenuHolding || roundSheetHolding || dialogHolding || sheetHolding);
}

void MainWindow::showRoundSheet(const RoundResult& result)
{
    roundSheet->showResult(result);

    roundSheet->setGeometry(pages->rect());
    roundSheet->show();
    roundSheet->raise();

    if(roundSheetHolding)
        roundSheet->focusContinue();

    keepMenuSheetOnTop();
}

void MainWindow::hideRoundSheet()
{
    roundSheet->hide();

    // THE HOLD GOES WITH THE SHEET, always, and through applyHold() so that a
    // pause menu standing at the same time is not released with it.
    roundSheetHolding = false;
    updatePauseAction();
    applyHold();

    // The keyboard back to the strip if the strip is what is waiting, exactly as
    // the pause menu returns it - focusContinue() took it, and hiding the button
    // does not give it back.
    restoreFocus();
}

void MainWindow::restoreFocus()
{
    // The menu bar's sheets open over everything else.
    if(rulesSheet && rulesSheet->isVisible())
    {
        rulesSheet->focusClose();
        return;
    }

    // THEN THE REST, TOP FIRST. The pause menu hides any round sheet on its way up, and a game
    // that has ended has neither a sheet holding it nor a strip waiting, so at
    // most one of these is ever the answer - the order only says which wins if a
    // later change lets two stand at once.
    if(pauseOverlay->isVisible())
    {
        pauseOverlay->focusResume();
        return;
    }

    if(gameOverPanel->isVisible())
    {
        gameOverPanel->focusDefaultButton();
        return;
    }

    if(roundSheet->isVisible() && roundSheetHolding)
    {
        roundSheet->focusContinue();
        return;
    }

    if(bidStrip->isVisible())
        bidStrip->setFocus();
}

void MainWindow::showGameOverPanel()
{
    gameOverPanel->setGeometry(pages->rect());
    gameOverPanel->show();
    gameOverPanel->raise();
    gameOverPanel->focusDefaultButton();

    keepMenuSheetOnTop();
}

void MainWindow::abandonToMenu()
{
    setPauseMenuVisible(false);

    // AND THE GAME-OVER PANEL, because this is the button on BOTH of them. From
    // the pause it is hidden already; from the panel it is what was just
    // clicked, and a panel left up would stand over the menu page.
    gameOverPanel->hide();

    // The sheet cannot be up here - the pause menu is unreachable while it is -
    // but the hold it may have set has to go, and stop() below is what releases
    // the game thread either way.
    roundSheet->hide();
    roundSheetHolding = false;
    updatePauseAction();

    // And the menu bar's sheets, which can stand over the game-over panel.
    rulesSheet->hide();
    sheetHolding = false;

    // SET BEFORE THE STOP, exactly as closeEvent() sets `closing` before its
    // own - onGameStopped() closes the window for any stop it cannot account
    // for, and this is the second such stop the client has ever produced.
    returningToMenu = true;

    controller.stop();

    // THE ABANDONED GAME'S QUEUED SIGNALS, dropped - the second moment an old
    // game's queue is worthless, which is why the purge is no longer private to
    // prepare(). Every frame the game thread published between this click and
    // the join above is still in this thread's queue, and is delivered after
    // showMainMenu() below: a line into the log of a game that no longer exists.
    //
    // SMALL HERE, AND NOT NOTHING. Main menu is only reachable from the pause,
    // which has usually let the window go idle and drain the queue first -
    // measured at one frame for a click that does not wait, against the 618 that
    // prepare()'s purge was written for. It is the same defect either way, and
    // this is the path with nothing else standing between it and the menu page.
    //
    // AFTER THE STOP, never before it. The join is what makes this a purge
    // rather than a race.
    controller.discardQueuedSignals();

    tableState = TableState::NoGame;
    updatePauseAction();
    showMainMenu();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // The overlay is a child rather than a layout item, so nothing else moves
    // it. Only while it is up: a hidden widget's geometry is nobody's business,
    // and it is set again on the way in.
    if(pauseOverlay && pauseOverlay->isVisible())
        pauseOverlay->setGeometry(pages->rect());

    if(gameOverPanel && gameOverPanel->isVisible())
        gameOverPanel->setGeometry(pages->rect());

    if(roundSheet && roundSheet->isVisible())
        roundSheet->setGeometry(pages->rect());

    if(rulesSheet && rulesSheet->isVisible())
        rulesSheet->setGeometry(pages->rect());
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

void MainWindow::buildMenuBar()
{
    // SECTION 2.12's FOUR, and a QAction each so that what a menu action gives -
    // a trigger, a greyed state, a shortcut - works as it did on the QMenuBar this
    // replaced. The bar's buttons stand for them (QToolButton::setDefaultAction()).
    //
    // ALT AND A LETTER, set as each action's shortcut. A painted label has no
    // underline to show a mnemonic with, and the button takes its text from the
    // action without the '&' anyway.
    newGameAction = new QAction(tr("New Game"), this);
    newGameAction->setObjectName(QStringLiteral("newGameAction"));
    newGameAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_N));
    connect(newGameAction, &QAction::triggered, this, &MainWindow::showNewGameDialog);

    // NOT YET: the redesign's phase 9 builds the sheet this opens after the bar,
    // and an item that looks live and does nothing is its own defect.
    settingsAction = new QAction(tr("Settings"), this);
    settingsAction->setObjectName(QStringLiteral("settingsAction"));
    settingsAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_S));
    settingsAction->setEnabled(false);

    // Live on every page: the rules for the game on the table, or on the menu
    // page for the one New Game will offer.
    rulesAction = new QAction(tr("Rules"), this);
    rulesAction->setObjectName(QStringLiteral("rulesAction"));
    rulesAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_R));
    connect(rulesAction, &QAction::triggered, this, &MainWindow::showRules);

    // THE PAUSE MENU'S WAY IN FOR THE MOUSE, and Esc is its keyboard half. The
    // mock-up's Exit "returns to the main menu"; it does so through the pause
    // menu, so it can be taken back with Resume. It replaces the temporary Menu
    // item phase 8 put here when it took the prompt bar's Menu button away.
    exitAction = new QAction(tr("Exit"), this);
    exitAction->setObjectName(QStringLiteral("exitAction"));
    exitAction->setShortcut(QKeySequence(Qt::ALT | Qt::Key_X));
    connect(exitAction, &QAction::triggered, this, &MainWindow::showPauseMenu);

    // THE DEVELOPER'S MENU, kept: the log is the thing to reach for when the table
    // looks wrong. At the right, in the place of the mock-up's Players switch,
    // which was not a game control either (the owner's call, 2026-10-21).
    QMenu* view = new QMenu(tr("&View"), this);
    view->setObjectName(QStringLiteral("viewMenu"));

    // Qt's own action for the dock, not one of ours - see buildLogDock().
    QAction* logAction = logDock->toggleViewAction();
    logAction->setText(tr("&Log"));
    view->addAction(logAction);

    view->addSeparator();

    QAction* cardArtAction = view->addAction(tr("&Card art..."));
    connect(cardArtAction, &QAction::triggered, this, &MainWindow::showCardArt);

    bar = new MenuBar({ newGameAction, settingsAction, rulesAction, view, exitAction }, this);
    bar->setTheme(theme);
    setMenuWidget(bar);

    // View pops its menu up on a press, so a shortcut's click would do nothing.
    QShortcut* viewShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_V), this);
    connect(viewShortcut, &QShortcut::activated, bar, &MenuBar::showViewMenu);

    updatePauseAction();
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

void MainWindow::startGame(GameDeal deal, std::optional<Seat> humanSeat, Pacing pacing)
{
    // CLEARED HERE, not in the constructor: the elapsed column is measured from
    // a game's first snapshot, so the origin has to be re-armed by whatever
    // starts a game rather than once by whatever builds the window.
    haveStarted = false;

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
    // the defence - but a stale PlayPrompt is also what the fan would be redrawn
    // from. The trick panel holds its own copy, and is cleared below.
    activeBetPrompt = BetPrompt{};
    activePrompt = PlayPrompt{};

    bidStrip->hide();
    hand->clear();
    hand->setPlayable(false);
    trickPanel->clearPrompt();

    // AND THE PAUSE WITH THEM, before prepare() replaces the bridge the flag
    // lives on. Unreachable today - every path here comes from the menu page,
    // where no game is running - and one line against a menu left standing over
    // a game that has just been dealt.
    pauseOverlay->hide();

    // AND THE PANEL THE LAST GAME ENDED ON, which is not unreachable at all: it
    // is where Play again is pressed, and it is still up while the setup dialog
    // it opens is answered.
    gameOverPanel->hide();

    // AND THE SHEET, with its hold - a game abandoned from a round sheet would
    // otherwise leave roundSheetHolding set into the next one, where applyHold()
    // would pause a game nobody asked to pause.
    roundSheet->hide();
    roundSheetHolding = false;
    updatePauseAction();
    pauseMenuHolding = false;
    dialogHolding = false;

    // AND THE MENU BAR'S SHEETS, which can be up over a game New Game replaces.
    rulesSheet->hide();
    sheetHolding = false;

    // Cleared here rather than in showPauseMenu(), so that the flag is false for
    // exactly one stop and cannot survive into a game it was not raised for.
    returningToMenu = false;
    tableState = TableState::Playing;
    updatePauseAction();

    showTable();

    // A REAL SEAT since C1, where phases A and B passed std::nullopt. The one
    // optional decides three things at once and they must not be separable:
    // GameBridge stamps it onto every snapshot as humanSeat, which is what gives
    // canSeeHand() a viewer to be asked about; GameController builds a
    // QtMoveProvider from it and fills that seat in start(); and delayOf() reads
    // it to choose which beat to pace at.
    //
    // THE GAME IS SLOWER FROM HERE, and correctly so. Pacing{} is unchanged, but
    // delayOf() picks spectatorBeat (250ms) only when nobody is seated and
    // beat (700ms) when somebody is - section 4.1's split, which phases A and B
    // only ever saw one side of. A spectator watches; a player is waited for.
    // FROM THE DIALOG SINCE D1, both of them. The one optional decides three
    // things at once and they must not be separable: GameBridge stamps it onto
    // every snapshot as humanSeat, which is what gives canSeeHand() a viewer;
    // GameController builds a QtMoveProvider from it and fills that seat in
    // start(); and delayOf() reads it to choose which beat to pace at.
    //
    // NO MODAL DIALOG MAY RUN BETWEEN prepare() AND HERE, and that is the one
    // ordering trap in the reuse. prepare() calls stop() and then purges this
    // object's posted MetaCalls - and THAT PURGE is what drops game one's queued
    // gameStopped before it reaches onGameStopped(). Anything spinning the event
    // loop in that gap delivers the relay instead, and the window closes.
    controller.prepare(humanSeat, pacing);
    controller.start(std::move(deal));
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

    // THE SHEET GOES WHEN THE GAME MOVES ON, which is what dismisses it for a
    // player who did not ask to be waited for: the dwell ends, onRoundComplete()
    // publishes the next round, and this is where that arrives.
    //
    // GUARDED ON THE HOLD rather than hidden unconditionally, because a waited
    // sheet must survive whatever else is published - and while the game thread
    // is held inside pace(), nothing is.
    if(!roundSheetHolding && roundSheet->isVisible())
        hideRoundSheet();

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

    // AND THE TRICK PANEL, which says why the dimmed cards are out: legality from
    // the prompt, the suit being followed from the frame it already holds.
    trickPanel->setPrompt(prompt);
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
    trickPanel->clearPrompt();

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
    // BEFORE THE MODIFIER GUARD AND BEFORE THE DIGITS. Esc raises the pause
    // menu, which is the keyboard half of the menu bar's Exit, and
    // dismisses it again - so the key that opened it is the key that leaves.
    // Checked first because the digit path below subtracts Key_1 from whatever
    // arrives and would otherwise have to reason about the result.
    //
    // THE MENU BAR'S SHEETS FIRST, which open over the pause menu as over
    // everything else: Esc closes the one on top, as the mock-up's does.
    if(event->key() == Qt::Key_Escape)
    {
        if(openMenuSheet())
            hideMenuSheets();
        else
            setPauseMenuVisible(!pauseOverlay->isVisible());

        return;
    }

    // THE TABLE'S KEYS ARE NOT LIVE WHILE THE MENU IS UP. The overlay swallows
    // the mouse by covering the table; the keyboard needs saying, because these
    // digits are read HERE on the window rather than on the fan - so a 3 pressed
    // over the pause menu would otherwise play the third card. The same goes for
    // the menu bar's sheets.
    if(pauseOverlay->isVisible() || openMenuSheet())
    {
        QMainWindow::keyPressEvent(event);
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
    trickPanel->clearPrompt();

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

void MainWindow::onRoundScored(RoundResult result)
{
    // THE LAST ROUND OF AN UNATTENDED GAME GETS NO SHEET. Its numbers are the
    // ones the game-over panel is about to show in full, and a sheet that
    // flashed for a dwell and was replaced would be two panels in a second.
    //
    // A player who asked to be waited for DOES get it, and their Continue is
    // what hands over to the panel - which is the whole reason that button says
    // Continue rather than naming a round to deal.
    const bool finalRound = result.roundNumber >= result.roundCount;

    if(finalRound && !waitAfterEachRound)
        return;

    // THE WINDOW'S RECORD OF A HOLD THE BRIDGE HAS ALREADY TAKEN. It is not what
    // pauses the game - onRoundScored() on the game thread is, before it waits,
    // because a hold set from here would be racing that wait and would lose it
    // outright at zero dwell (Pacing::waitAfterEachRound).
    //
    // What this is for is the RELEASE: applyHold() is the only thing that clears
    // the flag, and it has to know that a sheet is one of the two reasons it is
    // set. The applyHold() below therefore re-asserts a hold that is already
    // taken, which is idempotent, and matters only when a pause menu is up at
    // the same time.
    roundSheetHolding = waitAfterEachRound;
    updatePauseAction();
    applyHold();

    showRoundSheet(result);
}

void MainWindow::onGameOver(std::vector<Standing> standings, std::vector<Standing> winners)
{
    // THE SHEET GOES WHEN THE GAME DOES. On the final round of a waited game the
    // player is still looking at it when Continue releases the thread, and the
    // panel below would otherwise be raised behind it.
    hideRoundSheet();

    bidStrip->hide();
    hand->setPlayable(false);
    trickPanel->clearPrompt();

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

    // THE GAME IS OVER BUT THE TABLE STAYS UP, which is what section 16.2's
    // panel is drawn over - the final snapshot arrived before this signal did
    // (section 4.1), so what is underneath the panel is the game that ended.
    //
    // Finished, not NoGame: the table is still showing a game, and the pause
    // must not open over it - there is nothing to hold, and Resume would promise
    // a game that has already ended. The panel below is the way on, and D1's
    // review is what found that there had to be one.
    tableState = TableState::Finished;
    updatePauseAction();

    gameOverPanel->showResult(standings, winners);
    showGameOverPanel();
}

void MainWindow::showFailure(QString what)
{
    // Neither onGameStopped() nor onGameOver() fires on this path, so the state
    // they move has to be moved here too - otherwise the pause menu is still
    // reachable on a game whose thread has already unwound.
    tableState = TableState::Finished;
    updatePauseAction();

    bidStrip->hide();
    hand->setPlayable(false);
    trickPanel->clearPrompt();

    appendLine(tr("-- the game thread failed: %1 --").arg(what));

    // THE PANEL'S OTHER FACE, and the reason it has one: this ending is as much
    // a dead end as the ordinary one was, and it is the ending a player is least
    // able to guess their way out of. No standings, because there are none, and
    // no Play again - see GameOverPanel::showFailure().
    gameOverPanel->showFailure(what);
    showGameOverPanel();
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
