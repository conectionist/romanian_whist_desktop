#include "RulesSheet.h"

#include <romanian_whist/Scoreboard.h>

#include <QBoxLayout>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>

#include <algorithm>

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
    styleTitle(title, 1.6);

    inside->addWidget(title);
    inside->addSpacing(12);

    // A bulleted list, as the mock-up's <ul>. Rich text for the bullets only:
    // every line is escaped on its way in, so nothing in it is read as markup.
    rules = new QLabel(panel);
    rules->setObjectName(QStringLiteral("rulesText"));
    rules->setTextFormat(Qt::RichText);
    rules->setWordWrap(true);

    // max-width: 62ch in the mock-up; about this wide in the client's own font.
    rules->setFixedWidth(560);

    inside->addWidget(rules);
    inside->addSpacing(18);

    closeButton = new QPushButton(QCoreApplication::translate("rulessheet", "Close"), panel);
    closeButton->setObjectName(QStringLiteral("rulesClose"));
    closeButton->setDefault(true);
    closeButton->setMinimumWidth(140);
    closeButton->setMinimumHeight(34);

    // Bottom-right, where the mock-up's sheets keep their actions.
    QHBoxLayout* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(closeButton);
    inside->addLayout(actions);

    connect(closeButton, &QPushButton::clicked, this, &RulesSheet::closeRequested);
}

void RulesSheet::showRules(const newgame::Config& config)
{
    QString list = QStringLiteral("<ul style=\"margin-left: 0px; -qt-list-indent: 1;\">");

    for(const QString& line : rulessheet::linesFor(config))
        list += QStringLiteral("<li style=\"margin-bottom: 6px;\">") + line.toHtmlEscaped() +
                QStringLiteral("</li>");

    list += QStringLiteral("</ul>");

    rules->setText(list);
}

void RulesSheet::focusDefault()
{
    closeButton->setFocus(Qt::OtherFocusReason);
}
