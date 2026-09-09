#include <catch2/catch_test_macros.hpp>

#include "GameFixtures.h"

#include "ui/NewGameDialog.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/RoundType.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>

#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace romanian_whist;

// The split BidStrip.h argues for and InfoStrip follows, and the reason D1 needs
// it more than either: the dialog's answers reach GameEngine::start(), which
// throws five different ways by name, and every one of those is a crash on the
// GUI thread rather than a message. The part with a right answer is tested here
// without a window, and the window is tested after it.
namespace
{
newgame::Config playingConfig(int opponents = 3)
{
    newgame::Config config;
    config.humanName = "Dan";

    static const char* names[] = { "Ana", "Bogdan", "Cristina", "Elena", "Florin" };

    for(int i = 0 ; i < opponents ; i++)
    {
        config.botNames.push_back(names[i]);
        config.botLevels.push_back(newgame::BotLevel::LowRisk);
    }

    return config;
}

newgame::Config watchingConfig(int bots = 4)
{
    newgame::Config config = playingConfig(bots > 5 ? 5 : bots);
    config.spectator = true;
    return config;
}

// Fills the human's seat the way GameController::start() does, so a setup built
// for a player can be handed to a real engine. Not a SeatProvider, because there
// is no QtMoveProvider here to forward to - only the engine's null check is
// under test.
class NullProvider : public IMoveProvider
{
public:
    unsigned int makeBet(const BetContext&) override { return 0; }
    std::optional<std::size_t> playCard(const PlayContext&) override { return 0; }
};
}

TEST_CASE("The round count is the engine's own schedule", "[new-game]")
{
    // 3N + 12, plus the appended forehead and hidden pair - and the assertion
    // that matters is not the arithmetic but that it AGREES WITH THE ENGINE,
    // which is why every case below builds a real game and asks it.
    //
    // SECTION 13's MOCKUP SAYS 24 for the four-seat game with the box ticked,
    // and this is what settles that it is 26: the engine is the authority and
    // the mockup is stale.
    for(int opponents = 1 ; opponents <= 5 ; opponents++)
    {
        for(const bool appended : { true, false })
        {
            newgame::Config config = playingConfig(opponents);
            config.endWithForeheadAndHidden = appended;

            GameSetup setup = newgame::buildSetup(config);
            setup.seats.at(0).moveProvider = std::make_unique<NullProvider>();

            GameEngine engine;
            engine.start(std::move(setup));

            INFO("opponents " << opponents << ", appended " << appended);
            REQUIRE(newgame::roundCount(config) == engine.getRoundCount());
        }
    }
}

TEST_CASE("The structure and the forehead rule do not change the round count", "[new-game]")
{
    // The counterweight to the case above, and the reason the formula names
    // neither: S_818 is the same schedule read backwards, and
    // all1GamesAreForehead retypes rounds rather than adding any. A roundCount()
    // that consulted either would pass every case above and fail here.
    newgame::Config config = playingConfig();

    const unsigned int base = newgame::roundCount(config);

    config.structure = GameStructure::S_818;
    REQUIRE(newgame::roundCount(config) == base);

    config.all1GamesAreForehead = true;
    REQUIRE(newgame::roundCount(config) == base);

    // And the one flag that DOES change it, so this case cannot pass by the
    // function ignoring its argument altogether.
    config.endWithForeheadAndHidden = false;
    REQUIRE(newgame::roundCount(config) == base - 2);
}

TEST_CASE("A watched game counts no seat for the human", "[new-game]")
{
    newgame::Config config = watchingConfig(4);

    REQUIRE(newgame::seatCount(config) == 4);
    REQUIRE(newgame::roundCount(config) == 3 * 4 + 12 + 2);

    config.spectator = false;
    REQUIRE(newgame::seatCount(config) == 5);
}

TEST_CASE("The human is seat 0, and only when there is one", "[new-game]")
{
    // C1 SEATED THE HUMAN AT 1 AND D1 MOVED THEM TO 0, which took away the
    // diagnostic that seat choice was carrying: seatlayout puts seat 0 at
    // bottom-centre when humanSeat is -1, so a client that dropped the human
    // entirely now draws the same table it should. This assertion is what holds
    // that property instead - it names the seat outright rather than relying on
    // the index being unusual.
    const newgame::Config playing = playingConfig();

    REQUIRE(newgame::humanSeatOf(playing).has_value());
    REQUIRE(newgame::humanSeatOf(playing)->index == 0);

    REQUIRE_FALSE(newgame::humanSeatOf(watchingConfig()).has_value());
}

TEST_CASE("buildSetup() leaves the human's provider null and no other", "[new-game]")
{
    // THE ONE SEAT GameController::start() FILLS, addressed by the index
    // humanSeatOf() returns - and it indexes with .at(), so the two disagreeing
    // is a std::out_of_range on the GUI thread. Any OTHER null seat is rejected
    // by GameEngine::start() BY NAME, which the last case here proves is not
    // what happens.
    const newgame::Config config = playingConfig(3);
    const GameSetup setup = newgame::buildSetup(config);

    REQUIRE(setup.seats.size() == 4);
    REQUIRE(setup.seats.at(0).name == "Dan");
    REQUIRE(setup.seats.at(0).moveProvider == nullptr);

    for(std::size_t i = 1 ; i < setup.seats.size() ; i++)
    {
        INFO("seat " << i << " (" << setup.seats.at(i).name << ")");
        REQUIRE(setup.seats.at(i).moveProvider != nullptr);
    }

    // The index the controller will use, against the setup it will use it on.
    REQUIRE(newgame::humanSeatOf(config)->index < setup.seats.size());
}

TEST_CASE("A watched game leaves no seat for the controller to fill", "[new-game]")
{
    // The spectator half, and it has to be asserted separately: a buildSetup()
    // that always left seat 0 null would pass every case above and then hand
    // GameEngine::start() a nameless hole in a game with no provider to plug it.
    GameSetup setup = newgame::buildSetup(watchingConfig(4));

    REQUIRE(setup.seats.size() == 4);

    for(const SeatSetup& seat : setup.seats)
    {
        INFO("seat " << seat.name);
        REQUIRE(seat.moveProvider != nullptr);
    }

    // Straight into a real engine with nothing added, which is exactly what
    // MainWindow does for a watched game.
    GameEngine engine;
    REQUIRE_NOTHROW(engine.start(std::move(setup)));
}

TEST_CASE("A setup the dialog would produce is one the engine accepts", "[new-game]")
{
    // THE ASSERTION THE WHOLE VALIDATION EXISTS FOR, at both ends of the clamp.
    // GameEngine::start() throws five ways by name - already started, seat count
    // out of range, an empty name, a null provider, a duplicate name - and every
    // one of them reaches the GUI thread as an exception rather than a message.
    for(int opponents = 1 ; opponents <= 5 ; opponents++)
    {
        newgame::Config config = playingConfig(opponents);

        INFO("opponents " << opponents);
        REQUIRE_FALSE(newgame::firstError(config).has_value());

        GameSetup setup = newgame::buildSetup(config);
        setup.seats.at(0).moveProvider = std::make_unique<NullProvider>();

        GameEngine engine;
        REQUIRE_NOTHROW(engine.start(std::move(setup)));
    }
}

TEST_CASE("A seated game from the dialog reaches game over", "[new-game]")
{
    // PHASE C's OWN "DONE WHEN", re-asked against D1's setup builder - because
    // D1 moved the human from seat 1 to seat 0, and the seat is the index
    // GameController::start() fills and GameBridge stamps onto every snapshot.
    //
    // AND THE LAST TWO ROUNDS ARE THE POINT. With endWithForeheadAndHidden left
    // at its default the schedule ends with a Forehead round and then a Hidden
    // one, which section 8 names as the two rounds a client driving its hand
    // from snapshots stops working in. A game that stops short of them has
    // proved nothing about them.
    //
    // Synchronous, with the human's seat filled by a bot: which prompt carried
    // what is HumanSeatTests' question, and this one is about the schedule
    // completing from a setup this dialog built.
    for(int opponents = 1 ; opponents <= 5 ; opponents++)
    {
        newgame::Config config = playingConfig(opponents);
        config.shuffleSeed = 42u;

        GameSetup setup = newgame::buildSetup(config);

        // Exactly the seat humanSeatOf() names, and it is null - which is the
        // agreement start() relies on and indexes with .at().
        const std::size_t seat = newgame::humanSeatOf(config)->index;
        REQUIRE(setup.seats.at(seat).moveProvider == nullptr);

        setup.seats.at(seat).moveProvider =
            std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>());

        GameEngine engine;
        engine.start(std::move(setup));
        engine.run();

        INFO("opponents " << opponents);
        REQUIRE(engine.getStatus() == GameStatus::Finished);
        REQUIRE(engine.getCurrentRoundIndex() == newgame::roundCount(config) - 1);

        // The schedule really did end with the appended pair, so the loop above
        // cannot pass by never reaching them.
        REQUIRE(engine.getRound(newgame::roundCount(config) - 2).getRoundType()
                == RoundType::Forehead);
        REQUIRE(engine.getRound(newgame::roundCount(config) - 1).getRoundType()
                == RoundType::Hidden);
    }
}

TEST_CASE("A name the engine would reject disables Deal first", "[new-game]")
{
    newgame::Config config = playingConfig();
    REQUIRE_FALSE(newgame::firstError(config).has_value());

    SECTION("an empty name")
    {
        config.botNames[1] = "";
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a name of nothing but spaces")
    {
        // ACCEPTED BY THE ENGINE and empty to a reader, which is why the check
        // trims for the test and not for the value.
        config.humanName = "   ";
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a duplicate, byte for byte")
    {
        config.botNames[1] = config.botNames[0];
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a duplicate differing only in case")
    {
        // STRICTER THAN THE ENGINE ON PURPOSE. GameEngine::start() compares byte
        // for byte and would accept this; two seats reading Ana and ana share
        // nothing there and everything on a scoreboard.
        config.botNames[1] = config.botNames[0].toUpper();
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("the human colliding with a bot")
    {
        config.humanName = config.botNames[2];
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a name past the cap")
    {
        config.humanName = QString(newgame::MaximumNameLength + 1, 'x');
        REQUIRE(newgame::firstError(config).has_value());
    }
}

TEST_CASE("A table too small or too large is refused", "[new-game]")
{
    // Unreachable through the spinner, which is clamped - and worth checking
    // anyway, because buildSetup() and firstError() are callable without one and
    // the engine's own range check is a throw rather than a message.
    newgame::Config tooSmall = playingConfig(0);
    REQUIRE(newgame::firstError(tooSmall).has_value());

    newgame::Config tooLarge = playingConfig(5);
    tooLarge.botNames.push_back("Gheorghe");
    tooLarge.botLevels.push_back(newgame::BotLevel::Ducking);
    REQUIRE(newgame::seatCount(tooLarge) == 7);
    REQUIRE(newgame::firstError(tooLarge).has_value());
}

TEST_CASE("The bot-speed slider runs from slow to instant", "[new-game]")
{
    const Pacing slowest = newgame::pacingFor(newgame::SlowestSpeed);
    const Pacing fastest = newgame::pacingFor(newgame::FastestSpeed);

    REQUIRE(slowest.beat > fastest.beat);
    REQUIRE(slowest.trickDwell > fastest.trickDwell);
    REQUIRE(fastest.beat == std::chrono::milliseconds(0));

    // THE MIDDLE POSITION IS Pacing{}, which is what phases A to C were tuned
    // at - so a player who touches nothing gets the game this client was
    // designed against, and those three numbers still live in exactly one place.
    const Pacing middle = newgame::pacingFor(newgame::DefaultSpeed);
    const Pacing defaults;

    REQUIRE(middle.beat == defaults.beat);
    REQUIRE(middle.spectatorBeat == defaults.spectatorBeat);
    REQUIRE(middle.trickDwell == defaults.trickDwell);

    // Monotonic across every position, not only at the ends.
    for(int i = newgame::SlowestSpeed ; i < newgame::FastestSpeed ; i++)
    {
        INFO("position " << i);
        REQUIRE(newgame::pacingFor(i).beat >= newgame::pacingFor(i + 1).beat);
    }

    // Out of range is clamped rather than indexing off the table.
    REQUIRE(newgame::pacingFor(-5).beat == slowest.beat);
    REQUIRE(newgame::pacingFor(99).beat == fastest.beat);
}

TEST_CASE("The summary line names the round count it computed", "[new-game]")
{
    // Section 13's live line. Its minutes are a rule of thumb and not worth an
    // assertion, but the COUNT in it is the same arithmetic the cases above pin
    // against a real engine - so this is what catches the two ways the label goes
    // wrong without looking wrong: coming back empty, and being built from
    // something other than roundCount().
    newgame::Config config = playingConfig(3);

    REQUIRE(newgame::summaryText(config)
                .contains(QString::number(newgame::roundCount(config))));

    // It MOVES with the thing it reports, so it cannot be a fixed string that
    // happens to contain the right number today.
    config.endWithForeheadAndHidden = false;
    REQUIRE(newgame::summaryText(config)
                .contains(QString::number(newgame::roundCount(config))));

    // And at the instant end of the slider it still says something, rather than
    // promising a game of about zero minutes.
    config.botSpeed = newgame::FastestSpeed;
    REQUIRE_FALSE(newgame::summaryText(config).isEmpty());
    REQUIRE_FALSE(newgame::summaryText(config).contains(QStringLiteral("0 minutes")));

    // NOT "26 round(s)". The parenthesised plural is an idiom that only resolves
    // through a translator, so an untranslated build printed it verbatim - which
    // no assertion here noticed until the dialog was rendered and read.
    REQUIRE_FALSE(newgame::summaryText(config).contains(QStringLiteral("(s)")));
}

TEST_CASE("A fixed deal seeds the bots that draw, so the game repeats", "[new-game]")
{
    // GameSetup::shuffleSeed REACHES THE SHUFFLE AND NOTHING ELSE - the engine's
    // own header says a fully reproducible game additionally requires seeding
    // every provider that draws. RandomCardStrategy is the only one that does,
    // and it has a seeded constructor, so a fixed deal has to use it.
    //
    // Asserted as a whole game rather than by inspecting the provider, because
    // the provider is a unique_ptr<IMoveProvider> with nothing to ask.
    const auto transcript = [](std::optional<std::uint32_t> seed)
    {
        newgame::Config config = watchingConfig(3);
        config.shuffleSeed = seed;

        for(auto& level : config.botLevels)
            level = newgame::BotLevel::Random;

        GameEngine engine;
        engine.start(newgame::buildSetup(config));
        engine.run();

        std::string result;
        for(const Standing& standing : engine.getStandings())
            result += standing.name + ":" + std::to_string(standing.score) + " ";

        return result;
    };

    REQUIRE(transcript(42u) == transcript(42u));

    // And a different seed is a different game, so the case above cannot pass by
    // the seed being ignored entirely.
    REQUIRE(transcript(42u) != transcript(4242u));
}

TEST_CASE("Every bot level names a strategy that can be built", "[new-game]")
{
    // The list is section 13's four, in its order, and it doubles as the combo's
    // index - so a level added without a provider case is a table of names that
    // silently plays something else. A whole game each, because a provider that
    // returns nothing parks the engine rather than throwing.
    for(const newgame::BotLevel level : newgame::BotLevels)
    {
        newgame::Config config = watchingConfig(2);

        for(auto& each : config.botLevels)
            each = level;

        config.shuffleSeed = 42u;

        INFO("level " << newgame::nameOf(level).toStdString());
        REQUIRE_FALSE(newgame::nameOf(level).isEmpty());
        REQUIRE_FALSE(newgame::tooltipFor(level).isEmpty());

        GameEngine engine;
        engine.start(newgame::buildSetup(config));
        engine.run();

        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }
}

TEST_CASE("The dialog's rows follow the opponent count", "[new-game]")
{
    // The widget half, constructible because TestApp is a QApplication on the
    // offscreen platform. Section 13: changing the count "adds a fourth row with
    // a pre-filled default name; it does not open another dialog".
    NewGameDialog dialog(newgame::defaults());

    REQUIRE(dialog.config().botNames.size() == 3);

    // BY NAME, not by position in findChildren(). Every QSpinBox owns an
    // editable QLineEdit of its own, so a positional lookup over either list
    // finds a control the dialog did not create - which is how this file's first
    // run typed a bot name into the seed box and asserted nothing.
    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    count->setValue(5);
    REQUIRE(dialog.config().botNames.size() == 5);

    // AND THE NAMES ALREADY THERE SURVIVE, which is the half a rebuild would
    // lose - and the reason rebuildOpponentRows() adds and removes rather than
    // clearing.
    const QStringList before = dialog.config().botNames;

    count->setValue(4);
    const QStringList after = dialog.config().botNames;

    REQUIRE(after.size() == 4);
    for(int i = 0 ; i < after.size() ; i++)
        REQUIRE(after.at(i) == before.at(i));

    // No row is left blank, which would disable Deal for a reason the player did
    // not cause.
    for(const QString& name : dialog.config().botNames)
        REQUIRE_FALSE(name.isEmpty());
}

TEST_CASE("Every row of the largest table is pre-filled with a real name", "[new-game]")
{
    // THE LARGEST TABLE IS A WATCHED ONE, which is the whole of this case: six
    // bots where a played game has five, because nobody is taking a chair. The
    // stock list was five long - sized for the played maximum - so the sixth row
    // fell through to a "Bot 6" placeholder, found by rendering a six-seat game
    // rather than by anything here.
    //
    // AND THE DIALOG IS STILL DEALABLE at that size, which is the assertion that
    // makes this about more than tidiness: a placeholder name that collided with
    // a stock one would disable Deal outright.
    NewGameDialog dialog(newgame::defaults());

    QRadioButton* watch = dialog.findChild<QRadioButton*>("watching");
    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    QPushButton* deal = dialog.findChild<QPushButton*>("deal");
    REQUIRE(watch != nullptr);
    REQUIRE(count != nullptr);
    REQUIRE(deal != nullptr);

    watch->setChecked(true);
    count->setValue(static_cast<int>(newgame::MaximumSeats));

    const newgame::Config config = dialog.config();
    REQUIRE(config.botNames.size() == static_cast<int>(newgame::MaximumSeats));

    for(const QString& name : config.botNames)
    {
        INFO("name " << name.toStdString());
        REQUIRE_FALSE(name.isEmpty());
        REQUIRE_FALSE(name.startsWith(QStringLiteral("Bot ")));
    }

    REQUIRE_FALSE(newgame::firstError(config).has_value());
    REQUIRE(deal->isEnabled());
}

TEST_CASE("Reopening on a config offers back the game it describes", "[new-game]")
{
    // WHAT "PLAY AGAIN PRE-FILLS WITH THE SAME SETTINGS" HAS TO MEAN (section
    // 16.2), and what it did not: every control read the config it was opened on
    // except the opponent rows, which were built from the stock names and levels
    // regardless - and refresh() then wrote those defaults back over the config,
    // so a second game was dealt against three seats the player never chose.
    //
    // EVERY CASE ABOVE OPENS ON newgame::defaults(), which is exactly why this
    // passed a green suite: the stock names ARE the defaults' names, so the
    // overwrite was invisible to a dialog opened on them.
    newgame::Config config = newgame::defaults();

    config.botNames = QStringList{ "Zoltan", "Yvette" };
    config.botLevels = { newgame::BotLevel::Ducking, newgame::BotLevel::Random };

    NewGameDialog dialog(config);

    const newgame::Config offered = dialog.config();

    REQUIRE(offered.botNames == config.botNames);
    REQUIRE(offered.botLevels == config.botLevels);

    // The rows themselves, not only what config() computes from them - the
    // player has to SEE the game they are being offered.
    QLineEdit* first = dialog.findChild<QLineEdit*>("botName0");
    QComboBox* firstLevel = dialog.findChild<QComboBox*>("botLevel0");
    REQUIRE(first != nullptr);
    REQUIRE(firstLevel != nullptr);

    REQUIRE(first->text() == "Zoltan");
    REQUIRE(newgame::BotLevels[firstLevel->currentIndex()] == newgame::BotLevel::Ducking);

    // A SEAT THE CONFIG NEVER HAD still gets a stock default rather than a blank
    // - the pre-fill above must not have cost the fallback that keeps Deal
    // enabled for a reason the player did not cause.
    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    count->setValue(3);

    const newgame::Config grown = dialog.config();
    REQUIRE(grown.botNames.size() == 3);
    REQUIRE(grown.botNames.at(0) == "Zoltan");
    REQUIRE_FALSE(grown.botNames.at(2).isEmpty());
    REQUIRE_FALSE(newgame::firstError(grown).has_value());
}

TEST_CASE("The spinner opens on the range its own game allows", "[new-game]")
{
    // THE RANGE WAS SET ONCE FOR A PLAYED GAME AND THEN MAINTAINED CORRECTLY,
    // which is the shape that hides: every later change goes through the radio's
    // handler, and only the initial state had nothing maintaining it - the
    // handler is connected after this runs.
    //
    // A WATCHED GAME OF SIX IS THE LARGEST TABLE THERE IS. Clamped to the played
    // ceiling of five, the dialog silently offers a different game from the one
    // it was opened on.
    newgame::Config config = newgame::defaults();
    config.spectator = true;
    config.botNames = QStringList{ "A", "B", "C", "D", "E", "F" };
    config.botLevels = std::vector<newgame::BotLevel>(6, newgame::BotLevel::Random);

    NewGameDialog dialog(config);

    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    REQUIRE(count->value() == static_cast<int>(newgame::MaximumSeats));
    REQUIRE(dialog.config().botNames.size() == static_cast<int>(newgame::MaximumSeats));

    // And the floor is the watched one, so the spinner cannot reach a table the
    // engine would refuse - section 13 wants the control to make the illegal
    // value unreachable rather than the message to explain it.
    REQUIRE(count->minimum() == static_cast<int>(newgame::MinimumSeats));
}

TEST_CASE("The dialog refuses to deal a game the engine would reject", "[new-game]")
{
    NewGameDialog dialog(newgame::defaults());

    QPushButton* deal = dialog.findChild<QPushButton*>("deal");
    QLineEdit* first = dialog.findChild<QLineEdit*>("botName0");
    QLineEdit* second = dialog.findChild<QLineEdit*>("botName1");

    REQUIRE(deal != nullptr);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(deal->isEnabled());

    // Two names the same, TYPED rather than set on the Config - so this goes
    // through the same textChanged path a player does.
    first->setText(second->text());
    REQUIRE_FALSE(deal->isEnabled());

    first->setText("Someone else");
    REQUIRE(deal->isEnabled());

    // Differing only in case, which the engine would accept and a scoreboard
    // could not tell apart.
    first->setText(second->text().toUpper());
    REQUIRE_FALSE(deal->isEnabled());

    first->setText("Someone else");
    REQUIRE(deal->isEnabled());

    // And an empty one, which is the other thing GameEngine::start() throws on.
    first->setText("");
    REQUIRE_FALSE(deal->isEnabled());
}

TEST_CASE("Watching removes the human's seat from the dialog's answer", "[new-game]")
{
    // "I'm watching" is not a mode bolted on later (section 13) - it is
    // humanSeat == std::nullopt, which is the state sections 4.3 and 6
    // null-check for and which nothing in this client has run since phase B.
    NewGameDialog dialog(newgame::defaults());

    REQUIRE_FALSE(dialog.config().spectator);
    REQUIRE(newgame::humanSeatOf(dialog.config()).has_value());

    QRadioButton* watch = dialog.findChild<QRadioButton*>("watching");
    REQUIRE(watch != nullptr);
    watch->setChecked(true);

    REQUIRE(dialog.config().spectator);
    REQUIRE_FALSE(newgame::humanSeatOf(dialog.config()).has_value());

    // The bot count's floor moves with it: a watched game needs two bots where a
    // played one needs one, because there is no human seat making up the pair.
    //
    // THIS IS THE ASSERTION THAT FOUND THE RADIO GROUPING. All four radios are
    // children of the dialog, so Qt auto-grouped them into ONE exclusive set and
    // checking a game shape in the constructor unchecked "I'm playing" - after
    // which toggling to watching emitted nothing and the range never moved. The
    // dialog looked entirely correct.
    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);
    REQUIRE(count->minimum() == static_cast<int>(newgame::MinimumSeats));
    REQUIRE(count->maximum() == static_cast<int>(newgame::MaximumSeats));
}
