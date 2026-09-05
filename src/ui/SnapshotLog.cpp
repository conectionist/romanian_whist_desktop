#include "SnapshotLog.h"

#include <QStringList>

using romanian_whist::Card;
using romanian_whist::RoundType;

namespace
{

QString roundTypeName(RoundType type)
{
    switch(type)
    {
        case RoundType::Normal:   return QStringLiteral("Normal");
        case RoundType::Forehead: return QStringLiteral("Forehead");
        case RoundType::Hidden:   return QStringLiteral("Hidden");
    }

    return QStringLiteral("?");
}

// The seat's name, or "-" for nobody. Both callers below need the same answer
// for the same reason: -1 is the snapshot's way of saying no seat, because Seat
// has no default constructor and no sentinel of its own.
QString seatName(const GameSnapshot& snapshot, int seat)
{
    if(seat < 0 || static_cast<std::size_t>(seat) >= snapshot.seats.size())
        return QStringLiteral("-");

    return QString::fromStdString(snapshot.seats[static_cast<std::size_t>(seat)].name);
}

}

namespace snapshotlog
{

QString describe(const Card& card)
{
    return QString::fromStdString(card.toString());
}

QString format(const GameSnapshot& snapshot, std::chrono::milliseconds elapsed)
{
    QStringList parts;

    // Seconds to three places rather than raw milliseconds: the numbers being
    // compared are 0.25 and 1.2, and reading those off 250 and 1200 is work the
    // formatter can do once.
    parts << QStringLiteral("[%1]").arg(elapsed.count() / 1000.0, 8, 'f', 3);

    // Both 1-based, as the snapshot already holds them - roundNumber is
    // getCurrentRoundIndex() + 1, and trickNumber is 0 only between the deal
    // and the first trick, which is a real state rather than an off-by-one.
    parts << QStringLiteral("R%1/%2").arg(snapshot.roundNumber).arg(snapshot.roundCount);
    parts << QStringLiteral("T%1/%2").arg(snapshot.trickNumber).arg(snapshot.trickCount);

    parts << roundTypeName(snapshot.roundType).leftJustified(8);

    // No trump in an 8-card round, and none at all before the deal - hasTrump
    // is what says so, and printing snapshot.trump regardless would show the
    // two of clubs, which is Card's default and a real card.
    parts << QStringLiteral("trump=%1").arg(snapshot.hasTrump ? describe(snapshot.trump)
                                                              : QStringLiteral("-")).leftJustified(11);

    parts << QStringLiteral("active=%1").arg(seatName(snapshot, snapshot.activeSeat)).leftJustified(15);

    parts << QStringLiteral("bets %1/%2 sum %3").arg(snapshot.betsPlaced)
                                                .arg(snapshot.seats.size())
                                                .arg(snapshot.betSum);

    if(!snapshot.table.empty())
    {
        QStringList played;

        // In PLAY order, which is the order the engine holds the trick in - not
        // seat order, and not rebuilt from the leader. The winning card is
        // starred from isWinning, which is the engine's own ranking.
        for(const PlayedCardView& entry : snapshot.table)
        {
            played << QStringLiteral("%1 %2%3")
                          .arg(seatName(snapshot, static_cast<int>(entry.seatIndex)))
                          .arg(describe(entry.card))
                          .arg(entry.isWinning ? QStringLiteral("*") : QString());
        }

        parts << QStringLiteral("lead=%1").arg(seatName(snapshot, static_cast<int>(snapshot.leadSeat)));
        parts << QStringLiteral("table=[%1]").arg(played.join(QStringLiteral(", ")));
    }

    // Tricks won, once anyone has taken one. Kept off the line until then so a
    // bidding frame is not padded with four zeroes.
    QStringList wins;
    for(const SeatView& seat : snapshot.seats)
    {
        if(seat.tricksWon > 0)
            wins << QStringLiteral("%1:%2").arg(QString::fromStdString(seat.name)).arg(seat.tricksWon);
    }

    if(!wins.isEmpty())
        parts << QStringLiteral("won[%1]").arg(wins.join(QStringLiteral(" ")));

    return parts.join(QStringLiteral("  "));
}

} // namespace snapshotlog
