#include "HistorySheet.h"

#include "table/HudText.h"
#include "ui/RoundSheet.h"

#include <QBoxLayout>
#include <QCoreApplication>
#include <QLabel>

QString historysheet::noteFor(const RoundSeatResult& seat)
{
    const QString bid = seat.hasBet ? QString::number(seat.bet) : hudtext::dash();

    return QStringLiteral("%1/%2").arg(QString::number(seat.tricksWon), bid);
}

std::vector<int> historysheet::totalsFor(const std::vector<RoundResult>& rounds, std::size_t seats)
{
    std::vector<int> totals(seats, 0);

    if(rounds.empty())
        return totals;

    const RoundResult& last = rounds.back();

    for(std::size_t i = 0 ; i < seats && i < last.seats.size() ; i++)
        totals[i] = last.seats[i].totalScore + last.seats[i].roundScore;

    return totals;
}

// ---- the widget ----

HistorySheet::HistorySheet(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    title = new QLabel(QCoreApplication::translate("historysheet", "Round history"), panel);
    title->setObjectName("historyTitle");
    dress(title, sheet::Role::Title);

    inside->addWidget(title);

    meta = new QLabel(QCoreApplication::translate("historysheet", "Score per round, then taken/bid"), panel);
    meta->setObjectName("historyMeta");
    dress(meta, sheet::Role::Meta);

    inside->addSpacing(2);
    inside->addWidget(meta);
    inside->addSpacing(16);

    rows = new SheetTable(panel);
    rows->setObjectName("historyRows");

    inside->addWidget(rows);

    closeButton = new SheetButton(QCoreApplication::translate("historysheet", "Close"),
                                  SheetButton::Kind::Primary, panel);
    closeButton->setObjectName("closeHistory");
    closeButton->setDefault(true);

    addActions({ closeButton });

    connect(closeButton, &QPushButton::clicked, this, &HistorySheet::closeRequested);
}

void HistorySheet::showHistory(const std::vector<RoundResult>& rounds, const std::vector<QString>& names)
{
    std::vector<QString> heads = names;

    if(heads.empty() && !rounds.empty())
    {
        for(const RoundSeatResult& seat : rounds.back().seats)
            heads.push_back(QString::fromStdString(seat.name));
    }

    // THE MOCK-UP'S COLUMNS: the round, the cards dealt in it, and a player each,
    // whose names give way to fit six of them (fitHeadingsTo() below). Rebuilt
    // every time, since a game with fewer seats can follow one with more.
    std::vector<SheetTable::Column> columns = {
        { QCoreApplication::translate("historysheet", "Round"), Qt::AlignLeft },
        { QCoreApplication::translate("historysheet", "Cards"), Qt::AlignRight }
    };

    for(const QString& name : heads)
        columns.push_back({ name, Qt::AlignRight, true });

    rows->setColumns(std::move(columns));

    for(const RoundResult& round : rounds)
    {
        std::vector<SheetTable::Cell> cells = {
            { QString::number(round.roundNumber) },
            { QString::number(round.trickCount) }
        };

        for(std::size_t i = 0 ; i < heads.size() ; i++)
        {
            if(i >= round.seats.size())
            {
                cells.push_back({ hudtext::dash() });
                continue;
            }

            const RoundSeatResult& seat = round.seats[i];

            // THE SIGN CARRIES IT and the colour makes it quick (section 18), as on
            // the round sheet, whose spelling of a round score this is.
            SheetTable::Tone tone = SheetTable::Tone::Plain;

            if(seat.roundScore > 0)
                tone = SheetTable::Tone::Good;
            else if(seat.roundScore < 0)
                tone = SheetTable::Tone::Bad;

            cells.push_back({ roundsheet::roundScoreFor(seat), tone, false, historysheet::noteFor(seat) });
        }

        rows->addRow(cells);
    }

    // The mock-up's empty table: its headings, a line across them, and totals of
    // nothing.
    if(rounds.empty())
        rows->addNote(QCoreApplication::translate("historysheet", "No rounds finished yet."));

    std::vector<SheetTable::Cell> totalRow = {
        { QCoreApplication::translate("historysheet", "Total"), SheetTable::Tone::Plain, true },
        { QString() }
    };

    for(const int total : historysheet::totalsFor(rounds, heads.size()))
        totalRow.push_back({ sheet::number(total), SheetTable::Tone::Plain, true });

    rows->addRow(totalRow);

    fitColumns();
    placeSheet();
}

void HistorySheet::fitColumns()
{
    if(!rows)
        return;

    // NEVER WIDER THAN THE SHEET: its body scrolls down, never across, so a
    // six-seat table of long names shortens the names - to the room beside the
    // scroll bar, when a long game's history scrolls.
    rows->fitHeadingsTo(contentWidth());
}

void HistorySheet::contentWidthChanged()
{
    fitColumns();
}

void HistorySheet::focusDefault()
{
    closeButton->setFocus(Qt::OtherFocusReason);
    reveal(closeButton);
}
