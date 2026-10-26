#include "RoundSheet.h"

#include "table/CardText.h"
#include "table/HudText.h"
#include "table/RoundText.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QLabel>

namespace
{
// The streak the engine pays out on, and the round before it is the one worth
// warning about. docs/RULES.md: +10 on the fifth consecutive exact bid, -10 on
// the fifth consecutive miss.
constexpr unsigned int payoutStreak = 5;
constexpr unsigned int warnAt = payoutStreak - 1;
}

QString roundsheet::titleFor(const RoundResult& result)
{
    // THE CARD COUNT AND THE ROUND TYPE THROUGH infostrip's OWN FUNCTIONS, not
    // second copies of either. The scoreboard says the same round all round, and
    // two spellings of Forehead is exactly the drift section 7 warns about - one of
    // them would be the one lupdate collects.
    //
    // One arg() taking three rather than three chained: a player cannot reach
    // this string, but the rule SnapshotLog.cpp states holds anyway - arg() fills
    // the lowest placeholder still standing, including one that arrived inside a
    // string it just substituted.
    return QCoreApplication::translate("roundsheet", "Round %1 · %2 · %3")
        .arg(QString::number(result.roundNumber),
             infostrip::cardsText(result.trickCount),
             infostrip::roundTypeText(result.roundType));
}

QString roundsheet::metaFor(const RoundResult& result)
{
    // hasTrump is false in an eight-card round, where `trump` is Card's default -
    // a REAL two of clubs. Read unguarded, the sheet would name a trump the round
    // never had.
    const QString trump =
        result.hasTrump
            ? QCoreApplication::translate("roundsheet", "Trump %1").arg(cardtext::label(result.trump))
            : QCoreApplication::translate("roundsheet", "No trump");

    return QCoreApplication::translate("roundsheet",
                                       "%1 · exact bid scores 5 + bid, a miss loses the difference")
        .arg(trump);
}

QString roundsheet::arrowFor(const RoundSeatResult& seat)
{
    // NOT A SUM DONE UPSTREAM. The bridge carries the two numbers apart because
    // that is how the engine reports them inside onRoundScored(), and adding
    // them here is what makes the arrow show a before and an after rather than
    // one number twice.
    return QCoreApplication::translate("roundsheet", "%1 \xE2\x86\x92 %2")
        .arg(sheet::number(seat.totalScore),
             sheet::number(seat.totalScore + seat.roundScore));
}

QString roundsheet::roundScoreFor(const RoundSeatResult& seat)
{
    // THE SIGN IS THE CUE and the colour only makes it quick, which is section
    // 18's rule and the one the scoreboard's round column kept until the table
    // redesign's phase 8 took the column away. A negative number
    // already carries its own minus; a positive one has to be given its plus, or
    // the two columns read as unrelated kinds of number.
    if(seat.roundScore > 0)
        return QStringLiteral("+%1").arg(seat.roundScore);

    // The minus sign the Total column writes too (sheet::number()).
    if(seat.roundScore < 0)
        return sheet::number(seat.roundScore);

    return QStringLiteral("0");
}

QStringList roundsheet::streakWarnings(const RoundResult& result)
{
    // SILENT WHEN THE NEXT ROUND CANNOT COUNT, and this is the first thing
    // checked rather than the last, because it makes every warning below true or
    // none of them. A one-card round neither advances a streak nor breaks it, so
    // there is no fifth to reach and nothing to warn about.
    //
    // Also false on the final round, where there is no next round at all.
    if(!result.nextRoundCountsForStreaks)
        return {};

    QStringList lines;

    // EVERY SEAT ON FOUR, and the loop runs to the end rather than returning
    // from inside it - see the header for what returning early cost. Table
    // order, which is the order the rows above are drawn in.
    for(const RoundSeatResult& seat : result.seats)
    {
        // EXACTLY four, in either direction. Five has already paid out and three
        // is too early to be worth a line - and the two counters are never both
        // engaged, because a round that advances one resets the other, so the
        // order of these two within a seat decides nothing.
        if(seat.lossStreak == warnAt)
            lines.append(QCoreApplication::translate(
                             "roundsheet", "%1 has missed four in a row - a fifth costs %2.")
                             .arg(QString::fromStdString(seat.name), QString::number(10)));

        if(seat.winStreak == warnAt)
            lines.append(QCoreApplication::translate(
                             "roundsheet", "%1 has hit four in a row - a fifth is worth %2.")
                             .arg(QString::fromStdString(seat.name), QString::number(10)));
    }

    return lines;
}

// ---- the widget ----

RoundSheet::RoundSheet(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    title = new QLabel(panel);
    title->setObjectName("roundTitle");
    dress(title, sheet::Role::Title);

    inside->addWidget(title);

    // The trump's glyph in its suit's colour, so rich text.
    meta = new QLabel(panel);
    meta->setObjectName("roundMeta");
    meta->setTextFormat(Qt::RichText);
    dress(meta, sheet::Role::Meta);

    inside->addSpacing(2);
    inside->addWidget(meta);
    inside->addSpacing(16);

    // THE MOCK-UP'S FIVE COLUMNS, and the words the redesign's phase 8 left this
    // sheet to settle with its look: Player, Bid, Taken, Round, Total - the sheet's
    // own now, under its own context. The scoreboard says B, T and PTS in a panel
    // with no room for words.
    //
    // A literal at every call site, for BidStripItem.h's reason: a wrapper of our
    // own would hide these from lupdate.
    rows = new SheetTable(panel);
    rows->setObjectName("roundRows");
    rows->setColumns({ { QCoreApplication::translate("roundsheet", "Player"), Qt::AlignLeft },
                       { QCoreApplication::translate("roundsheet", "Bid"), Qt::AlignRight },
                       { QCoreApplication::translate("roundsheet", "Taken"), Qt::AlignRight },
                       { QCoreApplication::translate("roundsheet", "Round"), Qt::AlignRight },
                       { QCoreApplication::translate("roundsheet", "Total"), Qt::AlignRight } });

    inside->addWidget(rows);

    warning = new QLabel(panel);
    warning->setObjectName("streakWarning");
    dress(warning, sheet::Role::Note);
    warning->hide();

    inside->addSpacing(12);
    inside->addWidget(warning);

    // THE MOCK-UP'S QUIET [Round history] BEFORE ITS DEAL BUTTON: the whole game so
    // far, opened over this sheet. Never the default - Enter is Continue's.
    historyButton = new SheetButton(QCoreApplication::translate("roundsheet", "Round history"),
                                    SheetButton::Kind::Quiet, panel);
    historyButton->setObjectName("roundHistory");
    historyButton->setAutoDefault(false);

    continueButton = new SheetButton(QCoreApplication::translate("roundsheet", "Continue"),
                                     SheetButton::Kind::Primary, panel);
    continueButton->setObjectName("continueRound");
    continueButton->setDefault(true);

    addActions({ historyButton, continueButton });

    connect(historyButton, &QPushButton::clicked, this, &RoundSheet::historyRequested);
    connect(continueButton, &QPushButton::clicked, this, &RoundSheet::continueRequested);
}

void RoundSheet::showResult(const RoundResult& result)
{
    title->setText(roundsheet::titleFor(result));
    meta->setText(sheet::withSuits(roundsheet::metaFor(result), theme().table));

    // REFILLED, NOT APPENDED: the table outlives every round of every game, and a
    // game with fewer seats can follow one with more.
    rows->clearRows();

    for(const RoundSeatResult& seat : result.seats)
    {
        // THE COLOUR REINFORCES THE SIGN rather than replacing it (section 18).
        // Zero takes neither, because zero went neither way.
        SheetTable::Tone tone = SheetTable::Tone::Plain;

        if(seat.roundScore > 0)
            tone = SheetTable::Tone::Good;
        else if(seat.roundScore < 0)
            tone = SheetTable::Tone::Bad;

        // Your own name bold, as the scoreboard drew it until the table redesign's
        // phase 8: a table of four names is read by finding your own first. A dash,
        // not a zero, for a seat that never bid.
        rows->addRow({ { QString::fromStdString(seat.name), SheetTable::Tone::Plain, seat.isHuman },
                       { seat.hasBet ? QString::number(seat.bet) : hudtext::dash() },
                       { QString::number(seat.tricksWon) },
                       { roundsheet::roundScoreFor(seat), tone },
                       { roundsheet::arrowFor(seat) } });
    }

    // ONE LABEL, NOT ONE PER LINE. The label already wraps and the sheet is sized
    // to what it holds, so joining costs no layout - and at most six seats can be
    // on four at once, which in practice is one and occasionally two.
    const QStringList lines = roundsheet::streakWarnings(result);

    warning->setText(lines.join(QLatin1Char('\n')));
    warning->setVisible(!lines.isEmpty());

    // The seat count can differ between games, so the sheet is sized to what it
    // now holds rather than to what it held.
    placeSheet();
}

void RoundSheet::focusDefault()
{
    continueButton->setFocus(Qt::OtherFocusReason);
}
