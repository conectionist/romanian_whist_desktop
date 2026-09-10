#ifndef NEW_GAME_DIALOG_H
#define NEW_GAME_DIALOG_H

#include "engine/GameBridge.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/Scoreboard.h>
#include <romanian_whist/Seat.h>

#include <QCoreApplication>
#include <QDialog>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSlider;
class QSpinBox;
QT_END_NAMESPACE

// Section 13's setup dialog: one modal screen rather than a wizard, because a
// terminal has one screen and a dialog does not, and seeing the whole shape of a
// game before starting it is strictly better than answering nine questions.
//
// PORTED FROM ../romanian_whist_terminal/src/SetupWizard.h - `GameConfig` is the
// shape below, and section 21.4's row for it says what changes on the way: same
// questions, same bounds, one screen. Two things do NOT come across. The
// Reckoner levels its `AiLevel` lists do not exist in this repository's engine
// submodule at 4.1.0, so offering them would be offering a strategy that cannot
// be built; and its wizard asks its questions in sequence, which is the thing
// this dialog exists not to do.
//
// WHAT IT PRODUCES is a GameSetup with the human seat's moveProvider left null,
// which GameController::start() fills with a SeatProvider (sections 4.2, 4.3).
// It does not build a QtMoveProvider and it does not touch the controller's
// ordering: the dialog answers questions, the controller does prepare, connect,
// start.
namespace newgame
{

// TRANSLATED, the choice BidStrip and InfoStrip make, through
// QCoreApplication::translate() with an explicit context at every call site
// rather than a tr() of our own - Q_DECLARE_TR_FUNCTIONS is class-only whatever
// its name suggests, and a hand-rolled wrapper hides every string from lupdate.
// Section 7 is explicit that this includes the strategy names and the
// round-count sentence, which are the two here least likely to look like UI
// text.

// Which strategy a bot plays with. THE ORDER IS SECTION 13's, which is the
// terminal wizard's, so it doubles as the combo-box index.
enum class BotLevel
{
    Random,
    FirstCard,
    LowRisk,
    Ducking
};

// Every option in menu order, so the combo and the factory below cannot drift
// apart - the terminal's `AiLevels` array, and its reason for existing.
inline constexpr BotLevel BotLevels[] = { BotLevel::Random,
                                          BotLevel::FirstCard,
                                          BotLevel::LowRisk,
                                          BotLevel::Ducking };

// The engine builds its deck from the player count and rejects anything outside
// this range by name (GameEngine::start). Section 13's rule is that a spinner
// which cannot reach an illegal value beats an error message explaining one, so
// these bound the widget rather than being checked after the fact.
inline constexpr unsigned int MinimumSeats = 2;
inline constexpr unsigned int MaximumSeats = 6;

// Long enough for any name a scoreboard can show without eliding, and the reason
// there is a cap at all: ScoreboardWidget sizes its name column from the widest
// name it is given, so an unbounded one takes the table's width with it. The
// terminal's own limit is 10, set by a fixed-width seat table this client does
// not have.
inline constexpr int MaximumNameLength = 20;

struct Config
{
    // Section 13's radio pair, and NOT a mode bolted on later: this is
    // humanSeat == std::nullopt, which is phase A's entire configuration and the
    // state sections 4.3 and 6 null-check for.
    bool spectator = false;

    QString humanName;

    // One level per name, in the same order - the terminal's parallel-vector
    // shape, kept because it is what the dialog's rows are.
    QStringList botNames;
    std::vector<BotLevel> botLevels;

    romanian_whist::GameStructure structure = romanian_whist::GameStructure::S_181;
    bool endWithForeheadAndHidden = true;
    bool all1GamesAreForehead = false;

    // Section 13's "fixed deal", which is shuffleSeed with a friendlier name. It
    // stays in the GUI because it is how a rendering change gets reviewed as a
    // diff.
    std::optional<std::uint32_t> shuffleSeed;

    // A slider position, not a duration. pacingFor() below is what turns it into
    // the three delays, so the Config stays a record of what was ASKED and the
    // mapping lives in one testable place.
    int botSpeed = 2;

    // Does section 16.1's round sheet WAIT, or show for the dwell and go?
    //
    // OFF BY DEFAULT, which is the deliberate answer rather than the lazy one: a
    // player learning the game turns this on to read every round, and most will
    // want it off once they are not.
    //
    // THE ROUND SIBLING OF SECTION 15.3's "wait for me between tricks", which is
    // still phase E's. This one is here because the sheet needs it and because
    // D1 already built the indefinite pace() both would use; the trick one is
    // not, and no disabled checkbox promises it.
    bool waitAfterEachRound = false;
};

// The defaults the dialog opens with the first time. Play again re-opens it on
// the last accepted Config instead (section 16.2).
Config defaults();

// Bots plus the human, unless nobody is playing.
unsigned int seatCount(const Config& config);

// 3*seats + 12, plus the two appended rounds. From Scoreboard::initialize():
// S_181 deals N rounds of one card, then 2..7, then N of eight, then 7..2, then
// N of one - and S_818 the same schedule read backwards, so the STRUCTURE DOES
// NOT CHANGE THE COUNT. Neither does all1GamesAreForehead, which retypes rounds
// rather than adding any.
//
// SECTION 13's MOCKUP SAYS 24 FOR THIS, with three opponents and the forehead
// checkbox ticked, and 24 is the count without the appended pair. The formula is
// the engine's; the mockup is stale.
unsigned int roundCount(const Config& config);

// Section 13's live line: "26 rounds - about 20 minutes". The count is exact and
// the time is a rule of thumb off the bot-speed slider, which is the honest
// division - one is arithmetic over the schedule and the other is a guess.
QString summaryText(const Config& config);

// What is wrong with this config, or nothing. THE DIALOG DISABLES Deal ON IT
// rather than letting GameEngine::start() throw, which is section 13's rule and
// the reason this is a function with a right answer instead of a try/catch.
//
// STRICTER THAN THE ENGINE ON PURPOSE. The engine rejects two names equal BYTE
// FOR BYTE (no trimming, no case folding, deliberate on its side); this rejects
// them case-insensitively, following the terminal's Input::equalIgnoringCase.
// "Ana" and "ana" share nothing in the engine and everything on a scoreboard.
std::optional<QString> firstError(const Config& config);

// The slider position to the three delays. Pacing{}'s defaults are the middle
// position, so a player who touches nothing gets phase A's game.
Pacing pacingFor(int botSpeed);

// How many positions the slider has, and where the defaults sit.
inline constexpr int SlowestSpeed = 0;
inline constexpr int FastestSpeed = 4;
inline constexpr int DefaultSpeed = 2;

// THE HUMAN IS SEAT 0, or there is no human. The terminal does the same
// (TerminalRomanianWhist::buildSetup sets view.humanSeat = 0) and section 13's
// dialog implies it by asking for your name before the opponents'.
//
// C1 SEATED THE HUMAN AT 1 AND THIS REPLACES THAT. Its reason was diagnostic:
// seatlayout puts seat 0 at bottom-centre when humanSeat is -1, so a client that
// dropped the human entirely would draw a table identical to a spectator's and
// look correct. That property is now held by a test naming the seat outright
// rather than by the seat index being unusual.
std::optional<romanian_whist::Seat> humanSeatOf(const Config& config);

// The human's seat first with a NULL moveProvider - which GameController::start()
// fills, addressing it by the index humanSeatOf() returns. The two are written
// beside each other because they must agree: start() indexes with .at(), so a
// disagreement is a std::out_of_range on the GUI thread rather than a misdeal.
//
// A FIXED DEAL SEEDS THE RANDOM BOTS TOO. GameSetup::shuffleSeed reaches the
// shuffle and nothing else - the engine's own header says a fully reproducible
// game additionally requires seeding every provider that draws - and
// RandomCardStrategy has a seeded constructor that seeds its mt19937 once. So a
// seeded config builds RandomCardStrategy(seed + index) and an unseeded one the
// default. (The engine's STRATEGIES.md claims it "seeds a fresh
// std::random_device on every call", which its own source contradicts.)
romanian_whist::GameSetup buildSetup(const Config& config);

// The combo's rows and their tooltips, from the engine's STRATEGIES.md lead
// table. Free functions rather than a table in the widget so the test can read
// them without a window.
QString nameOf(BotLevel level);
QString tooltipFor(BotLevel level);

} // namespace newgame

// The widget. Built in code rather than in a .ui, for the reason
// MainWindow::buildViewMenu() gives about its actions and one more that is
// specific to this dialog: the opponent rows APPEAR AND DISAPPEAR WITH THE
// COUNT, so half of it does not exist until it is asked for.
class NewGameDialog : public QDialog
{
    Q_OBJECT

public:
    // Opened on a Config so Play again pre-fills with the last game's settings
    // (section 16.2). Held by MainWindow rather than in QSettings, which is
    // section 20 and phase E.
    explicit NewGameDialog(const newgame::Config& initial, QWidget* parent = nullptr);

    // What was asked for. The CALLER runs buildSetup() on it: GameSetup is
    // move-only and spent by the move into start(), so play again re-runs the
    // build rather than reusing a stashed copy (section 13).
    newgame::Config config() const;

private:
    // Adds and removes opponent rows to match the spinner, keeping the names and
    // levels already typed into the rows that survive.
    void rebuildOpponentRows();

    // THE BOUNDS THE SPINNER TAKES, in one place because they are set twice: once
    // when the dialog is built and again whenever the radio moves. Written out
    // twice, they were set correctly on the second path only - see the body.
    void applyOpponentRange(bool playing);

    // Reads every control into `current`, then re-runs the two things that
    // depend on all of it: the summary line and whether Deal is enabled.
    void refresh();

    // One opponent. Held as a struct rather than as parallel vectors of widgets
    // because a row is added and removed as a unit.
    struct Row
    {
        QLabel* number = nullptr;
        QLineEdit* name = nullptr;
        QComboBox* level = nullptr;
    };

    newgame::Config current;

    // WHAT THE DIALOG WAS OPENED ON, kept whole and never written to. `current`
    // cannot serve: refresh() rebuilds its bot lists FROM the rows on every
    // keystroke, so by the time a row is added the config that would have filled
    // it has already been overwritten by the rows that exist.
    //
    // Read only by rebuildOpponentRows(), and that is the whole of its job: a
    // player who re-opens the dialog on the last game's settings gets that
    // game's opponents back, which is what "play again pre-filled with the same
    // settings" (section 16.2) has to mean if it means anything.
    newgame::Config initial;

    // TWO GROUPS, DECLARED RATHER THAN INFERRED. QRadioButton auto-groups with
    // every sibling sharing its PARENT, and all four radios below are children
    // of the dialog - so left to Qt, choosing a game shape unchecks "I'm
    // playing". Nothing about that looks wrong on screen; see the constructor.
    QButtonGroup* whoGroup = nullptr;
    QButtonGroup* structureGroup = nullptr;

    QRadioButton* watching = nullptr;
    QRadioButton* playing = nullptr;
    QLabel* humanNameLabel = nullptr;
    QLineEdit* humanName = nullptr;
    QSpinBox* opponents = nullptr;
    QLabel* opponentsHint = nullptr;
    QFormLayout* opponentRows = nullptr;
    std::vector<Row> rows;

    QRadioButton* structure181 = nullptr;
    QRadioButton* structure818 = nullptr;
    QCheckBox* foreheadAndHidden = nullptr;
    QCheckBox* allOnesForehead = nullptr;
    QLabel* summary = nullptr;

    QSlider* speed = nullptr;
    QCheckBox* waitEachRound = nullptr;
    QCheckBox* fixedDeal = nullptr;
    QSpinBox* seed = nullptr;

    QLabel* error = nullptr;
    QPushButton* deal = nullptr;
};

#endif
