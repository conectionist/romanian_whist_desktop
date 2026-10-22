#ifndef GAME_OVER_PANEL_H
#define GAME_OVER_PANEL_H

#include "ui/Overlay.h"

#include <romanian_whist/GameEngine.h>

#include <QCoreApplication>
#include <QString>

#include <vector>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// Section 16.2's end of the game: the final standings, who won, and the two ways
// on. D2's panel, PULLED FORWARD INTO D1 by D1's own review - a finished game
// cleared `gameRunning`, which is what the pause menu is gated on, so Menu and
// Esc were both dead and the only thing left in the menu bar was View. There was
// no route from a finished game to anywhere, which made "play again" - the thing
// D1 exists to build - unreachable from the ending it exists for.
//
// D2 KEEPS THE REST OF ITS STEP: roundScored()'s RoundResult payload and the
// round sheet are untouched here. This is the one bullet the dead end needed.
namespace gameover
{

// Translated through QCoreApplication::translate() with an explicit context, for
// the reason table/ScoreboardItem.h gives at length: this is chrome a player
// reads, Q_DECLARE_TR_FUNCTIONS is class-only, and a tr() wrapper of our own
// would hide every string in here from lupdate.

// PORTED FROM romanian_whist_terminal, src/Renderer.cpp - Renderer::
// winnerAnnouncement(), with its test (section 21.4). Verbatim in its wording
// and its structure; std::string became QString and the pieces go through
// translate(), which is the whole of what changed.
//
// "Ana wins with 47 points." for one, "Ana and Bogdan tie for first with 47
// points." for two, "Ana, Bogdan and Dan tie for first with 47 points." for
// three - the last name joined with "and" rather than a comma, however many
// there are.
//
// TAKES THE WINNERS, NOT THE STANDINGS, and that is the whole point of the
// function existing in both clients: standings.front() names ONE winner of a
// drawn game, which is the bug the terminal shipped in 2.1.0 and which
// getWinners() was added in 4.1.0 to end.
QString winnerAnnouncement(const std::vector<romanian_whist::Standing>& winners);

} // namespace gameover

// The panel itself. An Overlay, for two reasons: section 16.2 draws this OVER
// the final state - section 4.1 publishes the last snapshot before emitting
// gameOver precisely so the table underneath is the game that just ended - and
// section 4.4 wants nothing application-modal standing over a window whose X
// must keep working.
class GameOverPanel : public Overlay
{
    Q_OBJECT

public:
    explicit GameOverPanel(QWidget* parent = nullptr);

    // The ordinary ending. Both vectors come straight off the gameOver signal.
    void showResult(const std::vector<romanian_whist::Standing>& standings,
                    const std::vector<romanian_whist::Standing>& winners);

    // THE OTHER ENDING, and it is here so that the failure path is not the dead
    // end this class was written to remove. The run slot caught something, so
    // there are no standings to show and nothing to play again from - the error
    // takes the announcement's place and Main menu is the only button.
    void showFailure(const QString& what);

    // The button the face on show leads with: Play again on the ordinary
    // ending, Main menu on the failure.
    void focusDefault() override;

signals:
    void playAgainRequested();
    void mainMenuRequested();

    // The quiet Round history between the two (the redesign's phase 11, by the
    // owner's call of 2026-10-22): the game's every round, which this panel's dim
    // otherwise keeps the table's own button from. Opened over the panel and closed
    // back to it.
    void historyRequested();

private:
    // One standing, spelled and laid out: place, name, score, and the star every
    // winner takes.
    //
    // THE PLACE COMES FROM Standing::place AND NEVER FROM A ROW COUNTER. Seats
    // level on points share a place and the places they consume are skipped, so
    // 47, 47, 31, 28 reads 1, 1, 3, 4 - a counter would number that table
    // 1, 2, 3, 4 and quietly rank players the engine had ranked equal.
    void addRow(const romanian_whist::Standing& standing, bool isWinner);

    QLabel* title = nullptr;

    // The announcement, under the title where the mock-up's sheets keep their
    // .meta line - or, on the failure face, what went wrong.
    QLabel* announcement = nullptr;

    // The standings table, as labels in a grid. Not painted like the scoreboard
    // is: that panel repaints on every one of a game's ~1300 frames and this one
    // is built once at the end of a game, so the reason for painting there is
    // absent here - and labels keep the text selectable and reachable, which
    // section 20's accessibility pass will thank this for.
    SheetTable* rows = nullptr;

    SheetButton* playAgain = nullptr;
    SheetButton* history = nullptr;
    SheetButton* mainMenu = nullptr;
};

#endif
