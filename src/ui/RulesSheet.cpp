#include "RulesSheet.h"

#include <romanian_whist/Scoreboard.h>

#include <QBoxLayout>
#include <QCoreApplication>
#include <QFontMetricsF>
#include <QLabel>

#include <algorithm>
#include <cmath>

using namespace romanian_whist;

Rank rulessheet::lowestRank(unsigned int seats)
{
    // The Ace and the 2N - 1 ranks under it. The engine accepts two to six
    // players (GameEngine::start), which keeps this between the Three and the
    // Jack; clamped so a count outside that still names a rank.
    const int ace = static_cast<int>(Rank::Ace);
    const int lowest = ace - (2 * static_cast<int>(seats) - 1);

    return static_cast<Rank>(std::clamp(lowest, static_cast<int>(Rank::Two), ace));
}

QString rulessheet::rankName(Rank rank)
{
    switch(rank)
    {
    case Rank::Jack:  return QCoreApplication::translate("rulessheet", "Jack");
    case Rank::Queen: return QCoreApplication::translate("rulessheet", "Queen");
    case Rank::King:  return QCoreApplication::translate("rulessheet", "King");
    case Rank::Ace:   return QCoreApplication::translate("rulessheet", "Ace");

    // Two is 0 in the engine's enum, so a pip card's number is its value plus 2.
    default:
        return QString::number(static_cast<int>(rank) + 2);
    }
}

QStringList rulessheet::linesFor(const newgame::Config& config)
{
    const unsigned int seats = newgame::seatCount(config);
    const unsigned int rounds = newgame::roundCount(config);

    QStringList lines;

    lines << QCoreApplication::translate("rulessheet",
                                         "%1 players use Ace down to %2: eight cards each, %3 in all.")
                 .arg(QString::number(seats), rankName(lowestRank(seats)), QString::number(8 * seats));

    // WHOLE SENTENCES, four of them, rather than a schedule with a clause added:
    // a translator sees each one as it reads, and Romanian may want the appended
    // rounds somewhere other than at the end. The count is never 1 (the smallest
    // table deals 18), so it needs no plural forms.
    const bool appended = config.endWithForeheadAndHidden;

    if(config.structure == GameStructure::S_818)
    {
        lines << (appended
                      ? QCoreApplication::translate("rulessheet",
                                                    "Rounds go 8 (×%1), 7 … 3, 2, 1 (×%1), 2, 3 … 7, "
                                                    "8 (×%1), then a Forehead and a Hidden round: "
                                                    "%2 rounds.")
                      : QCoreApplication::translate("rulessheet",
                                                    "Rounds go 8 (×%1), 7 … 3, 2, 1 (×%1), 2, 3 … 7, "
                                                    "8 (×%1): %2 rounds."))
                     .arg(QString::number(seats), QString::number(rounds));
    }
    else
    {
        lines << (appended
                      ? QCoreApplication::translate("rulessheet",
                                                    "Rounds go 1 (×%1), 2, 3 … 7, 8 (×%1), 7 … 3, 2, "
                                                    "1 (×%1), then a Forehead and a Hidden round: "
                                                    "%2 rounds.")
                      : QCoreApplication::translate("rulessheet",
                                                    "Rounds go 1 (×%1), 2, 3 … 7, 8 (×%1), 7 … 3, 2, "
                                                    "1 (×%1): %2 rounds."))
                     .arg(QString::number(seats), QString::number(rounds));
    }

    // RULES.md rule 9, said only for a game that deals them.
    //
    // WITH THE APPENDED PAIR, NOT QUITE EVERY ONE: the Hidden round at the end
    // is a 1-card round and keeps its own type (Scoreboard::initialize()). The
    // plain sentence claimed it was Forehead (PR #35's review, finding #5).
    if(config.all1GamesAreForehead)
    {
        lines << (appended
                      ? QCoreApplication::translate("rulessheet",
                                                    "Every 1-card round is a Forehead round, except "
                                                    "the Hidden round at the end.")
                      : QCoreApplication::translate("rulessheet",
                                                    "Every 1-card round is a Forehead round."));
    }

    if(config.all1GamesAreForehead || appended)
    {
        lines << QCoreApplication::translate("rulessheet",
                                             "In a Forehead round you see everyone's card but your "
                                             "own while you bid.");
    }

    if(appended)
    {
        lines << QCoreApplication::translate("rulessheet",
                                             "In a Hidden round nobody sees their card until the "
                                             "bidding is done.");
    }

    // Rules 4 to 8, in the mock-up's words. The last adds rule 8's exception,
    // which the mock-up leaves out and which a player counting their run needs.
    lines << QCoreApplication::translate("rulessheet",
                                         "Below 8 cards, the next card is turned as trump. The "
                                         "8-card rounds have no trump.")
          << QCoreApplication::translate("rulessheet",
                                         "Everyone bids how many tricks they will take. The last "
                                         "bidder may not make the bids add up to the number of "
                                         "tricks.")
          << QCoreApplication::translate("rulessheet",
                                         "Follow suit if you can, otherwise trump if you can. You "
                                         "never have to overtrump.")
          << QCoreApplication::translate("rulessheet",
                                         "An exact bid scores 5 + bid. A miss loses the "
                                         "difference. Five exact bids in a row earn +10, five "
                                         "misses cost 10; 1-card rounds neither add to a run "
                                         "nor break one.");

    return lines;
}

RulesSheet::RulesSheet(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    QLabel* title = new QLabel(QCoreApplication::translate("rulessheet", "Rules at a glance"), panel);
    title->setObjectName(QStringLiteral("rulesTitle"));
    dress(title, sheet::Role::Title);

    inside->addWidget(title);
    inside->addSpacing(14);

    // A bulleted list, as the mock-up's <ul>. Rich text for the bullets only:
    // every line is escaped on its way in, so nothing in it is read as markup.
    rules = new QLabel(panel);
    rules->setObjectName(QStringLiteral("rulesText"));
    rules->setTextFormat(Qt::RichText);
    dress(rules, sheet::Role::Body);

    // max-width: 62ch in the mock-up - 62 of the font's zeros - so a line is as
    // long as reads comfortably whatever the sheet's width. A maximum and not a
    // width, so a sheet narrowed by a small window narrows the list with it.
    rules->setMaximumWidth(static_cast<int>(
        std::ceil(62.0 * QFontMetricsF(rules->font()).horizontalAdvance(QLatin1Char('0')))));

    // IN A ROW WITH A STRETCH AFTER IT, not straight in the column. A column asks
    // a label how tall it is at the column's whole width, past the label's
    // maximum, and so under-counts the lines it wraps to; a row hands it the
    // width it will really have, and asks at that.
    QHBoxLayout* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(rules, 1);
    row->addStretch(0);

    inside->addLayout(row);

    closeButton = new SheetButton(QCoreApplication::translate("rulessheet", "Close"),
                                  SheetButton::Kind::Primary, panel);
    closeButton->setObjectName(QStringLiteral("rulesClose"));
    closeButton->setDefault(true);

    // Bottom-right, where the mock-up's sheets keep their actions.
    addActions({ closeButton });

    connect(closeButton, &QPushButton::clicked, this, &RulesSheet::closeRequested);
}

void RulesSheet::showRules(const newgame::Config& config)
{
    // .sheet ul: lines 1.5 tall. Qt's list indent is the document's, which leaves
    // the bullets where Qt puts them rather than the mock-up's 20 in.
    QString list = QStringLiteral("<ul style=\"margin-left: 0px; margin-top: 0px; -qt-list-indent: 1;\">");

    for(const QString& line : rulessheet::linesFor(config))
        list += QStringLiteral("<li style=\"line-height: 150%;\">") + line.toHtmlEscaped() +
                QStringLiteral("</li>");

    list += QStringLiteral("</ul>");

    rules->setText(list);

    placeSheet();
}

void RulesSheet::focusDefault()
{
    closeButton->setFocus(Qt::OtherFocusReason);
}
