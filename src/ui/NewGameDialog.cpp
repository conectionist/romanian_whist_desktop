#include "NewGameDialog.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/strategies/CyborgStrategy.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>
#include <romanian_whist/strategies/RandomCardStrategy.h>
#include <romanian_whist/strategies/ReckonerStrategy.h>
#include <romanian_whist/strategies/reckoner/ReckonerKnobs.h>

#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>

#include <algorithm>
#include <memory>
#include <utility>

using namespace romanian_whist;

namespace
{
// The stock opponent names, in seat order. The terminal's list is three letters
// each because its seat table is fixed-width; this one has room, so these are
// the names phase A's setup and tests/GameFixtures.h both use.
//
// SIX, NOT FIVE, and the difference is a whole seat. Five is the most opponents
// a PLAYING game can have, and a watched one has no human taking a chair - so a
// five-name list runs out on the largest watched table and the sixth row fell
// through to a "Bot 6" placeholder. Found by rendering a six-seat game; nothing
// in the suite was looking at what the rows were pre-filled with.
const QStringList& stockNames()
{
    static const QStringList names = { QStringLiteral("Ana"),
                                       QStringLiteral("Bogdan"),
                                       QStringLiteral("Cristina"),
                                       QStringLiteral("Dan"),
                                       QStringLiteral("Elena"),
                                       QStringLiteral("Florin") };
    return names;
}

// The default level for the nth opponent: THE TERMINAL'S LINE-UPS, but for one
// preset, so a player who knows one client sits down at nearly the same table in
// the other.
//
// THE FIRST THREE ARE ITS QUICK GAME, "you and three bots", and they are what
// defaults() seats: a Reckoner, then the Cyborg, then LowRisk. Its reason is the
// one worth keeping: the table most players judge the bots on seats the two that
// remember the round, and one simple bot for contrast.
//
// THE RECKONER IS AT BETA, AND THAT IS THE DEPARTURE. The terminal seats Gamma,
// the preset its --reckoner-preset flag defaults to, since its quick game names
// no preset of its own. Engine 5.0.0 withdrew the reason to: Gamma "is often no
// stronger than Alpha or Beta". Beta is the preset the engine actually measures -
// STRATEGIES.md has it beating LowRisk clearly and topping the Cyborg's
// tournament table - and it does that on sixty imagined deals a card to Gamma's
// two hundred.
//
// THE FOURTH IS ITS DEMO'S REMAINING SEAT, with the two presets swapped. The
// terminal has no default for a fourth bot at a played table, because its quick
// game stops at three - but its watched demo seats four, these three and a
// Reckoner at Beta. Here that seat is Gamma, so a fourth row, added from the
// spinner at a played table or a watched one, makes the demo's four bots rather
// than a second copy of the first seat. Past four they cycle.
//
// THIS REPLACED THE FOUR RECKONER PRESETS, cheapest first, which was the most
// expensive opening table this client could deal and was chosen for being so.
// Beta samples sixty imagined deals a card where Delta samples four hundred, so
// the opening game is now the cheaper one; and only its Reckoner draws - the
// Cyborg and LowRisk are deterministic - so it is the one seat a fixed deal has
// to seed.
newgame::BotLevel defaultLevel(int index)
{
    static constexpr newgame::BotLevel levels[] = { newgame::BotLevel::ReckonerBeta,
                                                    newgame::BotLevel::Cyborg,
                                                    newgame::BotLevel::LowRisk,
                                                    newgame::BotLevel::ReckonerGamma };

    return levels[static_cast<std::size_t>(index) % std::size(levels)];
}

// A seat's provider, and - for the levels that also WATCH the game - the observer
// that has to reach the engine before it starts. Two fields rather than a return
// value because a Reckoner or Cyborg seat is genuinely two things; see GameDeal
// in engine/GameController.h.
struct BuiltSeat
{
    std::unique_ptr<IMoveProvider> provider;
    IGameObserver* observer = nullptr;
};

// A STRATEGY THAT ALSO WATCHES, AS A SEAT: the engine's own makeReckonerSeat()
// and makeCyborgSeat() minus their one addObserver() line, and the one place the
// two rules below are written down, so the next strategy that watches cannot
// take one of them and not the other.
//
// THE REGISTRATION IS THE LINE LEFT OUT, because it cannot be made here. Both
// engine helpers take a GameEngine& and call addObserver() on it themselves,
// which is right for a caller that owns its engine as a member (the terminal
// client does) and impossible for this one: buildDeal() runs on the GUI thread,
// where the game's engine does not exist yet and will not until
// GameController's run slot constructs it. Kept line-for-line with them
// otherwise, so a fix landing there is findable here.
//
// setPlayerName() AND NOT setSeat(), which is the engine helpers' choice and
// worth keeping for a second reason of our own: the strategy resolves its seat
// from its name inside onGameStarted(), so a seat that is right here would be
// right even for a strategy that was never registered - and the test proving the
// registration happened would be asserting nothing. The name cannot mis-resolve,
// because firstError() rejects duplicates more strictly than the engine does
// (case-insensitively).
template<typename Strategy>
BuiltSeat makeWatchingSeat(std::unique_ptr<Strategy> strategy, const std::string& name)
{
    strategy->setPlayerName(name);

    // TAKEN FROM Strategy*, and taken BEFORE the move, in a named local. Both
    // strategies derive from IStrategy and IGameObserver, so the two base
    // subobjects sit at different addresses within each - upcasting from the
    // wrong one compiles and hands the engine a pointer to the wrong vtable. A
    // dynamic_cast in the test is what pins this down. The template closes the
    // one gap left: handed a unique_ptr<IStrategy>, this line does not compile.
    IGameObserver* observer = strategy.get();

    return BuiltSeat{ std::make_unique<AiMoveProvider>(std::move(strategy)), observer };
}

// A Reckoner seat, from one of the engine's knob tables.
BuiltSeat makeReckoner(const std::string& name,
                       const reckoner::ReckonerKnobs& knobs,
                       std::optional<std::uint32_t> seed)
{
    return makeWatchingSeat(std::make_unique<ReckonerStrategy>(knobs, seed), name);
}

// THE CYBORG'S NAME CARRIES ONE MORE CHECK. A Cyborg whose name matches no seat
// does not play as seat 0 the way a Reckoner does - it throws from
// onGameStarted(), the last thing GameEngine::start() does. buildDeal() cannot
// trip that, because the one `name` it builds per seat is the one both this and
// the SeatSetup are given.
//
// NO KNOBS AND NO SEED, which is not makeReckoner()'s shape and is deliberate
// both times. The knobs are "development tuning, not a difficulty dial"
// (CyborgKnobs.h) - the Cyborg ships as one strength, and the engine's defaults
// are that strength - and it draws nothing, so there is nothing to seed.
BuiltSeat makeCyborg(const std::string& name)
{
    return makeWatchingSeat(std::make_unique<CyborgStrategy>(), name);
}

// One seat, from a level.
//
// TWO STRATEGIES DRAW NOW, NOT ONE. This comment used to read "the seed is
// engaged only for Random, which is the only strategy that draws", and the
// Reckoner made it false: RandomCardStrategy seeds its mt19937 once from the
// seed, and ReckonerStrategy does the same for its deal sampler, its epsilon roll
// and its bid noise. Unseeded, both fall back to std::random_device exactly once,
// at construction. The other four are deterministic and ignore it - the Cyborg
// included, which is why a fixed deal replays it with no seed of its own.
//
// WHAT A SEEDED RECKONER IS AND IS NOT. It replays a game card for card given the
// same deal and the same opponents - the engine's own suite asserts that
// (ReckonerStrategyTests). It is NOT reproducible across a change to any other
// seat, because its rng is consumed per decision and a different card played
// upstream shifts the whole stream. RandomCardStrategy has the same property, and
// it is why the fixtures keep both out of transcript comparisons.
BuiltSeat providerFor(newgame::BotLevel level,
                      const std::string& name,
                      std::optional<std::uint32_t> seed)
{
    switch(level)
    {
    case newgame::BotLevel::Random:
        return BuiltSeat{ std::make_unique<AiMoveProvider>(
                              seed ? std::make_unique<RandomCardStrategy>(*seed)
                                   : std::make_unique<RandomCardStrategy>()),
                          nullptr };

    case newgame::BotLevel::FirstCard:
        return BuiltSeat{ std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>()),
                          nullptr };

    case newgame::BotLevel::LowRisk:
        return BuiltSeat{ std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()),
                          nullptr };

    case newgame::BotLevel::Ducking:
        return BuiltSeat{ std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>()),
                          nullptr };

    case newgame::BotLevel::Cyborg:
        return makeCyborg(name);

    // THE KNOBS OVERLOAD, not the ReckonerPreset one, so the mapping from a row
    // of this client's menu to a table of the engine's numbers is visible at the
    // call site rather than one enum deep.
    case newgame::BotLevel::ReckonerAlpha:
        return makeReckoner(name, reckoner::ReckonerKnobs::alpha(), seed);

    case newgame::BotLevel::ReckonerBeta:
        return makeReckoner(name, reckoner::ReckonerKnobs::beta(), seed);

    case newgame::BotLevel::ReckonerGamma:
        return makeReckoner(name, reckoner::ReckonerKnobs::gamma(), seed);

    case newgame::BotLevel::ReckonerDelta:
        return makeReckoner(name, reckoner::ReckonerKnobs::delta(), seed);
    }

    // Unreachable for any enumerator above, and not an assert: a switch with no
    // default warns when a new level is added, which is the diagnostic wanted -
    // this is only here because a scoped enum can hold a value none of them name.
    return BuiltSeat{ std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>()),
                      nullptr };
}

// Every name in the config, in seat order, for the duplicate check.
QStringList allNames(const newgame::Config& config)
{
    QStringList names;

    if(!config.spectator)
        names.push_back(config.humanName);

    names.append(config.botNames);

    return names;
}

// A knob that is a probability or a weight, as the whole percent a tooltip
// quotes it at.
QString percent(float fraction)
{
    return QString::number(qRound(fraction * 100.0f));
}

// Gamma's tooltip and Delta's, which differ only in their numbers - so one
// sentence, and the two cannot drift apart in wording. What it says in words is
// checked where each is called, since a parameter cannot be static_asserted.
QString searchingTooltip(const reckoner::ReckonerKnobs& knobs)
{
    return QCoreApplication::translate("newgame",
                                       "Weighs %1 imagined deals before each card, never plays at "
                                       "random, and counts %2% of every point an opponent scores "
                                       "against its own, %3% of the leader's.")
        .arg(QString::number(knobs.K_play), percent(knobs.lambdaDefault), percent(knobs.lambdaLeader));
}
}

newgame::Config newgame::defaults()
{
    Config config;

    // NOT "You". SeatItem appends " (you)" to the seated player's plaque, so
    // that default drew "You (you)" - found by rendering a dealt table. Section
    // 13's mockup fills the field with a person's name for the same reason: it
    // is a name to replace, not a label.
    config.humanName = QCoreApplication::translate("newgame", "Player");

    // THREE, as in the terminal's quick game - see defaultLevel() for who they
    // are and why. Three bots plus the human is four seats and 26 rounds, where the four
    // Reckoner presets this replaced made five and 29.
    for(int i = 0 ; i < 3 ; i++)
    {
        config.botNames.push_back(stockNames().at(i));
        config.botLevels.push_back(defaultLevel(i));
    }

    config.botSpeed = DefaultSpeed;

    return config;
}

unsigned int newgame::seatCount(const Config& config)
{
    return static_cast<unsigned int>(config.botNames.size()) + (config.spectator ? 0u : 1u);
}

unsigned int newgame::roundCount(const Config& config)
{
    // Scoreboard::initialize(), read as arithmetic: N one-card rounds, 2..7,
    // N eight-card rounds, 7..2, N one-card rounds - which is 3N + 12 whichever
    // end it starts from - and then the appended forehead and hidden pair.
    return 3u * seatCount(config) + 12u + (config.endWithForeheadAndHidden ? 2u : 0u);
}

QString newgame::summaryText(const Config& config)
{
    const unsigned int rounds = roundCount(config);

    // A RULE OF THUMB OFF THE SLIDER, which is all section 13 asks for - and the
    // honest division of this line, whose round count is exact arithmetic over
    // the engine's schedule and whose minutes are a guess.
    //
    // The schedule runs 1..8 and back, so it averages about four tricks a round
    // at any table size. Each trick costs one beat per seat plus one dwell, and
    // a spectated game paces at the shorter beat (section 4.1).
    const Pacing pacing = pacingFor(config.botSpeed);
    const auto beat = config.spectator ? pacing.spectatorBeat : pacing.beat;

    constexpr int averageTricksPerRound = 4;

    const auto perTrick = beat * seatCount(config) + pacing.trickDwell;
    const auto total = perTrick * averageTricksPerRound * rounds;

    const int minutes = static_cast<int>(
        std::chrono::duration_cast<std::chrono::minutes>(total).count());

    // "%n rounds" AND NOT "%n round(s)", which is the idiom and is wrong here.
    // The parenthesised plural only resolves through a translator; with none
    // loaded QCoreApplication::translate() substitutes %n and returns the source
    // verbatim, so the dialog read "26 round(s)" - found by rendering it. The
    // plural marker is still %n, so lupdate still collects every form Romanian
    // needs; the source form just has to be the one English actually uses, and
    // the count here is never 1 (the smallest table gives 18).
    //
    // TWO SENTENCES, NOT ONE INTERPOLATED, when the estimate rounds to nothing.
    // An "about 0 minutes" is worse than saying nothing, and at the instant end
    // of the slider that is the honest answer.
    if(minutes < 1)
        return QCoreApplication::translate("newgame", "%n rounds", "", static_cast<int>(rounds));

    return QCoreApplication::translate("newgame", "%1 - about %2 minutes")
        .arg(QCoreApplication::translate("newgame", "%n rounds", "", static_cast<int>(rounds)),
             QString::number(minutes));
}

std::optional<QString> newgame::firstError(const Config& config)
{
    const QStringList names = allNames(config);

    // THE COUNT IS CLAMPED RATHER THAN VALIDATED (section 13), so reaching this
    // at all means the spinner was bypassed - which the tests do, and which is
    // the reason to keep the check: buildSetup() is callable without a dialog.
    if(seatCount(config) < MinimumSeats || seatCount(config) > MaximumSeats)
        return QCoreApplication::translate("newgame",
                                           "A game needs between %1 and %2 players.")
            .arg(QString::number(MinimumSeats), QString::number(MaximumSeats));

    for(const QString& name : names)
    {
        // TRIMMED FOR THE TEST, not for the value. A name of spaces is empty to
        // a reader and not to the engine, which would accept it and then print a
        // blank plaque.
        if(name.trimmed().isEmpty())
            return QCoreApplication::translate("newgame", "Every player needs a name.");

        if(name.size() > MaximumNameLength)
            return QCoreApplication::translate("newgame",
                                               "Names are at most %1 characters.")
                .arg(QString::number(MaximumNameLength));
    }

    // CASE-INSENSITIVE, which is stricter than the engine - see the header.
    // Quadratic over at most six names, which is the shape that reports the
    // first offender rather than the fact that there is one.
    for(int i = 0 ; i < names.size() ; i++)
        for(int j = i + 1 ; j < names.size() ; j++)
            if(names.at(i).compare(names.at(j), Qt::CaseInsensitive) == 0)
                return QCoreApplication::translate("newgame",
                                                   "Two players cannot both be called %1.")
                    .arg(names.at(j));

    return std::nullopt;
}

Pacing newgame::pacingFor(int botSpeed)
{
    // MULTIPLES OF Pacing{}'s OWN DEFAULTS rather than five hand-written triples,
    // so the pacing the client was tuned at in phases A to C stays the middle
    // position and stays the single source of those three numbers.
    static constexpr double factors[] = { 2.0, 1.5, 1.0, 0.5, 0.0 };

    const int clamped = std::clamp(botSpeed, SlowestSpeed, FastestSpeed);
    const double factor = factors[static_cast<std::size_t>(clamped)];

    const Pacing defaults;

    const auto scale = [factor](std::chrono::milliseconds base)
    {
        return std::chrono::milliseconds(static_cast<long long>(base.count() * factor));
    };

    return Pacing{ scale(defaults.beat),
                   scale(defaults.spectatorBeat),
                   scale(defaults.trickDwell) };
}

std::optional<Seat> newgame::humanSeatOf(const Config& config)
{
    if(config.spectator)
        return std::nullopt;

    return Seat{ 0u };
}

bool newgame::isReckoner(BotLevel level)
{
    switch(level)
    {
    case BotLevel::ReckonerAlpha:
    case BotLevel::ReckonerBeta:
    case BotLevel::ReckonerGamma:
    case BotLevel::ReckonerDelta:
        return true;

    case BotLevel::Random:
    case BotLevel::FirstCard:
    case BotLevel::LowRisk:
    case BotLevel::Ducking:
    // Watches, but does not sample - which is why isObserver() names it on its
    // own rather than taking it from here.
    case BotLevel::Cyborg:
        return false;
    }

    // Every enumerator is named above, so this is the same unreachable tail
    // providerFor() carries and for the same reason: no default:, so a level added
    // without a decision here is a warning rather than a silent false.
    return false;
}

// FROM isReckoner() RATHER THAN A SWITCH OF ITS OWN, so a new level is decided
// once - in isReckoner(), whose switch warns until it is - and not twice in two
// switches that could each be satisfied and still disagree. A level that watches
// and is left out of this line is not silent either: the observer-count case in
// NewGameTests compares it against the observers buildDeal() actually built.
bool newgame::isObserver(BotLevel level)
{
    return isReckoner(level) || level == BotLevel::Cyborg;
}

GameDeal newgame::buildDeal(const Config& config)
{
    GameDeal deal;
    GameSetup& setup = deal.setup;

    // FIRST, and with a null provider on purpose - GameController::start() fills
    // exactly this seat, addressed by humanSeatOf() above. Any OTHER null seat
    // stays null and GameEngine::start() rejects it BY NAME, which is the
    // behaviour worth preserving.
    if(!config.spectator)
        setup.seats.push_back(SeatSetup{ config.humanName.toStdString(), nullptr });

    for(int i = 0 ; i < config.botNames.size() ; i++)
    {
        // The seed varies per seat, so two bots that draw at one table do not play
        // the same stream as each other. It is indexed by the SEAT and not by the
        // bot, so a watched game and a played one do not seed the same bot alike -
        // which is correct, because they are not the same table.
        const std::optional<std::uint32_t> seed =
            config.shuffleSeed
                ? std::optional<std::uint32_t>(*config.shuffleSeed
                                               + static_cast<std::uint32_t>(setup.seats.size()))
                : std::nullopt;

        const std::string name = config.botNames.at(i).toStdString();

        BuiltSeat built = providerFor(config.botLevels.at(static_cast<std::size_t>(i)), name, seed);

        // COLLECTED BEFORE THE PROVIDER IS MOVED AWAY, and only when there is one
        // - an observer per seat that watches, nothing for the seats that do not.
        // runGame() asserts the list holds no nulls, which is this check's other
        // half.
        if(built.observer != nullptr)
            deal.observers.push_back(built.observer);

        setup.seats.push_back(SeatSetup{ name, std::move(built.provider) });
    }

    setup.structure = config.structure;
    setup.endWithForeheadAndHidden = config.endWithForeheadAndHidden;
    setup.all1GamesAreForehead = config.all1GamesAreForehead;
    setup.shuffleSeed = config.shuffleSeed;

    return deal;
}

QString newgame::nameOf(BotLevel level)
{
    switch(level)
    {
    case BotLevel::Random:    return QCoreApplication::translate("newgame", "Random");
    case BotLevel::FirstCard: return QCoreApplication::translate("newgame", "First card");
    case BotLevel::LowRisk:   return QCoreApplication::translate("newgame", "Low risk");
    case BotLevel::Ducking:   return QCoreApplication::translate("newgame", "Ducking");

    // THE TERMINAL WIZARD'S STRINGS VERBATIM (section 21.4), parenthesised preset
    // and all, so a player who knows one client reads the other.
    case BotLevel::Cyborg:
        return QCoreApplication::translate("newgame", "Cyborg");
    case BotLevel::ReckonerAlpha:
        return QCoreApplication::translate("newgame", "Reckoner (Alpha)");
    case BotLevel::ReckonerBeta:
        return QCoreApplication::translate("newgame", "Reckoner (Beta)");
    case BotLevel::ReckonerGamma:
        return QCoreApplication::translate("newgame", "Reckoner (Gamma)");
    case BotLevel::ReckonerDelta:
        return QCoreApplication::translate("newgame", "Reckoner (Delta)");
    }

    return QString();
}

QString newgame::tooltipFor(BotLevel level)
{
    // The engine's STRATEGIES.md lead table, one row each, so the choice means
    // something to a player who has never read the source (section 13). The
    // Cyborg's is its row there, bids then play, like the four before it.
    //
    // EXCEPT THE RECKONER'S FOUR, WHICH ARE READ OFF THEIR KNOBS. Engine 5.0.0
    // gave STRATEGIES.md a row per preset, and that is not the source either: it
    // says Alpha remembers only the current trick and Delta solves the last two
    // tricks exactly, and the engine reads neither knob behind those - fullMemory
    // and endgameN appear in ReckonerKnobs and nowhere else, and
    // RolloutEngine::endgameSearch() is declared and never written. These four
    // tooltips used to make the same two claims. So every number below comes
    // from the table providerFor() builds the seat with, and a retune moves the
    // tooltip with it.
    //
    // ONE SHAPE FOR ALL FOUR: how many deals it imagines, how often it plays at
    // random, and how much of an opponent's score it counts against its own. The
    // old shape was "the one thing this preset does that the one below does not",
    // which is the ladder engine 5.0.0 withdrew - and it had Gamma alone playing
    // against the leader's bid, when Beta weighs every opponent's points too.
    //
    // WHAT A SENTENCE SAYS IN WORDS IS PINNED BY A static_assert BESIDE IT, since
    // no argument can carry "never" or "no deals". A retune that makes one false
    // stops this file compiling rather than shipping a tooltip that is wrong.
    switch(level)
    {
    case BotLevel::Random:
        return QCoreApplication::translate("newgame",
                                           "Bids a random legal number and plays a random legal card.");
    case BotLevel::FirstCard:
        return QCoreApplication::translate("newgame",
                                           "Bids 0, or 1 when 0 is barred, and plays the first legal "
                                           "card in its hand.");
    case BotLevel::LowRisk:
        return QCoreApplication::translate("newgame",
                                           "Bids the tricks its hand will take anyway, then wins "
                                           "cheaply while it owes them.");
    case BotLevel::Ducking:
        return QCoreApplication::translate("newgame",
                                           "Bids 0 come what may, and sheds its highest cards "
                                           "whenever they cannot win.");
    case BotLevel::Cyborg:
        return QCoreApplication::translate("newgame",
                                           "Bids the count with the best expected score, from exact "
                                           "odds on every card, and re-plans every trick which cards "
                                           "win and which go.");

    // NO SABOTAGE CLAUSE, and none is owed: with no deals imagined there are no
    // rollouts for the lambdas to weigh, so Alpha's are moot whatever they are.
    //
    // AND NO BID CLAUSE, though Alpha's bidNoise is 0.20. RolloutEngine::chooseBid()
    // returns before its noise when K_bid is 0, which the first assert pins - so
    // the knob never nudges a bid, and seeded games play the same with it at 0.20
    // and at 0 (engine issue #36). Quoting it is how this tooltip first went wrong.
    case BotLevel::ReckonerAlpha:
    {
        constexpr reckoner::ReckonerKnobs knobs = reckoner::ReckonerKnobs::alpha();
        static_assert(knobs.K_play == 0 && knobs.K_bid == 0,
                      "the tooltip says it imagines no deals, and has no bid noise to quote");
        static_assert(knobs.epsilon > 0.0f,
                      "the tooltip says it plays a random card");

        return QCoreApplication::translate("newgame",
                                           "Imagines no deals and plays by rule, but plays a random "
                                           "legal card %1% of the time.")
            .arg(percent(knobs.epsilon));
    }

    // NO LEADER CLAUSE, because Beta weighs the score leader like everyone else.
    case BotLevel::ReckonerBeta:
    {
        constexpr reckoner::ReckonerKnobs knobs = reckoner::ReckonerKnobs::beta();
        static_assert(knobs.K_play > 0,
                      "the tooltip says it imagines deals, and weighs opponents in them");
        static_assert(knobs.lambdaLeader == knobs.lambdaDefault,
                      "the tooltip gives no leader weight");
        static_assert(knobs.epsilon > 0.0f && knobs.bidNoise > 0.0f,
                      "the tooltip says it plays at random");
        static_assert(knobs.K_bid > 0,
                      "the tooltip says it nudges its bid, which chooseBid() does only after sampling");

        // "BY UP TO ONE", not "one off": bid noise, when it fires, adds -1, 0 or +1.
        return QCoreApplication::translate("newgame",
                                           "Weighs %1 imagined deals before each card and counts %2% "
                                           "of every point an opponent scores against its own, but "
                                           "plays a random legal card %3% of the time and nudges its "
                                           "bid by up to one %4% of the time.")
            .arg(QString::number(knobs.K_play), percent(knobs.lambdaDefault),
                 percent(knobs.epsilon), percent(knobs.bidNoise));
    }

    case BotLevel::ReckonerGamma:
    {
        constexpr reckoner::ReckonerKnobs knobs = reckoner::ReckonerKnobs::gamma();
        static_assert(knobs.K_play > 0,
                      "the tooltip says it imagines deals, and weighs opponents in them");
        static_assert(knobs.epsilon == 0.0f && knobs.bidNoise == 0.0f,
                      "the tooltip says it never plays at random");

        return searchingTooltip(knobs);
    }

    case BotLevel::ReckonerDelta:
    {
        constexpr reckoner::ReckonerKnobs knobs = reckoner::ReckonerKnobs::delta();
        static_assert(knobs.K_play > 0,
                      "the tooltip says it imagines deals, and weighs opponents in them");
        static_assert(knobs.epsilon == 0.0f && knobs.bidNoise == 0.0f,
                      "the tooltip says it never plays at random");

        return searchingTooltip(knobs);
    }
    }

    return QString();
}

// ---- the widget ----

NewGameDialog::NewGameDialog(const newgame::Config& _initial, QWidget* parent)
    : QDialog(parent), current(_initial), initial(_initial)
{
    setWindowTitle(tr("New Game"));

    // MODAL, and safe to be. Section 4.4's warning is about a dialog that is up
    // while the game thread is parked in a prompt, which takes the close button
    // with it for an unbounded time; this one is up while no game is running at
    // all, so there is nothing to abandon and nothing to hang on.
    setModal(true);

    QVBoxLayout* stack = new QVBoxLayout(this);

    watching = new QRadioButton(tr("I'm watching"), this);
    watching->setObjectName("watching");

    playing = new QRadioButton(tr("I'm playing"), this);
    playing->setObjectName("playing");

    // TWO GROUPS, DECLARED, and not left to Qt's auto-exclusion. QRadioButton
    // groups itself with every sibling under the same PARENT, and all four
    // radios here are children of the dialog - so without this, checking
    // "1-8-1" unchecks "I'm playing" and the dialog quietly forgets whether
    // anyone is sitting down.
    //
    // Found by a test, and it is worth knowing which one: the case that flips to
    // watching and reads the opponent spinner's floor back. Nothing about the
    // dialog LOOKS wrong while this is broken - all four buttons draw, and three
    // of the four states are reachable.
    whoGroup = new QButtonGroup(this);
    whoGroup->addButton(playing);
    whoGroup->addButton(watching);

    QHBoxLayout* who = new QHBoxLayout;
    who->addWidget(playing);
    who->addWidget(watching);
    who->addStretch(1);
    stack->addLayout(who);

    QFormLayout* top = new QFormLayout;
    humanName = new QLineEdit(this);
    humanName->setMaxLength(newgame::MaximumNameLength);

    // NAMED, and every control a test reaches for below is. findChildren<QLineEdit*>
    // does not return the line edits this dialog created - it returns those AND
    // the one inside every QSpinBox, which is a real editable QLineEdit child.
    // A test indexing that list types a bot name into the seed box and asserts
    // nothing. Names cost a line each and make the lookup say what it means.
    humanName->setObjectName("humanName");
    humanNameLabel = new QLabel(tr("Your name"), this);
    top->addRow(humanNameLabel, humanName);

    opponents = new QSpinBox(this);
    opponents->setObjectName("opponents");
    opponentsHint = new QLabel(this);

    QHBoxLayout* countRow = new QHBoxLayout;
    countRow->addWidget(opponents);
    countRow->addWidget(opponentsHint);
    countRow->addStretch(1);
    top->addRow(new QLabel(tr("Opponents"), this), countRow);
    stack->addLayout(top);

    opponentRows = new QFormLayout;
    stack->addLayout(opponentRows);

    // ---- shape ----

    QFrame* shapeRule = new QFrame(this);
    shapeRule->setFrameShape(QFrame::HLine);
    stack->addWidget(new QLabel(tr("Shape of the game"), this));
    stack->addWidget(shapeRule);

    structure181 = new QRadioButton(tr("1-8-1 - counts up from one card and back down"), this);
    structure181->setObjectName("structure181");
    structure818 = new QRadioButton(tr("8-1-8 - the other way round"), this);
    structure818->setObjectName("structure818");

    // The second group - see whoGroup above for what sharing one costs.
    structureGroup = new QButtonGroup(this);
    structureGroup->addButton(structure181);
    structureGroup->addButton(structure818);

    stack->addWidget(structure181);
    stack->addWidget(structure818);

    foreheadAndHidden = new QCheckBox(tr("End with a forehead round and a hidden round"), this);
    foreheadAndHidden->setObjectName("foreheadAndHidden");
    allOnesForehead = new QCheckBox(tr("Play every one-card round forehead"), this);
    allOnesForehead->setObjectName("allOnesForehead");
    stack->addWidget(foreheadAndHidden);
    stack->addWidget(allOnesForehead);

    summary = new QLabel(this);
    summary->setAlignment(Qt::AlignCenter);
    stack->addWidget(summary);

    // ---- pace ----

    QFrame* paceRule = new QFrame(this);
    paceRule->setFrameShape(QFrame::HLine);
    stack->addWidget(new QLabel(tr("Pace"), this));
    stack->addWidget(paceRule);

    // BOT SPEED ONLY. Section 13's mockup also offers "Wait for me between
    // tricks", which is phase E's: it turns pace() into a wait on a button
    // rather than on a duration, and that is the machinery D1 built one caller
    // of and no more. A disabled checkbox would be a promise, so there is none.
    speed = new QSlider(Qt::Horizontal, this);
    speed->setObjectName("speed");
    speed->setRange(newgame::SlowestSpeed, newgame::FastestSpeed);
    speed->setPageStep(1);
    speed->setTickPosition(QSlider::TicksBelow);

    QHBoxLayout* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(tr("Bot speed"), this));
    speedRow->addWidget(new QLabel(tr("Slow"), this));
    speedRow->addWidget(speed, 1);
    speedRow->addWidget(new QLabel(tr("Instant"), this));
    stack->addLayout(speedRow);

    // SECTION 16.1's SHEET, held or not. Its button says Continue in every round
    // rather than "Deal round 8", because on the final round there is nothing
    // left to deal - see RoundSheet.h.
    //
    // NOTE WHAT THE SLIDER ABOVE DOES TO THE OTHER SETTING, which is worth
    // stating rather than fixing: at Instant the trick dwell is zero, so with
    // this unticked the sheet flashes and is gone. That is what Instant means,
    // and a player who wants to read the sheet has this box.
    waitEachRound = new QCheckBox(tr("Wait for me after each round"), this);
    waitEachRound->setObjectName("waitEachRound");
    stack->addWidget(waitEachRound);

    fixedDeal = new QCheckBox(tr("Fixed deal, seed"), this);
    fixedDeal->setObjectName("fixedDeal");
    seed = new QSpinBox(this);
    seed->setObjectName("seed");

    // A PLAIN int RANGE over a uint32 field. shuffleSeed is 32-bit and a
    // QSpinBox is not, and a player typing a seed wants a number they can read
    // back off a bug report rather than the whole space.
    seed->setRange(0, 999999);
    seed->setValue(42);

    QHBoxLayout* seedRow = new QHBoxLayout;
    seedRow->addWidget(fixedDeal);
    seedRow->addWidget(seed);
    seedRow->addStretch(1);
    stack->addLayout(seedRow);

    // ---- the verdict ----

    error = new QLabel(this);
    error->setWordWrap(true);
    stack->addWidget(error);

    QDialogButtonBox* buttons = new QDialogButtonBox(this);
    buttons->addButton(QDialogButtonBox::Cancel);
    deal = buttons->addButton(tr("Deal"), QDialogButtonBox::AcceptRole);
    deal->setObjectName("deal");
    deal->setDefault(true);
    stack->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // ---- the initial state, then the wiring ----
    //
    // IN THAT ORDER, so setting the controls up does not run refresh() against
    // a half-built dialog. Every setter below fires its signal.
    playing->setChecked(!current.spectator);
    watching->setChecked(current.spectator);
    humanName->setText(current.humanName);
    structure181->setChecked(current.structure == GameStructure::S_181);
    structure818->setChecked(current.structure == GameStructure::S_818);
    foreheadAndHidden->setChecked(current.endWithForeheadAndHidden);
    allOnesForehead->setChecked(current.all1GamesAreForehead);
    speed->setValue(current.botSpeed);
    waitEachRound->setChecked(current.waitAfterEachRound);
    fixedDeal->setChecked(current.shuffleSeed.has_value());

    if(current.shuffleSeed)
        seed->setValue(static_cast<int>(*current.shuffleSeed % 1000000u));

    // FROM THE CONFIG, not from the playing bounds regardless. This line set the
    // range for a PLAYED game whatever was being opened, and then set the value
    // against it: a watched game of six bots - which is legal, and the largest
    // table there is - was clamped to five on the way in, silently changing the
    // settings the player is being shown as the last game's. In the other
    // direction it let a watched game read 1, which firstError() then has to
    // reject, breaking section 13's own rule that a spinner which cannot reach
    // an illegal value beats a message explaining one.
    //
    // The range moved correctly on every LATER change, because the radio's
    // handler below does this properly - and that handler has not been connected
    // yet when this runs, so the initial state was the one path with nothing
    // maintaining it.
    applyOpponentRange(!current.spectator);
    opponents->setValue(static_cast<int>(current.botNames.size()));

    const auto onChange = [this]{ refresh(); };

    connect(playing, &QRadioButton::toggled, this, [this](bool on)
    {
        applyOpponentRange(on);
        humanNameLabel->setEnabled(on);
        humanName->setEnabled(on);
        refresh();
    });

    connect(humanName, &QLineEdit::textChanged, this, onChange);
    connect(opponents, &QSpinBox::valueChanged, this, [this]{ rebuildOpponentRows(); refresh(); });
    connect(structure181, &QRadioButton::toggled, this, onChange);
    connect(foreheadAndHidden, &QCheckBox::toggled, this, onChange);
    connect(allOnesForehead, &QCheckBox::toggled, this, onChange);
    connect(speed, &QSlider::valueChanged, this, onChange);
    connect(waitEachRound, &QCheckBox::toggled, this, onChange);
    connect(fixedDeal, &QCheckBox::toggled, this, [this](bool on){ seed->setEnabled(on); refresh(); });
    connect(seed, &QSpinBox::valueChanged, this, onChange);

    seed->setEnabled(fixedDeal->isChecked());
    humanNameLabel->setEnabled(!current.spectator);
    humanName->setEnabled(!current.spectator);

    rebuildOpponentRows();
    refresh();
}

newgame::Config NewGameDialog::config() const
{
    return current;
}

void NewGameDialog::applyOpponentRange(bool playing)
{
    // A watched game has no human seat, so it needs two bots at minimum and may
    // have six; a played one already has the human, so one bot is enough and
    // five is the ceiling. Section 13: a spinner that cannot reach an illegal
    // value beats an error message explaining one.
    opponents->setRange(playing ? 1 : static_cast<int>(newgame::MinimumSeats),
                        static_cast<int>(newgame::MaximumSeats) - (playing ? 1 : 0));
}

void NewGameDialog::rebuildOpponentRows()
{
    const int wanted = opponents->value();

    // NAMES AND LEVELS ALREADY TYPED SURVIVE, which is the whole reason this
    // adds and removes rather than rebuilding: section 13 says changing the
    // count "adds a fourth row with a pre-filled default name; it does not open
    // another dialog", and a rebuild would also throw away the three above it.
    while(static_cast<int>(rows.size()) > wanted)
    {
        Row row = rows.back();
        rows.pop_back();

        opponentRows->removeRow(row.number);   // deletes the row's widgets
    }

    while(static_cast<int>(rows.size()) < wanted)
    {
        const int index = static_cast<int>(rows.size());

        Row row;
        row.number = new QLabel(tr("%1.").arg(index + 1), this);

        row.name = new QLineEdit(this);
        row.name->setMaxLength(newgame::MaximumNameLength);
        row.name->setObjectName(QStringLiteral("botName%1").arg(index));

        // THE OPPONENT THIS SEAT HAD WHEN THE DIALOG OPENED, if it had one. The
        // dialog is opened on the last game's config so that play again
        // pre-fills with its settings (section 16.2), and every control did that
        // except these: the rows were built from the stock list regardless, and
        // refresh() then wrote those defaults back over the config that was
        // handed in. So reopening a game against Ana, Bogdan and Cristi on three
        // different levels offered - and then dealt - the stock three.
        //
        // FROM `initial` AND NOT FROM `current`, which cannot answer: refresh()
        // rebuilds current's bot lists from the rows on every keystroke, so it
        // describes the rows that exist rather than the game that was asked for.
        //
        // A PRE-FILLED DEFAULT OTHERWISE, so no row starts blank - a blank one
        // disables Deal for a reason the player did not cause. The stock list
        // holds one name per seat at the largest table there is, so its fallback
        // is unreachable; it is one line against the day MaximumSeats moves.
        //
        // IT DOES NOT AVOID THE HUMAN'S NAME, unlike the terminal's quick start,
        // and that is section 13's own answer: a player who types "Ana" gets the
        // duplicate marked and Deal disabled, which names the collision instead
        // of silently renaming a seat under them.
        // TWO SPELLINGS OF THE SAME SEAT, because the two lists are different
        // containers: botNames is a QStringList and sizes in qsizetype, botLevels
        // a std::vector and sizes in size_t. Comparing one index against both
        // signednesses is what -Wsign-compare is for.
        const std::size_t seat = static_cast<std::size_t>(index);

        if(index < initial.botNames.size())
            row.name->setText(initial.botNames.at(index));
        else
            row.name->setText(index < stockNames().size() ? stockNames().at(index)
                                                          : tr("Bot %1").arg(index + 1));

        row.level = new QComboBox(this);
        row.level->setObjectName(QStringLiteral("botLevel%1").arg(index));

        for(const newgame::BotLevel level : newgame::BotLevels)
        {
            row.level->addItem(newgame::nameOf(level));
            row.level->setItemData(row.level->count() - 1,
                                   newgame::tooltipFor(level), Qt::ToolTipRole);
        }

        // AND THE LEVEL WITH THE NAME, from the same seat of the same config -
        // the two are one setting per opponent, and a row that kept the name and
        // reset the level would be the same defect wearing half a fix.
        //
        // The index is the enum's own value, which is what BotLevels is ordered
        // by and what defaultLevel() returns.
        row.level->setCurrentIndex(seat < initial.botLevels.size()
                                       ? static_cast<int>(initial.botLevels.at(seat))
                                       : static_cast<int>(defaultLevel(index)));

        connect(row.name, &QLineEdit::textChanged, this, [this]{ refresh(); });
        connect(row.level, &QComboBox::currentIndexChanged, this, [this]{ refresh(); });

        QHBoxLayout* line = new QHBoxLayout;
        line->addWidget(row.name);
        line->addWidget(new QLabel(tr("plays"), this));
        line->addWidget(row.level);

        opponentRows->addRow(row.number, line);
        rows.push_back(row);
    }
}

void NewGameDialog::refresh()
{
    current.spectator = watching->isChecked();
    current.humanName = humanName->text();

    current.botNames.clear();
    current.botLevels.clear();

    for(const Row& row : rows)
    {
        current.botNames.push_back(row.name->text());
        current.botLevels.push_back(newgame::BotLevels[row.level->currentIndex()]);
    }

    current.structure = structure181->isChecked() ? GameStructure::S_181 : GameStructure::S_818;
    current.endWithForeheadAndHidden = foreheadAndHidden->isChecked();
    current.all1GamesAreForehead = allOnesForehead->isChecked();
    current.botSpeed = speed->value();
    current.waitAfterEachRound = waitEachRound->isChecked();
    current.shuffleSeed = fixedDeal->isChecked()
                              ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(seed->value()))
                              : std::nullopt;

    opponentsHint->setText(tr("(%1-%2 at the table)")
                               .arg(QString::number(newgame::MinimumSeats),
                                    QString::number(newgame::MaximumSeats)));

    summary->setText(newgame::summaryText(current));

    // MARKED AND DISABLED, not reported after the fact (section 13). The message
    // names the offending value, and Deal cannot be pressed while one stands.
    const std::optional<QString> wrong = newgame::firstError(current);
    error->setText(wrong.value_or(QString()));
    deal->setEnabled(!wrong.has_value());
}
