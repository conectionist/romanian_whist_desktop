#include <catch2/catch_test_macros.hpp>

#include "MainWindow.h"
#include "TableFixtures.h"
#include "WindowDriving.h"
#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "table/BidBubbleItem.h"
#include "table/BidStripItem.h"
#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/TableScene.h"
#include "table/TableView.h"
#include "table/TrickPanelItem.h"
#include "paint/Avatars.h"
#include "paint/Theme.h"
#include "ui/GameOverPanel.h"
#include "ui/HistorySheet.h"
#include "ui/LanguageDialog.h"
#include "ui/LastTrickSheet.h"
#include "ui/NewGameDialog.h"
#include "ui/PauseOverlay.h"
#include "ui/RoundSheet.h"
#include "ui/RulesSheet.h"
#include "ui/SettingsSheet.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QGraphicsItem>
#include <QGraphicsSceneHoverEvent>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSize>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QTranslator>

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// Screenshots of the real window, for comparing the client against the table
// redesign's mock-up (docs/ui-redesign/screenshots/). A TOOL, NOT A TEST: the
// tag is hidden, so neither a bare run nor ctest picks it up and the suite's
// count does not move. Run it by tag:
//
//     WHIST_SHOTS_DIR=/some/dir ./build/tests/desktop_tests "[.screenshot]"
//
// Without the variable, or with it empty, the pictures go to shots/ under the
// current directory, which .gitignore keeps out of a commit.
//
// IN THE TEST BINARY because that is where everything it needs already is: the
// offscreen QApplication TestApp.cpp builds, and the card art, which a separate
// target would have to compile from cards.qrc a third time.
//
// THE SAME STATE ON EVERY RUN, which is what makes a before and an after
// comparable. Every setting the dialog offers is pinned and read back, the deal
// is fixed, every bot plays a strategy that draws nothing, and the human answers
// by a fixed rule - so the n-th decision the human is asked for is always the
// same moment of the same game. A shot is reached by that count rather than by
// the round and trick it lands on, because the prompts are what the redesign
// promises to keep and the status bar is not.
//
// AND THE MOMENT IS ASSERTED, NOT PROMISED. The count is only as good as the
// assumptions under it - an engine that stops prompting for a forced card, a
// strip that hides late - and any of those would move every picture with nothing
// failing. So each capture states the moment it expects and the scene's own
// snapshot is checked against it, after the picture is saved.
namespace
{
using driving::answerModalStrictly;
using driving::clickBid;
using driving::DialogAnswer;
using driving::need;
using driving::pump;
using driving::pumpUntil;

// What a picture is expected to show, read back off the scene's snapshot.
struct Moment
{
    unsigned int round;     // 1-based
    unsigned int trick;     // 1-based; 0 while the round is still bidding
    std::size_t onTable;    // cards already played to this trick
    std::size_t inHand;     // the human's
};

struct Capture
{
    const char* file;

    // How many decisions the human makes before the picture is taken: 0 is the
    // first bid of round 1. Bids and cards both count.
    int decisions;

    Moment expected;
};

// ONE GAME PER SEAT COUNT, and each game takes every picture it has a moment
// for: the bidding shot and the 6-player table are the same seeded game, so it
// is dealt and played once. Captures run in decision order.
struct Game
{
    int players;
    std::vector<Capture> captures;
};

// Picked by running this and looking. Each table shot is the human's turn
// mid-game with every opponent's card already down, so every seat's played card
// has a position to compare (phase 6). That is the same kind of moment as the
// mock-up's table-4-players.jpg, which also has all three opponents' cards down
// and the human to play - BUT NOT THE SAME MOMENT: the mock-up is round 9, trick
// 3 of 6, and the 4-player shot here is round 8, trick 2 of 5. The bidding shot
// is round 1's opening bid, as in bidding.jpg.
//
// THE ROUND COUNT DIFFERS TOO, and is meant to: the client deals 26 rounds at
// four seats where the mock-up says 24, because the mock-up leaves out the two
// appended rounds - see roundCount() in NewGameDialog.h. shotConfig() keeps them.
const std::vector<Game> games = {
    { 3, { { "table-3-players.png", 26, { 8, 2, 2, 5 } } } },
    { 4, { { "table-4-players.png", 22, { 8, 2, 3, 4 } } } },
    { 5, { { "table-5-players.png", 24, { 9, 2, 4, 4 } } } },
    { 6, { { "bidding.png",          0, { 1, 0, 0, 1 } },
           { "table-6-players.png", 26, { 10, 2, 5, 4 } } } },
};

// EVERY SETTING THE DIALOG OFFERS, spelled out rather than taken from
// newgame::defaults(). A default that moves - the structure, the forehead
// rounds, a stock name - would otherwise move every picture with nothing
// failing, and the before and the after would be two different games.
//
// THE BOTS: LowRisk and Ducking draw nothing, which is GameFixtures.h's rule for
// a game that has to repeat. The defaults include a Reckoner, which samples -
// seeded it replays, but it is also slow.
newgame::Config shotConfig(int players)
{
    static const std::vector<PlayerProfile> opponents = { { "Ana", Gender::Female },
                                                          { "Bogdan", Gender::Male },
                                                          { "Cristina", Gender::Female },
                                                          { "Dan", Gender::Male },
                                                          { "Elena", Gender::Female } };

    newgame::Config config;
    config.spectator = false;

    // NOT THE MOCK-UP'S "You", which is a name to replace rather than a label -
    // NewGameDialog's default for the same reason.
    config.human = PlayerProfile{ "Player", Gender::Male };

    for(int i = 0 ; i < players - 1 ; i++)
    {
        config.bots.push_back(opponents.at(static_cast<std::size_t>(i)));
        config.botLevels.push_back(i % 2 == 0 ? newgame::BotLevel::LowRisk
                                              : newgame::BotLevel::Ducking);
    }

    config.structure = romanian_whist::GameStructure::S_181;
    config.endWithForeheadAndHidden = true;
    config.all1GamesAreForehead = false;
    config.shuffleSeed = 42u;
    config.botSpeed = newgame::FastestSpeed;
    config.waitAfterEachRound = false;

    return config;
}

// The fields of `got` that are not what `want` asked for, by name. A FIELD ADDED
// TO newgame::Config HAS TO BE ADDED HERE, or the dialog is free to deal it at
// whatever value it likes.
QStringList differences(const newgame::Config& want, const newgame::Config& got)
{
    QStringList wrong;

    const auto compare = [&wrong](bool same, const char* field)
    {
        if(!same)
            wrong << QString::fromUtf8(field);
    };

    compare(want.spectator == got.spectator, "spectator");
    compare(want.human == got.human, "human");
    compare(want.bots == got.bots, "bots");
    compare(want.botLevels == got.botLevels, "botLevels");
    compare(want.structure == got.structure, "structure");
    compare(want.endWithForeheadAndHidden == got.endWithForeheadAndHidden, "endWithForeheadAndHidden");
    compare(want.all1GamesAreForehead == got.all1GamesAreForehead, "all1GamesAreForehead");
    compare(want.shuffleSeed == got.shuffleSeed, "shuffleSeed");
    compare(want.botSpeed == got.botSpeed, "botSpeed");
    compare(want.waitAfterEachRound == got.waitAfterEachRound, "waitAfterEachRound");

    return wrong;
}

// Sets the dialog to `want` control by control, then reads back what it would
// deal and deals it.
//
// TWO CHECKS FOR TWO DIFFERENT FAILURES: a control that is gone, which need()
// names, and a control that is there but no longer feeds the setting it used to,
// which only the dialog's own config() can show.
//
// DEAL IS CLICKED, NOT done(Accepted), so the dialog's own gate applies. A config
// it would refuse is reported with firstError()'s reason instead of being dealt
// anyway and surfacing as a timeout at decision 0.
void fillShotDialog(NewGameDialog& dialog, DialogAnswer& answer, const newgame::Config& want)
{
    if(QRadioButton* playing = need<QRadioButton>(dialog, QStringLiteral("playing"), answer))
        playing->setChecked(true);

    if(QLineEdit* name = need<QLineEdit>(dialog, QStringLiteral("humanName"), answer))
        name->setText(QString::fromStdString(want.human.name));

    // The index is the enum's own value: Male, then Female.
    if(QComboBox* gender = need<QComboBox>(dialog, QStringLiteral("humanGender"), answer))
        gender->setCurrentIndex(static_cast<int>(want.human.gender));

    // BEFORE the rows: they follow the opponent count.
    if(QSpinBox* opponents = need<QSpinBox>(dialog, QStringLiteral("opponents"), answer))
        opponents->setValue(static_cast<int>(want.bots.size()));

    for(std::size_t i = 0 ; i < want.bots.size() ; i++)
    {
        if(QLineEdit* name = need<QLineEdit>(dialog, QStringLiteral("botName%1").arg(i), answer))
            name->setText(QString::fromStdString(want.bots.at(i).name));

        if(QComboBox* gender = need<QComboBox>(dialog, QStringLiteral("botGender%1").arg(i), answer))
            gender->setCurrentIndex(static_cast<int>(want.bots.at(i).gender));

        // The index is the enum's own value, which is what BotLevels is ordered by.
        if(QComboBox* level = need<QComboBox>(dialog, QStringLiteral("botLevel%1").arg(i), answer))
            level->setCurrentIndex(static_cast<int>(want.botLevels.at(i)));
    }

    const QString structure = want.structure == romanian_whist::GameStructure::S_181
                                  ? QStringLiteral("structure181")
                                  : QStringLiteral("structure818");

    if(QRadioButton* button = need<QRadioButton>(dialog, structure, answer))
        button->setChecked(true);

    if(QCheckBox* box = need<QCheckBox>(dialog, QStringLiteral("foreheadAndHidden"), answer))
        box->setChecked(want.endWithForeheadAndHidden);

    if(QCheckBox* box = need<QCheckBox>(dialog, QStringLiteral("allOnesForehead"), answer))
        box->setChecked(want.all1GamesAreForehead);

    if(QSlider* speed = need<QSlider>(dialog, QStringLiteral("speed"), answer))
        speed->setValue(want.botSpeed);

    if(QCheckBox* wait = need<QCheckBox>(dialog, QStringLiteral("waitEachRound"), answer))
        wait->setChecked(want.waitAfterEachRound);

    if(QCheckBox* fixedDeal = need<QCheckBox>(dialog, QStringLiteral("fixedDeal"), answer))
        fixedDeal->setChecked(want.shuffleSeed.has_value());

    if(QSpinBox* seed = need<QSpinBox>(dialog, QStringLiteral("seed"), answer); seed && want.shuffleSeed)
        seed->setValue(static_cast<int>(*want.shuffleSeed));

    for(const QString& field : differences(want, dialog.config()))
        answer.problems << QStringLiteral("the dialog would deal a different %1").arg(field);

    QPushButton* deal = need<QPushButton>(dialog, QStringLiteral("deal"), answer);

    if(!deal || !answer.problems.isEmpty())
    {
        dialog.reject();
        return;
    }

    if(!deal->isEnabled())
    {
        answer.problems << QStringLiteral("Deal is disabled: %1")
                               .arg(newgame::firstError(dialog.config()).value_or(QStringLiteral("no reason given")));
        dialog.reject();
        return;
    }

    deal->click();
}

// The first slot a click would play, or nothing when the hand is not asking.
std::optional<std::size_t> firstPlayableSlot(const HandItem& hand)
{
    for(std::size_t slot = 0 ; slot < hand.slotCount() ; slot++)
    {
        if(hand.isPlayable(slot))
            return slot;
    }

    return std::nullopt;
}

// Clicks the lowest bid the strip offers, through the scene as a player clicks it.
// Walked up from 0: the strip offers every bid up to the hand size and STRIKES OUT
// the forbidden one rather than leaving it out, so the first one it offers is the
// lowest legal bid.
bool clickLowestBid(BidStripItem& strip)
{
    for(unsigned int bet = 0 ; bet <= strip.fields().highest ; bet++)
    {
        if(bidstrip::offers(strip.fields(), bet))
            return clickBid(strip, bet);
    }

    return false;
}

// The moment, as words, for the line beside each picture and for every failure.
std::string describe(const std::shared_ptr<const GameSnapshot>& s)
{
    if(!s)
        return "no frame yet";

    return "round " + std::to_string(s->roundNumber) + " of " + std::to_string(s->roundCount)
         + ", trick " + std::to_string(s->trickNumber) + " of " + std::to_string(s->trickCount)
         + ", " + std::to_string(s->table.size()) + " on the table, "
         + std::to_string(s->hand.size()) + " in hand";
}

void capture(MainWindow& window, const TableScene& scene, const Capture& shot, const QString& dir)
{
    // The engine is parked on the prompt, so nothing moves while the last frame
    // settles.
    pump(200);

    const std::shared_ptr<const GameSnapshot> s = scene.lastSnapshot();
    REQUIRE(s != nullptr);

    // NO BID BUBBLES (phase 12). The game is played at the bots' top speed, so this
    // round's bids are a few milliseconds old and still being said - 1.5 s even with
    // motion off. A player at that speed sees them too; the pictures compare against
    // the mock-up's, which show the table between bids.
    for(std::size_t seat = 0 ; seat < s->seats.size() ; seat++)
    {
        if(BidBubbleItem* bubble = scene.bidBubble(seat))
            bubble->dismiss();
    }

    const QPixmap picture = window.grab();
    const QString path = QDir(dir).filePath(QString::fromUtf8(shot.file));
    REQUIRE(picture.save(path));

    std::cout << path.toStdString() << ": " << describe(s) << '\n';

    // AFTER THE SAVE, every check below: a picture of the wrong moment, or at the
    // wrong size, is still the thing to look at when working out why.
    INFO(shot.file << " shows " << describe(s));
    CHECK(s->roundNumber == shot.expected.round);
    CHECK(s->trickNumber == shot.expected.trick);
    CHECK(s->table.size() == shot.expected.onTable);
    CHECK(s->hand.size() == shot.expected.inHand);
    CHECK(s->activeSeat == s->humanSeat);

    // LOGICAL pixels. grab() returns device pixels, so at a device pixel ratio of
    // 2 a correct window is a 3840x2160 picture. What this is for is a window the
    // window manager clamped, which is not comparable and is reported here with
    // the size it got.
    const QSize logical = picture.deviceIndependentSize().toSize();
    INFO("logical size " << logical.width() << "x" << logical.height());
    CHECK(logical == QSize(1920, 1080));
}
}

namespace
{
// Where the pictures go: WHIST_SHOTS_DIR, or shots/ under the current directory.
//
// EMPTY COUNTS AS UNSET. qEnvironmentVariable()'s default applies only to a
// variable that is not there at all, so `WHIST_SHOTS_DIR= ...` would hand mkpath()
// an empty string.
QString shotsDirectory()
{
    return qEnvironmentVariableIsEmpty("WHIST_SHOTS_DIR") ? QStringLiteral("shots")
                                                          : qEnvironmentVariable("WHIST_SHOTS_DIR");
}
}

namespace
{
// WHIST_SHOTS_QM: a compiled translation to take the pictures in (phase 12), such as
// the build's romanian_whist_desktop_ro_RO.qm. Unset, the pictures are in the source
// language.
//
// FOR ONE TEST CASE, declared first in it so it outlives every window the case
// builds: installed as it is made, before the first window, and taken out again as
// it goes. Left in for the rest of the run, it turned every case run after the
// pictures - `"[.screenshot],[bubble]"` - into Romanian, and those assert English
// (PR #38's review, finding #5).
class ShotsTranslation
{
public:
    ShotsTranslation()
    {
        if(qEnvironmentVariableIsEmpty("WHIST_SHOTS_QM"))
            return;

        INFO("WHIST_SHOTS_QM=" << qEnvironmentVariable("WHIST_SHOTS_QM").toStdString());
        REQUIRE(translator.load(qEnvironmentVariable("WHIST_SHOTS_QM")));

        installed = QCoreApplication::installTranslator(&translator);
        REQUIRE(installed);
    }

    ~ShotsTranslation()
    {
        if(installed)
            QCoreApplication::removeTranslator(&translator);
    }

    ShotsTranslation(const ShotsTranslation&) = delete;
    ShotsTranslation& operator=(const ShotsTranslation&) = delete;

private:
    QTranslator translator;
    bool installed = false;
};
}

TEST_CASE("Screenshots of the table for the redesign", "[.screenshot]")
{
    const ShotsTranslation translation;

    const QString dir = shotsDirectory();
    REQUIRE(QDir().mkpath(dir));

    for(const Game& game : games)
    {
        DYNAMIC_SECTION(game.players << " players")
        {
            for(std::size_t i = 1 ; i < game.captures.size() ; i++)
                REQUIRE(game.captures[i - 1].decisions < game.captures[i].decisions);

            MainWindow window;
            window.resize(1920, 1080);
            window.show();
            pump(200);

            QPushButton* newGame = window.findChild<QPushButton*>("newGame");
            REQUIRE(newGame != nullptr);

            const newgame::Config want = shotConfig(game.players);

            QTimer poll;
            DialogAnswer answer;

            answerModalStrictly<NewGameDialog>(poll, answer,
                                               [&want](NewGameDialog& dialog, DialogAnswer& found)
                                               { fillShotDialog(dialog, found, want); });
            newGame->click();
            poll.stop();

            {
                INFO("the setup dialog: " << answer.problems.join(QStringLiteral("; ")).toStdString());
                REQUIRE(answer.found);
                REQUIRE(answer.problems.isEmpty());
            }

            TableScene* scene = window.findChild<TableScene*>();
            REQUIRE(scene != nullptr);

            // EACH PICTURE IS A SETTLED MOMENT: with the cards' motion off, the frame
            // the game published is the frame shot, not a card half way to the felt. The
            // window's own pace (phase 12's Card animations) is left as it ships.
            scene->setMotion(motion::Pace::Off);

            BidStripItem* bidStrip = scene->bidStripItem();
            REQUIRE(bidStrip != nullptr);

            HandItem* hand = scene->handItem();
            REQUIRE(hand != nullptr);

            std::size_t next = 0;

            for(int decision = 0 ; ; decision++)
            {
                const bool asked = pumpUntil(20000, [&]
                {
                    return bidStrip->isVisible() || firstPlayableSlot(*hand).has_value();
                });

                // A target past the end of the game lands here, with the last
                // moment the table showed, rather than hanging.
                {
                    INFO("decision " << decision << ", " << describe(scene->lastSnapshot()));
                    REQUIRE(asked);
                }

                if(decision == game.captures[next].decisions)
                {
                    capture(window, *scene, game.captures[next], dir);

                    if(++next == game.captures.size())
                        break;
                }

                // ONE ANSWER, ONE DECISION - and that holds only if the prompt
                // answered actually goes away before the next one is looked for.
                // Today both go inside the click; a strip that animated out, or a
                // card the window refused, would leave the same prompt up to be
                // counted twice, and every later picture would land early.
                bool taken = false;

                if(bidStrip->isVisible())
                {
                    REQUIRE(clickLowestBid(*bidStrip));
                    taken = pumpUntil(5000, [&]{ return !bidStrip->isVisible(); });
                }
                else
                {
                    hand->activate(*firstPlayableSlot(*hand));
                    taken = pumpUntil(5000, [&]{ return !firstPlayableSlot(*hand).has_value(); });
                }

                INFO("decision " << decision << " was not taken: " << describe(scene->lastSnapshot()));
                REQUIRE(taken);
            }

            REQUIRE(window.close());
        }
    }
}

// THE TABLE AND NOTHING ELSE, AT THE MOCK-UP'S OWN SCALE, for the redesign's
// phase 3: table-empty.png, to set beside screenshots/table-4-players.jpg below
// its 46-pixel menu bar.
//
// A BARE VIEW, NOT THE WINDOW. The window's menu bar is not the mock-up's 46
// pixels until phase 9, so its table is drawn at a slightly different scale and
// lies a little higher. A view of 1920 x 1034 is the
// mock-up's viewport less its menu bar: inside the margins both leave 1888 x 1016,
// so the two tables are drawn at one scale in one place, and the rim, the bead,
// the gold circle and the logo can be compared pixel for pixel.
TEST_CASE("A screenshot of the empty table at the mock-up's own scale", "[.screenshot]")
{
    const ShotsTranslation translation;

    const QString dir = shotsDirectory();
    REQUIRE(QDir().mkpath(dir));

    TableScene scene;
    scene.setTheme(Theme::dark());

    TableView view(&scene);
    view.resize(1920, 1034);
    view.show();
    pump(200);

    const QPixmap picture = view.grab();
    const QString path = QDir(dir).filePath(QStringLiteral("table-empty.png"));
    REQUIRE(picture.save(path));

    std::cout << path.toStdString() << ": the empty table\n";

    CHECK(picture.deviceIndependentSize().toSize() == QSize(1920, 1034));
}

// ---- the hand, for the redesign's phase 7 ----
//
// hand-1-card.png, hand-4-cards.png and hand-8-cards.png: a bare view at the
// mock-up's own scale, as table-empty.png is, to set beside the pictures of the same
// names in docs/ui-redesign/screenshots/ below their 46-pixel menu bar. None of the
// mock-up's own pictures shows a hand of eight, so those three were taken for this by
// docs/ui-redesign/mockup/source/capture_hands.py, at the same moment: four players,
// Bogdan on your left has led the King of Hearts, and it is your turn, with the same
// cards in the same order, those that follow suit lit and the rest dimmed - and in
// the 8-card picture the Queen of Hearts raised, as though the cursor were on it.
//
// KEPT IN STEP WITH capture_hands.py's SHOTS, card for card.
namespace
{

struct HandShot
{
    const char* file;

    // 1-based, of 24: the round of a 4-player game that deals this many cards.
    unsigned int round;
    std::optional<romanian_whist::Card> trump;

    // In the order the hand shows them, which is the mock-up's: trump first, then
    // alternating colours, high to low. Those that may follow the King of Hearts are
    // lit, the rest dimmed.
    std::vector<romanian_whist::Card> hand;
    std::vector<bool> legal;

    // The card under the cursor, if any.
    std::optional<std::size_t> raised;

    // Yours first, then clockwise.
    std::array<unsigned int, 4> bids;
};

std::vector<HandShot> handShots()
{
    using romanian_whist::Card;
    using romanian_whist::Rank;
    using romanian_whist::Suit;

    const Card sevenOfClubs(Rank::Seven, Suit::Clubs);

    return {
        { "hand-1-card.png", 1, sevenOfClubs,
          { Card(Rank::Jack, Suit::Diamonds) },
          { true }, std::nullopt, { 0, 1, 0, 0 } },
        { "hand-4-cards.png", 7, sevenOfClubs,
          { Card(Rank::Nine, Suit::Clubs), Card(Rank::Ace, Suit::Spades),
            Card(Rank::Ten, Suit::Hearts), Card(Rank::Eight, Suit::Hearts) },
          { false, false, true, true }, std::nullopt, { 1, 2, 0, 1 } },
        { "hand-8-cards.png", 11, std::nullopt,
          { Card(Rank::Ace, Suit::Spades), Card(Rank::Jack, Suit::Spades), Card(Rank::Eight, Suit::Spades),
            Card(Rank::Queen, Suit::Hearts), Card(Rank::Nine, Suit::Hearts), Card(Rank::Seven, Suit::Hearts),
            Card(Rank::Ten, Suit::Clubs), Card(Rank::Queen, Suit::Diamonds) },
          { false, false, false, true, true, true, false, false }, 3, { 2, 3, 1, 1 } },
    };
}

// The moment the picture shows, as the snapshot the scene draws from: the mock-up's
// four names, nobody with a trick yet, every bid in, and seat 1's King on the felt.
GameSnapshot handMoment(const HandShot& shot)
{
    using romanian_whist::Card;
    using romanian_whist::Rank;
    using romanian_whist::Suit;

    const unsigned int cards = static_cast<unsigned int>(shot.hand.size());
    const char* const names[] = { "You", "Bogdan", "Ana", "Costel" };

    GameSnapshot snapshot;

    snapshot.humanSeat = 0;
    snapshot.activeSeat = 0;
    snapshot.roundNumber = shot.round;
    snapshot.roundCount = 24;
    snapshot.trickCount = cards;
    snapshot.trickNumber = 1;
    snapshot.phase = romanian_whist::GamePhase::Playing;
    snapshot.hasTrump = shot.trump.has_value();
    snapshot.trump = shot.trump.value_or(Card());
    snapshot.leadSeat = 1;
    snapshot.hasLeadSuit = true;
    snapshot.leadSuit = Suit::Hearts;
    snapshot.table = { PlayedCardView{ 1, Card(Rank::King, Suit::Hearts), true } };
    snapshot.seats.resize(4);

    for(unsigned int i = 0 ; i < 4 ; i++)
    {
        SeatView& seat = snapshot.seats[i];

        seat.seatIndex = i;
        seat.name = names[i];
        seat.isHuman = i == 0;

        // THE MOCK-UP'S FACES, which go with its names: You, Bogdan, Ana and Costel
        // are the catalogue's first four, so these pictures still pair with its.
        seat.avatar = avatars::catalogue().at(i).id;
        seat.gender = avatars::catalogue().at(i).gender.value_or(Gender::Male);

        // Seat 1 bid first, as it leads. ONE-BASED, as the engine's
        // getBiddingOrder() is: the scoreboard lists the rows in this order since
        // the redesign's phase 8, and a 0 there is "not known yet".
        seat.bidOrder = (i + 3) % 4 + 1;
        seat.hasBet = true;
        seat.bet = shot.bids[i];

        // Seat 1 has played its card.
        seat.cardsLeft = i == 1 ? cards - 1 : cards;

        snapshot.betSum += shot.bids[i];
    }

    snapshot.betsPlaced = 4;

    for(const Card& card : shot.hand)
        snapshot.hand.push_back(HandEntry{ card });

    return snapshot;
}

}

TEST_CASE("Screenshots of the hand at the mock-up's own scale", "[.screenshot]")
{
    const ShotsTranslation translation;

    const QString dir = shotsDirectory();
    REQUIRE(QDir().mkpath(dir));

    for(const HandShot& shot : handShots())
    {
        DYNAMIC_SECTION(shot.file)
        {
            TableScene scene;
            scene.setTheme(Theme::dark());
            scene.setSnapshot(std::make_shared<const GameSnapshot>(handMoment(shot)));

            // YOUR TURN, as MainWindow hands it to the fan: the prompt's cards, with
            // which of them may be played.
            PlayPrompt prompt;
            prompt.promptId = 1;

            for(std::size_t i = 0 ; i < shot.hand.size() ; i++)
            {
                prompt.entries.push_back(HandEntry{ shot.hand[i], shot.legal[i] });
                prompt.handIndex.push_back(i);
            }

            HandItem* hand = scene.handItem();
            hand->setPrompt(prompt);
            hand->setPlayable(true);

            // And to the trick panel, which says why the dimmed cards are out.
            scene.trickPanelItem()->setPrompt(prompt);

            TableView view(&scene);
            view.resize(1920, 1034);
            view.show();
            pump(200);

            // THE CURSOR ON THE RAISED CARD, through the scene as a real one would
            // arrive: on its middle, which no other card and no panel covers.
            if(shot.raised)
            {
                const CardItem* raised = nullptr;

                for(QGraphicsItem* child : hand->childItems())
                {
                    const CardItem* card = dynamic_cast<const CardItem*>(child);

                    if(card && card->zValue() == static_cast<qreal>(*shot.raised))
                        raised = card;
                }

                REQUIRE(raised != nullptr);

                QGraphicsSceneHoverEvent hover(QEvent::GraphicsSceneHoverMove);
                hover.setScenePos(raised->scenePos());
                QCoreApplication::sendEvent(&scene, &hover);
                pump(100);

                CHECK(raised->isRaised());
            }

            const QPixmap picture = view.grab();
            const QString path = QDir(dir).filePath(QString::fromUtf8(shot.file));
            REQUIRE(picture.save(path));

            std::cout << path.toStdString() << ": your hand of " << shot.hand.size() << "\n";

            CHECK(picture.deviceIndependentSize().toSize() == QSize(1920, 1034));
            CHECK(hand->slotCount() == shot.hand.size());

            for(std::size_t i = 0 ; i < shot.hand.size() ; i++)
                CHECK(hand->isPlayable(i) == shot.legal[i]);
        }
    }
}

// ---- the sheets, for the redesign's phase 10 ----
//
// sheet-pause.png, sheet-round.png, sheet-game-over.png, sheet-rules.png and
// sheet-settings.png: each of the five sheets over a table, on a page of 1920 x
// 1034 - the window less its menu bar, as table-empty.png is. The mock-up has no
// pictures of its sheets, so these are looked at as they are. Phase 11 adds
// sheet-history.png and sheet-last-trick.png.
//
// A FIXTURE TABLE, NOT A GAME: a round sheet that waits and a game's end are a
// whole game away, and what is being looked at is the sheet, not the moment. The
// round has one seat on a streak of four, so the warning line shows, and the game
// ends in a tie, so two rows are starred.
namespace
{

const char* const sheetNames[] = { "Player", "Ana", "Bogdan", "Cristina", "Dan", "Elena" };

// A face of each player's own gender, as a dealt game gives them. Two women's
// faces for three women, until there is more art: Elena wears Ana's.
const PlayerProfile sheetFaces[] = { { "", Gender::Male, "man-dark-hair-beard" },
                                     { "", Gender::Female, "woman-long-brown-hair" },
                                     { "", Gender::Male, "man-hat-mustache" },
                                     { "", Gender::Female, "woman-long-red-hair" },
                                     { "", Gender::Male, "man-white-hair-hat" },
                                     { "", Gender::Female, "woman-long-brown-hair" } };

GameSnapshot sheetTable()
{
    GameSnapshot snapshot = fullTable();

    for(std::size_t i = 0 ; i < snapshot.seats.size() ; i++)
    {
        snapshot.seats[i].name = sheetNames[i];
        snapshot.seats[i].gender = sheetFaces[i].gender;
        snapshot.seats[i].avatar = sheetFaces[i].avatar;
    }

    return snapshot;
}

RoundResult sheetRound()
{
    RoundResult result;
    result.roundNumber = 10;
    result.roundCount = 32;
    result.trickCount = 5;
    result.roundType = romanian_whist::RoundType::Normal;
    result.hasTrump = true;
    result.trump = romanian_whist::Card(romanian_whist::Rank::King, romanian_whist::Suit::Hearts);
    result.nextRoundCountsForStreaks = true;

    const int bets[] = { 1, 2, 0, 1, 2, 0 };
    const int taken[] = { 1, 1, 0, 2, 1, 0 };
    const int totals[] = { 34, 41, 28, 19, 37, 30 };

    for(std::size_t i = 0 ; i < 6 ; i++)
    {
        RoundSeatResult seat;
        seat.name = sheetNames[i];
        seat.seatIndex = static_cast<unsigned int>(i);
        seat.isHuman = i == 0;
        seat.hasBet = true;
        seat.bet = static_cast<unsigned int>(bets[i]);
        seat.tricksWon = static_cast<unsigned int>(taken[i]);
        seat.roundScore = bets[i] == taken[i] ? 5 + bets[i] : -std::abs(bets[i] - taken[i]);
        seat.totalScore = totals[i];
        seat.winStreak = i == 2 ? 4 : 0;
        seat.lossStreak = i == 3 ? 2 : 0;
        result.seats.push_back(seat);
    }

    return result;
}

// TWELVE ROUNDS OF THE SIX-SEAT SCHEDULE: six of one card, then two to seven. You,
// Ana and Bogdan hit all five rounds of two to six cards, and the engine's ten for
// it lands in the round of six, a +16 or +17; one-card rounds count for no streak
// (RULES.md).
std::vector<RoundResult> sheetHistory()
{
    const unsigned int cards[] = { 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7 };

    // Per round, per seat: the bid and the tricks taken.
    const int bets[12][6] = { { 0, 1, 0, 0, 1, 0 }, { 1, 0, 0, 1, 0, 0 }, { 0, 0, 1, 0, 0, 1 },
                              { 1, 1, 0, 0, 0, 0 }, { 0, 0, 0, 1, 1, 0 }, { 0, 1, 1, 0, 0, 0 },
                              { 1, 1, 0, 0, 1, 0 }, { 0, 1, 1, 0, 1, 1 }, { 1, 2, 0, 1, 1, 0 },
                              { 1, 1, 2, 0, 2, 1 }, { 2, 2, 1, 1, 1, 0 }, { 2, 1, 2, 1, 2, 1 } };
    const int taken[12][6] = { { 0, 1, 0, 0, 0, 0 }, { 1, 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0, 1 },
                               { 0, 1, 0, 0, 0, 0 }, { 0, 0, 0, 1, 0, 0 }, { 0, 1, 0, 0, 0, 0 },
                               { 1, 1, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 1 }, { 1, 2, 0, 1, 0, 0 },
                               { 1, 1, 2, 0, 1, 0 }, { 2, 2, 1, 0, 1, 0 }, { 2, 1, 1, 1, 1, 1 } };

    std::vector<RoundResult> rounds;
    int totals[6] = {};
    int hits[6] = {};
    int misses[6] = {};

    for(unsigned int r = 0 ; r < 12 ; r++)
    {
        RoundResult round;
        round.roundNumber = r + 1;
        round.roundCount = 32;
        round.trickCount = cards[r];

        for(std::size_t i = 0 ; i < 6 ; i++)
        {
            const int bet = bets[r][i];
            const int won = taken[r][i];
            const bool exact = bet == won;

            int worth = exact ? 5 + bet : -std::abs(bet - won);

            // The engine's streaks: rounds of one card count for neither.
            if(cards[r] > 1)
            {
                hits[i] = exact ? hits[i] + 1 : 0;
                misses[i] = exact ? 0 : misses[i] + 1;

                if(hits[i] == 5)
                    worth += 10;

                if(misses[i] == 5)
                    worth -= 10;
            }

            RoundSeatResult seat;
            seat.name = sheetNames[i];
            seat.seatIndex = static_cast<unsigned int>(i);
            seat.isHuman = i == 0;
            seat.hasBet = true;
            seat.bet = static_cast<unsigned int>(bet);
            seat.tricksWon = static_cast<unsigned int>(won);
            seat.roundScore = worth;
            seat.totalScore = totals[i];
            round.seats.push_back(seat);

            totals[i] += worth;
        }

        rounds.push_back(round);
    }

    return rounds;
}

// A FINISHED TRICK OF HEARTS led by Bogdan, which Dan took with the Ace from the
// middle of the row.
lasttrick::Trick sheetTrick()
{
    using romanian_whist::Card;
    using romanian_whist::Rank;
    using romanian_whist::Suit;

    lasttrick::Trick trick;
    trick.roundNumber = 10;
    trick.humanSeat = 0;

    const std::pair<unsigned int, Card> played[] = {
        { 2, Card(Rank::Nine, Suit::Hearts) },
        { 3, Card(Rank::King, Suit::Hearts) },
        { 4, Card(Rank::Ace, Suit::Hearts) },
        { 5, Card(Rank::Seven, Suit::Hearts) },
        { 0, Card(Rank::Ten, Suit::Hearts) },
        { 1, Card(Rank::Queen, Suit::Spades) }
    };

    for(const auto& [seat, card] : played)
        trick.cards.push_back({ card, seat, QString::fromUtf8(sheetNames[seat]), seat == 4 });

    return trick;
}

}

TEST_CASE("Screenshots of the sheets", "[.screenshot]")
{
    const ShotsTranslation translation;

    const QString dir = shotsDirectory();
    REQUIRE(QDir().mkpath(dir));

    // THE SCENE FIRST, so that its card art outlives the last trick's sheet, which
    // draws from it and which the page deletes (PR #37's review, finding #9).
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(sheetTable()));

    QWidget page;
    page.resize(1920, 1034);

    TableView view(&scene, &page);
    view.setGeometry(page.rect());

    PauseOverlay* pause = new PauseOverlay(&page);

    RoundSheet* round = new RoundSheet(&page);
    round->showResult(sheetRound());

    GameOverPanel* over = new GameOverPanel(&page);
    std::vector<romanian_whist::Standing> standings;
    const int finals[] = { 63, 63, 51, 47, 40, 22 };
    const unsigned int places[] = { 1, 1, 3, 4, 5, 6 };
    const std::size_t order[] = { 1, 4, 0, 5, 2, 3 };

    for(std::size_t i = 0 ; i < 6 ; i++)
        standings.push_back({ romanian_whist::Seat{ static_cast<unsigned int>(order[i]) },
                              sheetNames[order[i]], finals[i], places[i] });

    over->showResult(standings, { standings[0], standings[1] });

    RulesSheet* rules = new RulesSheet(&page);
    rules->showRules(shotConfig(6));

    SettingsSheet* settings = new SettingsSheet(&page);
    settings->setValues(newgame::DefaultSpeed, true, motion::defaultPace);

    // Phase 11's two: the game's first twelve rounds, and a trick won mid-row.
    HistorySheet* history = new HistorySheet(&page);
    std::vector<QString> names;

    for(const char* name : sheetNames)
        names.push_back(QString::fromUtf8(name));

    history->showHistory(sheetHistory(), names);

    LastTrickSheet* last = new LastTrickSheet(&page);
    last->setCards(&scene.cardRenderer());
    last->showTrick(sheetTrick(), 10);

    const std::vector<std::pair<const char*, Overlay*>> shots = {
        { "sheet-pause.png", pause },
        { "sheet-round.png", round },
        { "sheet-game-over.png", over },
        { "sheet-rules.png", rules },
        { "sheet-settings.png", settings },
        { "sheet-history.png", history },
        { "sheet-last-trick.png", last }
    };

    for(const auto& [file, sheet] : shots)
        sheet->hide();

    page.show();
    pump(200);

    for(const auto& [file, sheet] : shots)
    {
        sheet->setTheme(Theme::dark());
        sheet->show();
        sheet->raise();
        pump(100);

        const QPixmap picture = page.grab();
        const QString path = QDir(dir).filePath(QString::fromUtf8(file));
        REQUIRE(picture.save(path));

        std::cout << path.toStdString() << '\n';

        CHECK(picture.deviceIndependentSize().toSize() == QSize(1920, 1034));

        sheet->hide();
    }
}

// menu.png: the page the client opens on, in the real window at 1920 x 1080 - the
// room, the logo plaque and its two buttons, under the menu bar. No namesake in the
// mock-up, which has no menu page.
TEST_CASE("A screenshot of the main menu page", "[.screenshot]")
{
    const ShotsTranslation translation;

    const QString dir = shotsDirectory();
    REQUIRE(QDir().mkpath(dir));

    MainWindow window;
    window.resize(1920, 1080);
    window.show();
    pump(200);

    const QPixmap picture = window.grab();
    const QString path = QDir(dir).filePath(QStringLiteral("menu.png"));
    REQUIRE(picture.save(path));

    std::cout << path.toStdString() << ": the main menu page\n";

    CHECK(picture.deviceIndependentSize().toSize() == QSize(1920, 1080));
}

// ---- the language dialog, for the redesign's phase 13 ----
//
// language.png: the dialog that asks for the language before the window is built,
// at its own size, Romanian lit to start (lit only in an active window, so
// not here: the offscreen platform never activates one).
TEST_CASE("A screenshot of the language dialog", "[.screenshot]")
{
    const QString dir = shotsDirectory();
    REQUIRE(QDir().mkpath(dir));

    LanguageDialog dialog;
    dialog.show();
    pump(200);

    const QPixmap picture = dialog.grab();
    const QString path = QDir(dir).filePath(QStringLiteral("language.png"));
    REQUIRE(picture.save(path));

    std::cout << path.toStdString() << ": the language dialog\n";

    CHECK(picture.deviceIndependentSize().toSize() == dialog.size());
}
