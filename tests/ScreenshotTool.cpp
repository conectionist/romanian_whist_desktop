#include <catch2/catch_test_macros.hpp>

#include "MainWindow.h"
#include "WindowDriving.h"
#include "core/GameSnapshot.h"
#include "table/HandItem.h"
#include "table/TableScene.h"
#include "ui/BidStrip.h"
#include "ui/NewGameDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
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

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
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
using driving::bidButton;
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
    static const QStringList names = { QStringLiteral("Ana"),
                                       QStringLiteral("Bogdan"),
                                       QStringLiteral("Cristina"),
                                       QStringLiteral("Dan"),
                                       QStringLiteral("Elena") };

    newgame::Config config;
    config.spectator = false;

    // NOT THE MOCK-UP'S "You": the client already marks the human's plaque
    // "(you)", and "You (you)" is a picture of the name rather than the table.
    config.humanName = QStringLiteral("Player");

    for(int i = 0 ; i < players - 1 ; i++)
    {
        config.botNames.push_back(names.at(i));
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
    compare(want.humanName == got.humanName, "humanName");
    compare(want.botNames == got.botNames, "botNames");
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
        name->setText(want.humanName);

    // BEFORE the rows: they follow the opponent count.
    if(QSpinBox* opponents = need<QSpinBox>(dialog, QStringLiteral("opponents"), answer))
        opponents->setValue(static_cast<int>(want.botNames.size()));

    for(qsizetype i = 0 ; i < want.botNames.size() ; i++)
    {
        if(QLineEdit* name = need<QLineEdit>(dialog, QStringLiteral("botName%1").arg(i), answer))
            name->setText(want.botNames.at(i));

        // The index is the enum's own value, which is what BotLevels is ordered by.
        if(QComboBox* level = need<QComboBox>(dialog, QStringLiteral("botLevel%1").arg(i), answer))
            level->setCurrentIndex(static_cast<int>(want.botLevels.at(static_cast<std::size_t>(i))));
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

// Clicks the lowest bid the strip offers. Walked up from 0 until a bid has no
// button: the strip offers every bid up to the hand size and DISABLES the
// forbidden one rather than leaving it out, so the first enabled button is the
// lowest legal bid.
bool clickLowestBid(const BidStrip& strip)
{
    for(unsigned int bet = 0 ; QPushButton* button = bidButton(strip, bet) ; bet++)
    {
        if(button->isEnabled())
        {
            button->click();
            return true;
        }
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

TEST_CASE("Screenshots of the table for the redesign", "[.screenshot]")
{
    // EMPTY COUNTS AS UNSET. qEnvironmentVariable()'s default applies only to a
    // variable that is not there at all, so `WHIST_SHOTS_DIR= ...` would hand
    // mkpath() an empty string.
    const QString dir = qEnvironmentVariableIsEmpty("WHIST_SHOTS_DIR")
                            ? QStringLiteral("shots")
                            : qEnvironmentVariable("WHIST_SHOTS_DIR");
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

            BidStrip* bidStrip = window.findChild<BidStrip*>();
            TableScene* scene = window.findChild<TableScene*>();
            REQUIRE(bidStrip != nullptr);
            REQUIRE(scene != nullptr);

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
