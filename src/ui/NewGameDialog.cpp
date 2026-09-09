#include "NewGameDialog.h"

#include <romanian_whist/AiMoveProvider.h>
#include <romanian_whist/strategies/DuckingStrategy.h>
#include <romanian_whist/strategies/FirstCardStrategy.h>
#include <romanian_whist/strategies/LowRiskStrategy.h>
#include <romanian_whist/strategies/RandomCardStrategy.h>

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
// the names phase A's hardcoded setup used - the same table, still recognisable
// to anyone who ran the client before D1.
const QStringList& stockNames()
{
    static const QStringList names = { QStringLiteral("Ana"),
                                       QStringLiteral("Cristina"),
                                       QStringLiteral("Dan"),
                                       QStringLiteral("Elena"),
                                       QStringLiteral("Florin") };
    return names;
}

// The default level for the nth opponent. Deterministic strategies first and
// Random last, which is the order phase A's setup chose and for its reason: a
// fixed deal is only reproducible if every provider that draws is seeded, and a
// table with no Random bot at all needs nothing said about it.
newgame::BotLevel defaultLevel(int index)
{
    static constexpr newgame::BotLevel levels[] = { newgame::BotLevel::LowRisk,
                                                    newgame::BotLevel::FirstCard,
                                                    newgame::BotLevel::Ducking };

    return levels[static_cast<std::size_t>(index) % std::size(levels)];
}

// One provider, from a level. The seed is engaged only for Random, which is the
// only strategy that draws.
std::unique_ptr<IMoveProvider> providerFor(newgame::BotLevel level,
                                           std::optional<std::uint32_t> seed)
{
    switch(level)
    {
    case newgame::BotLevel::Random:
        // SEEDED WHEN THE DEAL IS FIXED. RandomCardStrategy's seeded constructor
        // seeds its mt19937 once, so the whole game is then reproducible - which
        // GameSetup::shuffleSeed alone does not give, because it reaches the
        // shuffle and nothing else.
        return std::make_unique<AiMoveProvider>(
            seed ? std::make_unique<RandomCardStrategy>(*seed)
                 : std::make_unique<RandomCardStrategy>());

    case newgame::BotLevel::FirstCard:
        return std::make_unique<AiMoveProvider>(std::make_unique<FirstCardStrategy>());

    case newgame::BotLevel::LowRisk:
        return std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>());

    case newgame::BotLevel::Ducking:
        return std::make_unique<AiMoveProvider>(std::make_unique<DuckingStrategy>());
    }

    // Unreachable for any enumerator above, and not an assert: a switch with no
    // default warns when a new level is added, which is the diagnostic wanted -
    // this is only here because a scoped enum can hold a value none of them name.
    return std::make_unique<AiMoveProvider>(std::make_unique<LowRiskStrategy>());
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
}

newgame::Config newgame::defaults()
{
    Config config;

    config.humanName = QCoreApplication::translate("newgame", "You");

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

    // TWO SENTENCES, NOT ONE INTERPOLATED, when the estimate rounds to nothing.
    // An "about 0 minutes" is worse than saying nothing, and at the instant end
    // of the slider that is the honest answer.
    if(minutes < 1)
        return QCoreApplication::translate("newgame", "%n round(s)", "", static_cast<int>(rounds));

    return QCoreApplication::translate("newgame", "%1 - about %2 minutes")
        .arg(QCoreApplication::translate("newgame", "%n round(s)", "", static_cast<int>(rounds)),
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

GameSetup newgame::buildSetup(const Config& config)
{
    GameSetup setup;

    // FIRST, and with a null provider on purpose - GameController::start() fills
    // exactly this seat, addressed by humanSeatOf() above. Any OTHER null seat
    // stays null and GameEngine::start() rejects it BY NAME, which is the
    // behaviour worth preserving.
    if(!config.spectator)
        setup.seats.push_back(SeatSetup{ config.humanName.toStdString(), nullptr });

    for(int i = 0 ; i < config.botNames.size() ; i++)
    {
        // The seed varies per seat, so two Random bots at one table do not play
        // the same stream as each other.
        const std::optional<std::uint32_t> seed =
            config.shuffleSeed
                ? std::optional<std::uint32_t>(*config.shuffleSeed
                                               + static_cast<std::uint32_t>(setup.seats.size()))
                : std::nullopt;

        setup.seats.push_back(
            SeatSetup{ config.botNames.at(i).toStdString(),
                       providerFor(config.botLevels.at(static_cast<std::size_t>(i)), seed) });
    }

    setup.structure = config.structure;
    setup.endWithForeheadAndHidden = config.endWithForeheadAndHidden;
    setup.all1GamesAreForehead = config.all1GamesAreForehead;
    setup.shuffleSeed = config.shuffleSeed;

    return setup;
}

QString newgame::nameOf(BotLevel level)
{
    switch(level)
    {
    case BotLevel::Random:    return QCoreApplication::translate("newgame", "Random");
    case BotLevel::FirstCard: return QCoreApplication::translate("newgame", "First card");
    case BotLevel::LowRisk:   return QCoreApplication::translate("newgame", "Low risk");
    case BotLevel::Ducking:   return QCoreApplication::translate("newgame", "Ducking");
    }

    return QString();
}

QString newgame::tooltipFor(BotLevel level)
{
    // The engine's STRATEGIES.md lead table, one row each, so the choice means
    // something to a player who has never read the source (section 13).
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
    }

    return QString();
}

// ---- the widget ----

NewGameDialog::NewGameDialog(const newgame::Config& initial, QWidget* parent)
    : QDialog(parent), current(initial)
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
    structure818 = new QRadioButton(tr("8-1-8 - the other way round"), this);

    // The second group - see whoGroup above for what sharing one costs.
    structureGroup = new QButtonGroup(this);
    structureGroup->addButton(structure181);
    structureGroup->addButton(structure818);

    stack->addWidget(structure181);
    stack->addWidget(structure818);

    foreheadAndHidden = new QCheckBox(tr("End with a forehead round and a hidden round"), this);
    allOnesForehead = new QCheckBox(tr("Play every one-card round forehead"), this);
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
    speed->setRange(newgame::SlowestSpeed, newgame::FastestSpeed);
    speed->setPageStep(1);
    speed->setTickPosition(QSlider::TicksBelow);

    QHBoxLayout* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(tr("Bot speed"), this));
    speedRow->addWidget(new QLabel(tr("Slow"), this));
    speedRow->addWidget(speed, 1);
    speedRow->addWidget(new QLabel(tr("Instant"), this));
    stack->addLayout(speedRow);

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
    fixedDeal->setChecked(current.shuffleSeed.has_value());

    if(current.shuffleSeed)
        seed->setValue(static_cast<int>(*current.shuffleSeed % 1000000u));

    opponents->setRange(1, static_cast<int>(newgame::MaximumSeats) - 1);
    opponents->setValue(static_cast<int>(current.botNames.size()));

    const auto onChange = [this]{ refresh(); };

    connect(playing, &QRadioButton::toggled, this, [this](bool on)
    {
        // THE RANGE MOVES WITH THE RADIO. A watched game has no human seat, so
        // it needs two bots at minimum and may have six; a played one already
        // has the human, so one bot is enough and five is the ceiling. Section
        // 13: a spinner that cannot reach an illegal value beats an error
        // message explaining one.
        opponents->setRange(on ? 1 : static_cast<int>(newgame::MinimumSeats),
                            static_cast<int>(newgame::MaximumSeats) - (on ? 1 : 0));
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

        // A PRE-FILLED DEFAULT, and one that is not already taken. The stock
        // list runs out at five, which is exactly the maximum number of
        // opponents, so the fallback below is unreachable today and is written
        // anyway - it is one line against a blank field that disables Deal for
        // a reason the player did not cause.
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

        row.level->setCurrentIndex(static_cast<int>(defaultLevel(index)));

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
