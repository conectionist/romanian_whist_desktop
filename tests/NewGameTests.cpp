#include <catch2/catch_test_macros.hpp>

#include "GameFixtures.h"

#include "paint/Avatars.h"
#include "ui/LanguageDialog.h"
#include "ui/NewGameDialog.h"
#include "ui/PlayerNames.h"

#include <romanian_whist/GameEngine.h>
#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/IMoveProvider.h>
#include <romanian_whist/IGameObserver.h>
#include <romanian_whist/RoundType.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>
#include <romanian_whist/strategies/reckoner/ReckonerKnobs.h>

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSpinBox>

#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

using namespace romanian_whist;

// The split SnapshotLog.h argues for and the scoreboard follows, and the reason D1 needs
// it more than either: the dialog's answers reach GameEngine::start(), which
// throws five different ways by name, and every one of those is a crash on the
// GUI thread rather than a message. The part with a right answer is tested here
// without a window, and the window is tested after it.
namespace
{
// The opponents' names, as the cases below compare them.
QStringList namesOf(const std::vector<PlayerProfile>& players)
{
    QStringList names;

    for(const PlayerProfile& player : players)
        names.push_back(QString::fromStdString(player.name));

    return names;
}

// Opponents with these names, every one Male: the cases that use it are about
// names, not genders.
std::vector<PlayerProfile> named(const QStringList& names)
{
    std::vector<PlayerProfile> players;

    for(const QString& name : names)
        players.push_back(PlayerProfile{ name.toStdString(), Gender::Male });

    return players;
}

// WHETHER BOT `i`'S NAME IS ONE THE LIST GIVES A BOT OF ITS GENDER AND LEVEL, in
// the app's language: one of its strategy's own names, or an ordinary one (which
// it gets when its strategy has none, or none free).
bool drawnFor(const newgame::Config& config, std::size_t i)
{
    const PlayerProfile& bot = config.bots.at(i);
    const QString name = QString::fromStdString(bot.name);
    const std::string strategy = newgame::avatarStrategyOf(config.botLevels.at(i));

    for(const playernames::Name& entry : playernames::catalogue())
        if(entry.name == name && (entry.strategy.empty() || entry.strategy == strategy)
           && playernames::suits(entry, bot.gender, language::current()))
            return true;

    return false;
}

// The app's language for one case's lifetime, then English again: Catch2 runs the
// cases in a random order, and English is what every other case expects.
struct RunningIn
{
    explicit RunningIn(language::Language language) { language::setCurrent(language); }
    ~RunningIn() { language::setCurrent(language::Language::English); }
};

newgame::Config playingConfig(int opponents = 3)
{
    newgame::Config config;
    config.human = PlayerProfile{ "Dan", Gender::Male };

    static const char* names[] = { "Ana", "Bogdan", "Cristina", "Elena", "Florin" };

    for(int i = 0 ; i < opponents ; i++)
    {
        config.bots.push_back(PlayerProfile{ names[i], Gender::Male });
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

// Every bid and every card of a game, in the order the engine took them - what a
// replay has to match, and what equal standings alone do not prove.
class PlayRecorder : public IGameObserver
{
public:
    std::string log;

    void onBetPlaced(const GameEngine&, Seat seat, unsigned int bet) override
    {
        log += "b" + std::to_string(seat.index) + ":" + std::to_string(bet) + " ";
    }

    void onCardPlayed(const GameEngine&, Seat seat, const Card& card) override
    {
        log += "c" + std::to_string(seat.index) + ":" + card.toString() + " ";
    }
};

// Starts an engine on a deal the way GameController::runGame() does: the
// observers first, then the setup.
//
// NOT A CONVENIENCE, AND NOT SKIPPABLE. Written without it, the all-levels case
// below died with "Ana played a card that is not legal in this trick" - because an
// unregistered ReckonerStrategy keeps its tracker's default four-handed table, and
// the base policy then picks out of the wrong deck profile and against a led suit
// it never saw. So the cost of dropping the observers is not a bot that plays
// badly, as it would be for the memory alone: it is a std::logic_error out of the
// engine, on the game thread, from a strategy the dialog offered as a menu row.
// Every site here goes through this for that reason, including the ones whose
// levels cannot produce an observer today.
void startWith(GameEngine& engine, GameDeal deal)
{
    for(IGameObserver* observer : deal.observers)
        engine.addObserver(observer);

    // The setup's providers move INTO the engine here, so the deal this function
    // destroys on its way out holds nothing the engine is using.
    engine.start(std::move(deal.setup));
}
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

            GameDeal deal = newgame::buildDeal(config);
            deal.setup.seats.at(0).moveProvider = std::make_unique<NullProvider>();

            GameEngine engine;
            startWith(engine, std::move(deal));

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

TEST_CASE("buildDeal() leaves the human's provider null and no other", "[new-game]")
{
    // THE ONE SEAT GameController::start() FILLS, addressed by the index
    // humanSeatOf() returns - and it indexes with .at(), so the two disagreeing
    // is a std::out_of_range on the GUI thread. Any OTHER null seat is rejected
    // by GameEngine::start() BY NAME, which the last case here proves is not
    // what happens.
    const newgame::Config config = playingConfig(3);
    const GameSetup setup = newgame::buildDeal(config).setup;

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
    // The spectator half, and it has to be asserted separately: a buildDeal()
    // that always left seat 0 null would pass every case above and then hand
    // GameEngine::start() a nameless hole in a game with no provider to plug it.
    GameDeal deal = newgame::buildDeal(watchingConfig(4));

    REQUIRE(deal.setup.seats.size() == 4);

    for(const SeatSetup& seat : deal.setup.seats)
    {
        INFO("seat " << seat.name);
        REQUIRE(seat.moveProvider != nullptr);
    }

    // Straight into a real engine with nothing added, which is exactly what
    // MainWindow does for a watched game.
    GameEngine engine;
    REQUIRE_NOTHROW(startWith(engine, std::move(deal)));
}

TEST_CASE("The deal carries a profile per seat in seat order", "[new-game]")
{
    // SEAT i IS PROFILE i, which is all GameBridge has to go on when it stamps a
    // gender onto each seat: nothing matches them by name afterwards. So the two
    // lists have to come out of buildDeal() side by side, with the human first.
    SECTION("a seated game")
    {
        newgame::Config config = playingConfig(3);
        config.human.gender = Gender::Female;
        config.bots[1].gender = Gender::Female;

        const GameDeal deal = newgame::buildDeal(config);

        REQUIRE(deal.profiles.size() == deal.setup.seats.size());
        REQUIRE(deal.profiles.at(0).name == config.human.name);
        REQUIRE(deal.profiles.at(0).gender == config.human.gender);

        for(std::size_t seat = 0 ; seat < deal.setup.seats.size() ; seat++)
            REQUIRE(deal.profiles.at(seat).name == deal.setup.seats.at(seat).name);

        // Name and gender as the config has them; the avatar is the deal's own
        // (the next case).
        for(std::size_t bot = 0 ; bot < config.bots.size() ; bot++)
        {
            REQUIRE(deal.profiles.at(bot + 1).name == config.bots.at(bot).name);
            REQUIRE(deal.profiles.at(bot + 1).gender == config.bots.at(bot).gender);
        }
    }

    SECTION("a watched game, where bot i is seat i")
    {
        newgame::Config config = watchingConfig(4);
        config.bots[0].gender = Gender::Female;

        const GameDeal deal = newgame::buildDeal(config);

        REQUIRE(deal.profiles.size() == config.bots.size());

        for(std::size_t bot = 0 ; bot < config.bots.size() ; bot++)
        {
            REQUIRE(deal.profiles.at(bot).name == config.bots.at(bot).name);
            REQUIRE(deal.profiles.at(bot).gender == config.bots.at(bot).gender);
        }
    }
}

TEST_CASE("Every seat of a dealt game wears a face of its own gender", "[new-game]")
{
    // CHOSEN AS THE GAME IS DEALT, at random by the owner's rule
    // (avatars::assign()): New Game's settings carry no face, the deal does.
    newgame::Config config = newgame::defaults();

    REQUIRE(config.human.avatar.empty());
    for(const PlayerProfile& bot : config.bots)
        REQUIRE(bot.avatar.empty());

    // TWO MEN AND TWO WOMEN, you among the men, pinned: the defaults' bots are a
    // man or a woman at random, and three women would outnumber the catalogue's
    // two women's faces. With two of each there are faces enough, so no two seats
    // share one.
    config.human.gender = Gender::Male;
    config.bots.at(0).gender = Gender::Female;
    config.bots.at(1).gender = Gender::Male;
    config.bots.at(2).gender = Gender::Female;
    for(int deal = 0 ; deal < 20 ; deal++)
    {
        const GameDeal dealt = newgame::buildDeal(config);
        std::set<std::string> faces;

        // You at seat 0, with no strategy; then the bots with their levels'.
        const std::vector<avatars::Wearer> seated = newgame::wearersOf(config);

        for(std::size_t seat = 0 ; seat < dealt.profiles.size() ; seat++)
        {
            const PlayerProfile& profile = dealt.profiles[seat];

            INFO("deal " << deal << ", " << profile.name << " wears " << profile.avatar);

            const avatars::Avatar* avatar = avatars::find(profile.avatar);
            REQUIRE(avatar != nullptr);
            REQUIRE(avatars::suits(*avatar, profile.gender));

            // Its strategy's own face, or an ordinary one - never another's.
            REQUIRE((avatar->strategy.empty() || avatar->strategy == seated.at(seat).strategy));

            faces.insert(profile.avatar);
        }

        REQUIRE(faces.size() == dealt.profiles.size());
    }
}

TEST_CASE("Each level has its own name in the avatar catalogue", "[new-game]")
{
    // AN AVATAR DEDICATED TO A STRATEGY names it by these (avatars.json's
    // "strategy"). Each Reckoner preset is a strategy of its own (the owner's
    // call), so no two levels may share a name.
    std::set<std::string> names;

    for(const newgame::BotLevel level : newgame::BotLevels)
    {
        const std::string name = newgame::avatarStrategyOf(level);

        INFO("level " << newgame::nameOf(level).toStdString());
        REQUIRE_FALSE(name.empty());
        REQUIRE(names.insert(name).second);
    }

    REQUIRE(std::string(newgame::avatarStrategyOf(newgame::BotLevel::Cyborg)) == "cyborg");
    REQUIRE(std::string(newgame::avatarStrategyOf(newgame::BotLevel::ReckonerAlpha)) == "reckoner-alpha");
}

TEST_CASE("The avatar rule sees every seat's gender and strategy in seat order", "[new-game]")
{
    // What buildDeal() hands avatars::assign(): you first with no strategy, so no
    // bot's dedicated face is ever yours; then each bot with its level's.
    newgame::Config config = playingConfig(3);
    config.human.gender = Gender::Female;
    config.bots[1].gender = Gender::Female;
    config.botLevels = { newgame::BotLevel::Cyborg, newgame::BotLevel::ReckonerBeta, newgame::BotLevel::Random };

    const std::vector<avatars::Wearer> seated = newgame::wearersOf(config);

    REQUIRE(seated.size() == 4);
    REQUIRE(seated[0].gender == Gender::Female);
    REQUIRE(seated[0].strategy.empty());
    REQUIRE(seated[1].gender == Gender::Male);
    REQUIRE(seated[1].strategy == "cyborg");
    REQUIRE(seated[2].gender == Gender::Female);
    REQUIRE(seated[2].strategy == "reckoner-beta");
    REQUIRE(seated[3].strategy == "random");

    // Watching, bot i is seat i.
    config.spectator = true;
    const std::vector<avatars::Wearer> watched = newgame::wearersOf(config);

    REQUIRE(watched.size() == 3);
    REQUIRE(watched[0].strategy == "cyborg");
}

TEST_CASE("A Cyborg bot wears a cyborg face and nobody else does", "[new-game]")
{
    // THE OWNER'S CYBORG ART (avatars.json, "strategy": "cyborg"), a man's and a
    // woman's: dealt from New Game, a Cyborg of either gender wears one, and the
    // other bots and you never do.
    newgame::Config config = playingConfig(4);
    config.bots[0].gender = Gender::Male;
    config.bots[2].gender = Gender::Female;
    config.botLevels = { newgame::BotLevel::Cyborg, newgame::BotLevel::LowRisk,
                         newgame::BotLevel::Cyborg, newgame::BotLevel::Ducking };

    for(int deal = 0 ; deal < 20 ; deal++)
    {
        const GameDeal dealt = newgame::buildDeal(config);

        for(std::size_t seat = 0 ; seat < dealt.profiles.size() ; seat++)
        {
            const avatars::Avatar* avatar = avatars::find(dealt.profiles[seat].avatar);
            REQUIRE(avatar != nullptr);

            const bool cyborg = seat == 1 || seat == 3;

            INFO("deal " << deal << ", seat " << seat << " wears " << avatar->id);
            REQUIRE((avatar->strategy == "cyborg") == cyborg);
            REQUIRE(avatars::suits(*avatar, dealt.profiles[seat].gender));
        }
    }
}

TEST_CASE("A fixed deal deals the same faces every time", "[new-game]")
{
    // SO A RENDERING CHANGE CAN STILL BE REVIEWED AS A DIFF: the screenshot tool
    // deals fixed games, and its pictures must come out the same twice.
    newgame::Config config = newgame::defaults();
    config.shuffleSeed = 42u;

    const auto faces = [&]
    {
        std::vector<std::string> worn;

        for(const PlayerProfile& profile : newgame::buildDeal(config).profiles)
            worn.push_back(profile.avatar);

        return worn;
    };

    REQUIRE(faces() == faces());
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

        GameDeal deal = newgame::buildDeal(config);
        deal.setup.seats.at(0).moveProvider = std::make_unique<NullProvider>();

        GameEngine engine;
        REQUIRE_NOTHROW(startWith(engine, std::move(deal)));
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

        GameDeal deal = newgame::buildDeal(config);

        // Exactly the seat humanSeatOf() names, and it is null - which is the
        // agreement start() relies on and indexes with .at().
        const std::size_t seat = newgame::humanSeatOf(config)->index;
        REQUIRE(deal.setup.seats.at(seat).moveProvider == nullptr);

        deal.setup.seats.at(seat).moveProvider =
            std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>());

        GameEngine engine;
        startWith(engine, std::move(deal));
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
        config.bots[1].name = "";
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a name of nothing but spaces")
    {
        // ACCEPTED BY THE ENGINE and empty to a reader, which is why the check
        // trims for the test and not for the value.
        config.human.name = "   ";
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a duplicate, byte for byte")
    {
        config.bots[1].name = config.bots[0].name;
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a duplicate differing only in case")
    {
        // STRICTER THAN THE ENGINE ON PURPOSE. GameEngine::start() compares byte
        // for byte and would accept this; two seats reading Ana and ana share
        // nothing there and everything on a scoreboard.
        config.bots[1].name = QString::fromStdString(config.bots[0].name).toUpper().toStdString();
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("the human colliding with a bot")
    {
        config.human.name = config.bots[2].name;
        REQUIRE(newgame::firstError(config).has_value());
    }

    SECTION("a name past the cap")
    {
        config.human.name = std::string(newgame::MaximumNameLength + 1, 'x');
        REQUIRE(newgame::firstError(config).has_value());
    }
}

TEST_CASE("A table too small or too large is refused", "[new-game]")
{
    // Unreachable through the spinner, which is clamped - and worth checking
    // anyway, because buildDeal() and firstError() are callable without one and
    // the engine's own range check is a throw rather than a message.
    newgame::Config tooSmall = playingConfig(0);
    REQUIRE(newgame::firstError(tooSmall).has_value());

    newgame::Config tooLarge = playingConfig(5);
    tooLarge.bots.push_back(PlayerProfile{ "Gheorghe", Gender::Male });
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

    // And a watched game at the instant end of the slider - the one estimate that
    // rounds to nothing - still says something, rather than promising a game of
    // about zero minutes.
    config = watchingConfig();
    config.botSpeed = newgame::FastestSpeed;
    REQUIRE(newgame::estimatedMinutes(config) == 0);
    REQUIRE_FALSE(newgame::summaryText(config).isEmpty());
    REQUIRE(newgame::summaryText(config).contains(QString::number(newgame::roundCount(config))));
    REQUIRE_FALSE(newgame::summaryText(config).contains(QStringLiteral("minute")));

    // NOT "26 round(s)". The parenthesised plural is an idiom that only resolves
    // through a translator, so an untranslated build printed it verbatim - which
    // no assertion here noticed until the dialog was rendered and read.
    REQUIRE_FALSE(newgame::summaryText(config).contains(QStringLiteral("(s)")));
}

// THE OWNER'S MEASURE, 2026-10-23: with a person at the table a game takes about
// 30 minutes for two players and 5 more for each player added, with the bots at
// the default speed. Worked out from the bots' pace alone, as a watched game's
// is, it came out at a few minutes.
TEST_CASE("A game you play in is about 30 minutes for two and 5 more a player", "[new-game]")
{
    for(int opponents = 1 ; opponents <= 5 ; opponents++)
    {
        newgame::Config config = playingConfig(opponents);
        config.botSpeed = newgame::DefaultSpeed;

        const int expected = 30 + 5 * (opponents - 1);

        INFO(opponents << " opponents");
        REQUIRE(newgame::estimatedMinutes(config) == expected);
        REQUIRE(newgame::summaryText(config).contains(QStringLiteral("about %1 minutes").arg(expected)));
    }
}

// THE BOTS' SPEED STILL COUNTS: they wait before every card and after every
// trick, and the slider sets how long. What a speed adds or takes off is what the
// bots then wait more or less than at the default - so the more bots, the wider
// the spread.
TEST_CASE("A game you play in is longer with slower bots", "[new-game]")
{
    for(int opponents = 1 ; opponents <= 5 ; opponents++)
    {
        newgame::Config config = playingConfig(opponents);

        INFO(opponents << " opponents");

        for(int speed = newgame::SlowestSpeed ; speed < newgame::FastestSpeed ; speed++)
        {
            config.botSpeed = speed;
            const int slower = newgame::estimatedMinutes(config);

            config.botSpeed = speed + 1;

            INFO("bot speed " << speed);
            REQUIRE(slower >= newgame::estimatedMinutes(config));
        }

        config.botSpeed = newgame::DefaultSpeed;
        const int measured = newgame::estimatedMinutes(config);

        config.botSpeed = newgame::SlowestSpeed;
        REQUIRE(newgame::estimatedMinutes(config) > measured);

        config.botSpeed = newgame::FastestSpeed;
        REQUIRE(newgame::estimatedMinutes(config) < measured);
    }

    // At the ends of the slider, worked out by hand from the schedule (32 rounds
    // and 116 tricks for six players; a beat of 700 ms and a dwell of 1.2 s at
    // the default, twice that at the slowest and nothing at Instant): the bots
    // wait about 12 minutes at the default, so 62 minutes at the slowest and 38 at
    // Instant.
    newgame::Config six = playingConfig(5);

    six.botSpeed = newgame::SlowestSpeed;
    REQUIRE(newgame::estimatedMinutes(six) == 62);

    six.botSpeed = newgame::FastestSpeed;
    REQUIRE(newgame::estimatedMinutes(six) == 38);

    // And two players, with one bot, move least: 33 and 27.
    newgame::Config two = playingConfig(1);

    two.botSpeed = newgame::SlowestSpeed;
    REQUIRE(newgame::estimatedMinutes(two) == 33);

    two.botSpeed = newgame::FastestSpeed;
    REQUIRE(newgame::estimatedMinutes(two) == 27);
}

// WAITING AFTER EACH ROUND, the result is held until you go on and the bots take
// no dwell there, so the speed moves the estimate by the tricks' dwells alone. By
// hand, six players at the default: 772 beats of 700 ms and 116 trick dwells of
// 1.2 s are 679.6 s, so 61 minutes at the slowest and 39 at Instant - where the
// round dwells, counted when they never happen, made it 62 and 38.
TEST_CASE("Waiting after each round leaves the round's dwell out of the estimate", "[new-game]")
{
    newgame::Config six = playingConfig(5);
    six.waitAfterEachRound = true;

    six.botSpeed = newgame::DefaultSpeed;
    REQUIRE(newgame::estimatedMinutes(six) == 50);

    six.botSpeed = newgame::SlowestSpeed;
    REQUIRE(newgame::estimatedMinutes(six) == 61);

    six.botSpeed = newgame::FastestSpeed;
    REQUIRE(newgame::estimatedMinutes(six) == 39);
}

TEST_CASE("A watched game's estimate follows the bot speed", "[new-game]")
{
    newgame::Config config = watchingConfig(5);

    // Never longer at a faster position, and nothing at the instant end.
    for(int speed = newgame::SlowestSpeed ; speed < newgame::FastestSpeed ; speed++)
    {
        config.botSpeed = speed;
        const int slower = newgame::estimatedMinutes(config);

        config.botSpeed = speed + 1;

        INFO("bot speed " << speed);
        REQUIRE(slower >= newgame::estimatedMinutes(config));
    }

    config.botSpeed = newgame::SlowestSpeed;
    REQUIRE(newgame::estimatedMinutes(config) > 0);

    config.botSpeed = newgame::FastestSpeed;
    REQUIRE(newgame::estimatedMinutes(config) == 0);

    // Well under the game a person plays in: the bots alone are quick.
    config.botSpeed = newgame::DefaultSpeed;
    REQUIRE(newgame::estimatedMinutes(config) < newgame::estimatedMinutes(playingConfig(5)));
}

TEST_CASE("A fixed deal seeds the bots that draw, so the game repeats", "[new-game]")
{
    // GameSetup::shuffleSeed REACHES THE SHUFFLE AND NOTHING ELSE - the engine's
    // own header says a fully reproducible game additionally requires seeding
    // every provider that draws. RandomCardStrategy and the Reckoner are the ones
    // that do, and both have seeded constructors, so a fixed deal has to use
    // them. The Reckoner's replay is the engine suite's to prove
    // (ReckonerStrategyTests); this table keeps to the cheap one.
    //
    // Asserted as a whole game rather than by inspecting the provider, because
    // the provider is a unique_ptr<IMoveProvider> with nothing to ask.
    //
    // BID FOR BID AND CARD FOR CARD, not by the standings alone. Two games that
    // part ways can still end on the same scores, and a replay compared only at
    // the end would call them one game - so a Cyborg that started to draw would
    // pass here while real replays diverged under it.
    const auto transcript = [](std::optional<std::uint32_t> seed)
    {
        newgame::Config config = watchingConfig(3);
        config.shuffleSeed = seed;

        for(auto& level : config.botLevels)
            level = newgame::BotLevel::Random;

        // AND ONE SEAT GIVEN NO SEED AT ALL. The Cyborg draws nothing, so
        // buildDeal() hands it nothing - and this replay is what says that is
        // enough, rather than a seat the fixed deal forgot.
        config.botLevels.at(1) = newgame::BotLevel::Cyborg;

        // Declared before the engine, so the engine - which keeps a pointer to
        // it - is destroyed first.
        PlayRecorder recorder;

        GameEngine engine;
        engine.addObserver(&recorder);
        startWith(engine, newgame::buildDeal(config));
        engine.run();

        // An empty log would compare equal to itself, and say nothing.
        REQUIRE_FALSE(recorder.log.empty());

        std::string result = recorder.log;
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
    // The list doubles as the combo's index - so a level added without a provider
    // case is a table of names that silently plays something else.
    //
    // NOT A WHOLE GAME PER LEVEL ANY MORE, and that is a deliberate division of
    // labour rather than a corner cut. The Reckoner's four presets are one code
    // path and four tables of numbers, and the ENGINE's own suite already plays
    // full games at Alpha, Gamma and Delta and asserts seeded replay
    // (libs/RomanianWhistEngine/tests/ReckonerStrategyTests.cpp, and a tournament
    // besides). Gamma samples two hundred imagined deals per card and Delta four
    // hundred; playing two more full games here would re-test the engine at this
    // suite's expense, three times over, because LOCAL_VERIFICATION.md builds this
    // binary under three sanitizers.
    //
    // WHAT THIS SUITE OWES IS THE WIRING: that every level names something
    // buildable, and that the thing it builds reaches a seat. The game below is
    // what proves a provider actually answers rather than parking the engine, and
    // ReckonerAlpha is free enough to carry that for all four presets - its K_play
    // and K_bid are 0, so the rollout engine short-circuits and it is the base
    // policy plus an epsilon roll.
    //
    // THE CYBORG PLAYS ITS WHOLE GAME, being no Reckoner: it samples nothing and
    // decides in about half a microsecond, so there is no preset to stand in for
    // it and no cost to save by trying.
    //
    // ALPHA'S ZEROES ARE CHECKED, not only stated: a retune that gives it a search
    // fails the build here, rather than quietly running rollouts in this case
    // under three sanitizers.
    static_assert(reckoner::ReckonerKnobs::alpha().K_play == 0 &&
                      reckoner::ReckonerKnobs::alpha().K_bid == 0,
                  "the comment above relies on Alpha sampling nothing");

    for(const newgame::BotLevel level : newgame::BotLevels)
    {
        newgame::Config config = watchingConfig(2);

        for(auto& each : config.botLevels)
            each = level;

        config.shuffleSeed = 42u;

        INFO("level " << newgame::nameOf(level).toStdString());
        REQUIRE_FALSE(newgame::nameOf(level).isEmpty());
        REQUIRE_FALSE(newgame::tooltipFor(level).isEmpty());

        const GameDeal deal = newgame::buildDeal(config);

        REQUIRE(deal.setup.seats.size() == 2);
        for(const SeatSetup& seat : deal.setup.seats)
        {
            INFO("seat " << seat.name);
            REQUIRE(seat.moveProvider != nullptr);
        }

        if(newgame::isReckoner(level) && level != newgame::BotLevel::ReckonerAlpha)
            continue;

        GameEngine engine;
        startWith(engine, newgame::buildDeal(config));
        engine.run();

        REQUIRE(engine.getStatus() == GameStatus::Finished);
    }
}

TEST_CASE("Every Reckoner tooltip quotes the knobs its seat is built with", "[new-game]")
{
    // THE NUMBERS ARE ARGUMENTS, and this is what keeps them so. tooltipFor()
    // reads each preset's figures off the same ReckonerKnobs that providerFor()
    // builds its seat with. A case that read a neighbour's table, or a figure
    // typed in by hand, would still read well and be wrong - and a retune is
    // exactly when nobody rereads a tooltip.
    //
    // EVERY FIGURE A PLAYER CAN FEEL: the deals it imagines, how often it plays
    // or bids at random, and the weight it gives an opponent's points. A zero is
    // skipped, because its sentence says "no" or "never" instead - and that is
    // what the static_asserts beside each sentence are for.
    //
    // AND ONLY THOSE. A knob the engine never acts on is a figure nobody can
    // feel, and quoting one is the other way these tooltips have gone wrong:
    // Alpha's first rewrite quoted a bid noise that its preset never rolls.
    struct Preset
    {
        newgame::BotLevel level;
        reckoner::ReckonerKnobs knobs;
    };

    using Knobs = reckoner::ReckonerKnobs;

    const Preset presets[] = { { newgame::BotLevel::ReckonerAlpha, Knobs::alpha() },
                               { newgame::BotLevel::ReckonerBeta, Knobs::beta() },
                               { newgame::BotLevel::ReckonerGamma, Knobs::gamma() },
                               { newgame::BotLevel::ReckonerDelta, Knobs::delta() } };

    const auto percent = [](float fraction)
    {
        return QStringLiteral("%1%").arg(qRound(fraction * 100.0f));
    };

    // AS A WHOLE FIGURE, so Beta's 5% cannot pass by sitting inside its 15%,
    // nor a sample count inside a percentage.
    const auto quotes = [](const QString& tooltip, const QString& figure)
    {
        return tooltip.contains(QRegularExpression(
            QStringLiteral("(?<!\\d)%1(?![\\d%])").arg(QRegularExpression::escape(figure))));
    };

    for(const Preset& preset : presets)
    {
        const QString tooltip = newgame::tooltipFor(preset.level);

        INFO("level " << newgame::nameOf(preset.level).toStdString());
        INFO("tooltip " << tooltip.toStdString());

        QStringList figures;

        if(preset.knobs.epsilon > 0.0f)
            figures << percent(preset.knobs.epsilon);

        // BID NOISE ONLY WHERE THE ENGINE ROLLS IT. RolloutEngine::chooseBid()
        // returns before its noise when K_bid is 0, so at Alpha the knob is
        // inert whatever it holds.
        const bool bidNoiseActs = preset.knobs.bidNoise > 0.0f && preset.knobs.K_bid > 0;

        if(bidNoiseActs)
            figures << percent(preset.knobs.bidNoise);

        // THE WEIGHTS ACT ONLY INSIDE ROLLOUTS, so a preset that imagines no
        // deals has none to quote, whatever its lambdas hold.
        if(preset.knobs.K_play > 0)
        {
            figures << QString::number(preset.knobs.K_play);

            for(const float weight : { preset.knobs.lambdaDefault, preset.knobs.lambdaLeader })
                if(weight > 0.0f)
                    figures << percent(weight);
        }

        // AND SOMETHING WAS CHECKED, so a preset cannot pass by quoting nothing.
        REQUIRE_FALSE(figures.isEmpty());

        for(const QString& figure : figures)
        {
            INFO("figure " << figure.toStdString());
            REQUIRE(quotes(tooltip, figure));
        }

        // AND NOT THE NOISE IT NEVER ROLLS - unless another knob shares the
        // figure, which the loop above has already required the tooltip to quote.
        const QString inert = percent(preset.knobs.bidNoise);

        if(preset.knobs.bidNoise > 0.0f && !bidNoiseActs && !figures.contains(inert))
        {
            INFO("inert figure " << inert.toStdString());
            REQUIRE_FALSE(quotes(tooltip, inert));
        }
    }
}

TEST_CASE("A Reckoner or Cyborg seat comes with an observer and the others come with none",
          "[new-game]")
{
    // WHAT A RECKONER OR CYBORG SEAT IS: a provider AND an observer, the same
    // object twice, and it is useless as the first without being registered as
    // the second - it plays out of a memory only the engine's callbacks fill. A
    // buildDeal() that returned the providers alone would deal a table of bots
    // that refuse to play, and nothing before the first decision would say so.
    // This is the case that notices.
    for(const newgame::BotLevel level : newgame::BotLevels)
    {
        newgame::Config config = watchingConfig(3);

        for(auto& each : config.botLevels)
            each = level;

        const GameDeal deal = newgame::buildDeal(config);

        INFO("level " << newgame::nameOf(level).toStdString());
        REQUIRE(deal.observers.size() == (newgame::isObserver(level) ? 3u : 0u));

        for(IGameObserver* observer : deal.observers)
        {
            // dynamic_cast AND NOT A NULL CHECK, which is the whole assertion.
            // Both strategies derive from IStrategy and IGameObserver, so the two
            // base subobjects sit at different addresses inside each - upcasting
            // from the wrong one compiles and yields a non-null pointer to the
            // wrong vtable. This is what pins down that the engine is being handed
            // the IGameObserver subobject of a real strategy.
            //
            // AND TO THE LEVEL'S OWN TYPE, not to either of the two: a Cyborg row
            // that built a Reckoner would pass a cast to "something that watches".
            if(level == newgame::BotLevel::Cyborg)
                REQUIRE(dynamic_cast<CyborgStrategy*>(observer) != nullptr);
            else
                REQUIRE(dynamic_cast<ReckonerStrategy*>(observer) != nullptr);
        }
    }

    // AND A MIXED TABLE, because every case above sets one level across all three
    // seats - so a buildDeal() that pushed an observer per SEAT rather than per
    // watching seat would pass all nine. Both kinds that watch sit at it, each
    // right after a seat that does not.
    newgame::Config mixed = watchingConfig(4);
    mixed.botLevels = { newgame::BotLevel::LowRisk,
                        newgame::BotLevel::ReckonerGamma,
                        newgame::BotLevel::Ducking,
                        newgame::BotLevel::Cyborg };

    const GameDeal deal = newgame::buildDeal(mixed);

    // In seat order, which is the order buildDeal() collects them in.
    REQUIRE(deal.observers.size() == 2);
    REQUIRE(dynamic_cast<ReckonerStrategy*>(deal.observers.at(0)) != nullptr);
    REQUIRE(dynamic_cast<CyborgStrategy*>(deal.observers.at(1)) != nullptr);
}

TEST_CASE("Every combo row is the level its index names", "[new-game]")
{
    // THE RUNTIME HALF OF NewGameDialog.h's static_assert, and the widget half it
    // cannot see: rebuildOpponentRows() writes a row with
    // setCurrentIndex(static_cast<int>(level)) and refresh() reads it back with
    // BotLevels[currentIndex()], so the enum's value, the array's order and the
    // combo's rows are one fact in three places. A level appended to the enum but
    // not to the array builds fine and offers a shorter menu than it has levels.
    for(std::size_t i = 0 ; i < std::size(newgame::BotLevels) ; i++)
        REQUIRE(static_cast<std::size_t>(newgame::BotLevels[i]) == i);

    NewGameDialog dialog(newgame::defaults());

    QComboBox* level = dialog.findChild<QComboBox*>("botLevel0");
    REQUIRE(level != nullptr);
    REQUIRE(level->count() == static_cast<int>(std::size(newgame::BotLevels)));

    // Every row reads back as the level whose name it shows, which is the drift
    // the index arithmetic above cannot catch on its own.
    for(int i = 0 ; i < level->count() ; i++)
    {
        INFO("row " << i);
        REQUIRE(level->itemText(i) == newgame::nameOf(newgame::BotLevels[i]));
    }
}

TEST_CASE("The dialog opens on a Reckoner at Beta, the Cyborg and LowRisk", "[new-game]")
{
    // THE TERMINAL'S LINE-UPS BUT FOR ONE PRESET, which is the whole of what
    // defaults() and defaultLevel() decide: its quick game with Beta where the
    // terminal seats Gamma - see defaultLevel() for why. Pinned here because
    // nothing else would notice it drift - every other case that opens on
    // defaults() overrides its levels or only counts them.
    const newgame::Config config = newgame::defaults();

    REQUIRE_FALSE(config.spectator);
    REQUIRE(config.botLevels == std::vector<newgame::BotLevel>{ newgame::BotLevel::ReckonerBeta,
                                                                newgame::BotLevel::Cyborg,
                                                                newgame::BotLevel::LowRisk });
    REQUIRE(newgame::roundCount(config) == 26);

    // AND A FOURTH ROW MAKES THE DEMO'S FOUR BOTS, the Reckoner there at Gamma
    // since the first seat took Beta - rather than a second copy of the first
    // seat.
    NewGameDialog dialog(config);

    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    count->setValue(4);

    REQUIRE(dialog.config().botLevels.size() == 4);
    REQUIRE(dialog.config().botLevels.at(3) == newgame::BotLevel::ReckonerGamma);
}

TEST_CASE("The dialog's rows follow the opponent count", "[new-game]")
{
    // The widget half, constructible because TestApp is a QApplication on the
    // offscreen platform. Section 13: changing the count "adds a fourth row with
    // a pre-filled default name; it does not open another dialog".
    NewGameDialog dialog(newgame::defaults());

    // AGAINST defaults() RATHER THAN A LITERAL, because this case is about the
    // rows FOLLOWING THE SPINNER and not about how many the dialog opens with.
    // The default table has changed before and will again; a literal here
    // would make each change come and find this line for no reason.
    REQUIRE(dialog.config().bots.size() == newgame::defaults().bots.size());

    // BY NAME, not by position in findChildren(). Every QSpinBox owns an
    // editable QLineEdit of its own, so a positional lookup over either list
    // finds a control the dialog did not create - which is how this file's first
    // run typed a bot name into the seed box and asserted nothing.
    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    count->setValue(5);
    REQUIRE(dialog.config().bots.size() == 5);

    // AND THE NAMES ALREADY THERE SURVIVE, which is the half a rebuild would
    // lose - and the reason rebuildOpponentRows() adds and removes rather than
    // clearing.
    const QStringList before = namesOf(dialog.config().bots);

    count->setValue(4);
    const QStringList after = namesOf(dialog.config().bots);

    REQUIRE(after.size() == 4);
    for(int i = 0 ; i < after.size() ; i++)
        REQUIRE(after.at(i) == before.at(i));

    // No row is left blank, which would disable Deal for a reason the player did
    // not cause.
    for(const QString& name : namesOf(dialog.config().bots))
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
    REQUIRE(config.bots.size() == newgame::MaximumSeats);

    for(const QString& name : namesOf(config.bots))
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
    // THE NAMES ARE DRAWN AFRESH since the owner's call of 2026-10-23: each row
    // keeps its gender and its level, and is given a new name to match.
    newgame::Config config = newgame::defaults();

    config.bots = { { "Zoltan", Gender::Female }, { "Yvette", Gender::Male } };
    config.botLevels = { newgame::BotLevel::Ducking, newgame::BotLevel::Random };

    NewGameDialog dialog(config);

    const newgame::Config offered = dialog.config();

    REQUIRE(offered.botLevels == config.botLevels);
    REQUIRE(offered.bots.size() == 2);

    for(std::size_t i = 0 ; i < offered.bots.size() ; i++)
    {
        INFO("row " << i << ": " << offered.bots[i].name);
        REQUIRE(offered.bots[i].gender == config.bots[i].gender);
        REQUIRE(drawnFor(offered, i));
    }

    // The rows themselves, not only what config() computes from them - the
    // player has to SEE the game they are being offered.
    QComboBox* firstGender = dialog.findChild<QComboBox*>("botGender0");
    QComboBox* firstLevel = dialog.findChild<QComboBox*>("botLevel0");
    REQUIRE(firstGender != nullptr);
    REQUIRE(firstLevel != nullptr);

    REQUIRE(firstGender->currentIndex() == static_cast<int>(Gender::Female));
    REQUIRE(newgame::BotLevels[firstLevel->currentIndex()] == newgame::BotLevel::Ducking);

    // A SEAT THE CONFIG NEVER HAD still gets a name rather than a blank, which
    // would disable Deal for a reason the player did not cause.
    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    count->setValue(3);

    const newgame::Config grown = dialog.config();
    REQUIRE(grown.bots.size() == 3);
    REQUIRE(grown.bots.at(0).name == offered.bots.at(0).name);
    REQUIRE(drawnFor(grown, 2));
    REQUIRE_FALSE(newgame::firstError(grown).has_value());
}

TEST_CASE("The default table's bots are named to match their genders", "[new-game]")
{
    // CHOSEN, NEVER READ OFF THE NAME: each bot is a man or a woman at random,
    // and its name is drawn to match. You start as Male, as "Player" ("Jucător")
    // is masculine.
    for(int open = 0 ; open < 20 ; open++)
    {
        const newgame::Config config = newgame::defaults();

        REQUIRE(config.human.gender == Gender::Male);
        REQUIRE(config.bots.size() == 3);

        QStringList names;

        for(std::size_t i = 0 ; i < config.bots.size() ; i++)
        {
            INFO(config.bots[i].name);
            REQUIRE(drawnFor(config, i));
            names.push_back(QString::fromStdString(config.bots[i].name));
        }

        REQUIRE(names.removeDuplicates() == 0);
        REQUIRE_FALSE(newgame::firstError(config).has_value());
    }
}

TEST_CASE("A Romanian game draws Romanian names and an English one English", "[new-game]")
{
    // IN THE LIST FOR THAT LANGUAGE (or for any): a name can be in both, as Laura
    // is.
    const auto listedIn = [](const PlayerProfile& bot, language::Language language)
    {
        for(const playernames::Name& entry : playernames::catalogue())
            if(entry.name == QString::fromStdString(bot.name) && (!entry.language || *entry.language == language))
                return true;

        return false;
    };

    for(int open = 0 ; open < 10 ; open++)
    {
        {
            const RunningIn romanian(language::Language::Romanian);

            for(const PlayerProfile& bot : newgame::defaults().bots)
            {
                INFO(bot.name);
                REQUIRE(listedIn(bot, language::Language::Romanian));
            }
        }

        for(const PlayerProfile& bot : newgame::defaults().bots)
        {
            INFO(bot.name);
            REQUIRE(listedIn(bot, language::Language::English));
        }
    }
}

TEST_CASE("The dialog shows and returns every seat's gender", "[new-game]")
{
    newgame::Config config = newgame::defaults();
    config.human.gender = Gender::Female;
    config.bots = { { "Zoltan", Gender::Male }, { "Yvette", Gender::Female } };
    config.botLevels = { newgame::BotLevel::Ducking, newgame::BotLevel::Random };

    NewGameDialog dialog(config);

    QComboBox* yours = dialog.findChild<QComboBox*>("humanGender");
    QComboBox* first = dialog.findChild<QComboBox*>("botGender0");
    QComboBox* second = dialog.findChild<QComboBox*>("botGender1");
    REQUIRE(yours != nullptr);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    // What the player sees: Male is the first item, Female the second.
    REQUIRE(yours->currentIndex() == 1);
    REQUIRE(first->currentIndex() == 0);
    REQUIRE(second->currentIndex() == 1);

    REQUIRE(dialog.config().human == config.human);
    REQUIRE(dialog.config().bots.at(0).gender == Gender::Male);
    REQUIRE(dialog.config().bots.at(1).gender == Gender::Female);

    // And a choice made in the dialog is what it deals - with a name to match,
    // drawn again for a row that still showed the one drawn for it.
    first->setCurrentIndex(1);
    yours->setCurrentIndex(0);

    REQUIRE(dialog.config().bots.at(0).gender == Gender::Female);
    REQUIRE(drawnFor(dialog.config(), 0));
    REQUIRE(dialog.config().human.gender == Gender::Male);
}

TEST_CASE("A row's gender outlives the count changing and new rows are named to match", "[new-game]")
{
    newgame::Config config = newgame::defaults();
    config.bots = { { "A", Gender::Female }, { "B", Gender::Male }, { "C", Gender::Female } };

    NewGameDialog dialog(config);

    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    // A ROW ADDED PAST THE CONFIG is a man or a woman at random, named to match.
    count->setValue(5);

    for(std::size_t i = 0 ; i < 5 ; i++)
    {
        INFO("row " << i << ": " << dialog.config().bots.at(i).name);
        REQUIRE(drawnFor(dialog.config(), i));
    }

    // A gender changed on a row that survives the count going down and back up
    // stays changed.
    QComboBox* first = dialog.findChild<QComboBox*>("botGender0");
    REQUIRE(first != nullptr);
    first->setCurrentIndex(0);

    count->setValue(1);
    count->setValue(3);

    REQUIRE(dialog.config().bots.size() == 3);
    REQUIRE(dialog.config().bots.at(0).gender == Gender::Male);

    // The rows removed and added again take their gender from the config the
    // dialog opened on, as a removed row's level always did, with fresh names.
    REQUIRE(dialog.config().bots.at(1).gender == Gender::Male);
    REQUIRE(dialog.config().bots.at(2).gender == Gender::Female);
    REQUIRE(drawnFor(dialog.config(), 1));
    REQUIRE(drawnFor(dialog.config(), 2));
}

TEST_CASE("A name typed into a row is kept when its gender or level changes", "[new-game]")
{
    // A NAME YOU TYPED IS YOURS: only a name the dialog drew is drawn again.
    NewGameDialog dialog(newgame::defaults());

    QLineEdit* name = dialog.findChild<QLineEdit*>("botName0");
    QComboBox* gender = dialog.findChild<QComboBox*>("botGender0");
    QComboBox* level = dialog.findChild<QComboBox*>("botLevel0");
    REQUIRE(name != nullptr);
    REQUIRE(gender != nullptr);
    REQUIRE(level != nullptr);

    name->setText(QStringLiteral("Zoltan"));

    gender->setCurrentIndex(gender->currentIndex() == 0 ? 1 : 0);
    level->setCurrentIndex(level->currentIndex() == 0 ? 1 : 0);

    REQUIRE(name->text() == QStringLiteral("Zoltan"));
    REQUIRE(dialog.config().bots.at(0).name == "Zoltan");
}

TEST_CASE("Every time New Game opens its bots get fresh names that never repeat", "[new-game]")
{
    // FIVE WOMEN AT ONE TABLE, and you named as one of the list's women: the names
    // drawn are five different ones, none of them yours, and they change from one
    // opening to the next.
    newgame::Config config = newgame::defaults();
    config.human.name = "Emma";
    config.bots = std::vector<PlayerProfile>(5, PlayerProfile{ "x", Gender::Female });
    config.botLevels = std::vector<newgame::BotLevel>(5, newgame::BotLevel::LowRisk);

    std::set<std::string> everDrawn;

    for(int open = 0 ; open < 10 ; open++)
    {
        NewGameDialog dialog(config);
        const newgame::Config drawn = dialog.config();

        QStringList names;

        for(std::size_t i = 0 ; i < drawn.bots.size() ; i++)
        {
            const PlayerProfile& bot = drawn.bots[i];

            INFO("open " << open << ": " << bot.name);
            REQUIRE(drawnFor(drawn, i));
            REQUIRE(QString::fromStdString(bot.name).compare(QStringLiteral("Emma"), Qt::CaseInsensitive) != 0);

            names.push_back(QString::fromStdString(bot.name));
            everDrawn.insert(bot.name);
        }

        REQUIRE(names.removeDuplicates() == 0);
        REQUIRE_FALSE(newgame::firstError(drawn).has_value());
    }

    REQUIRE(everDrawn.size() > 5);
}

TEST_CASE("Your gender is greyed out with your name while watching", "[new-game]")
{
    NewGameDialog dialog(newgame::defaults());

    QRadioButton* watch = dialog.findChild<QRadioButton*>("watching");
    QRadioButton* play = dialog.findChild<QRadioButton*>("playing");
    QComboBox* yours = dialog.findChild<QComboBox*>("humanGender");
    REQUIRE(watch != nullptr);
    REQUIRE(play != nullptr);
    REQUIRE(yours != nullptr);

    REQUIRE(yours->isEnabled());

    watch->setChecked(true);
    REQUIRE_FALSE(yours->isEnabled());

    play->setChecked(true);
    REQUIRE(yours->isEnabled());

    // A dialog opened on a watched game starts with it greyed.
    newgame::Config watched = watchingConfig(4);
    NewGameDialog opened(watched);
    QComboBox* openedGender = opened.findChild<QComboBox*>("humanGender");
    REQUIRE(openedGender != nullptr);
    REQUIRE_FALSE(openedGender->isEnabled());
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
    config.bots = named({ "A", "B", "C", "D", "E", "F" });
    config.botLevels = std::vector<newgame::BotLevel>(6, newgame::BotLevel::Random);

    NewGameDialog dialog(config);

    QSpinBox* count = dialog.findChild<QSpinBox*>("opponents");
    REQUIRE(count != nullptr);

    REQUIRE(count->value() == static_cast<int>(newgame::MaximumSeats));
    REQUIRE(dialog.config().bots.size() == newgame::MaximumSeats);

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

TEST_CASE("The dialog's error shows a typed name as typed, never as markup", "[new-game]")
{
    // PR #42's review, finding #6. The message quotes the name, and a label left at
    // Qt's default read "<b>Ana</b>" as Ana in bold, with the tags gone.
    NewGameDialog dialog(newgame::defaults());

    QLabel* error = dialog.findChild<QLabel*>("dealError");
    QLineEdit* first = dialog.findChild<QLineEdit*>("botName0");
    QLineEdit* second = dialog.findChild<QLineEdit*>("botName1");

    REQUIRE(error != nullptr);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    first->setText(QStringLiteral("<b>Ana</b>"));
    second->setText(QStringLiteral("<b>Ana</b>"));

    REQUIRE(error->textFormat() == Qt::PlainText);
    REQUIRE(error->text().contains(QStringLiteral("<b>Ana</b>")));
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
