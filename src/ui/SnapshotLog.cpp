#include "SnapshotLog.h"

#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <vector>

using romanian_whist::Card;
using romanian_whist::RoundType;

namespace
{

// The seat's name, or "-" for nobody. Both callers below need the same answer
// for the same reason: -1 is the snapshot's way of saying no seat, because Seat
// has no default constructor and no sentinel of its own.
QString seatName(const GameSnapshot& snapshot, int seat)
{
    if(seat < 0 || static_cast<std::size_t>(seat) >= snapshot.seats.size())
        return QStringLiteral("-");

    return QString::fromStdString(snapshot.seats[static_cast<std::size_t>(seat)].name);
}

// How wide the active= field has to be for THIS game's names. Every seat is
// measured, not just the one on turn, because the field has to hold whichever
// of them takes a turn later in the same log - and "-" for nobody, which is
// what a bidding frame prints, is the floor.
//
// DERIVED RATHER THAN A CONSTANT. This was 15, which "Cristina" fills exactly,
// so the first 9-character name - phase D's setup dialog is where the names
// stop being ours - pushed every column right of it sideways for the whole log.
// Raising the constant only moves that cliff: a number big enough for any name
// anyone might type spends those columns on every line of every log, and one
// merely bigger than today's names is the same defect again, later. The seats
// do not change within a game, so a width read off them is the same on every
// line of one log, which is the only comparison this padding exists to serve.
int activeWidth(const GameSnapshot& snapshot)
{
    int widest = 1;

    for(const SeatView& seat : snapshot.seats)
    {
        // MEASURED AS A QString, not as the std::string it came from: a name
        // with a diacritic - and this is a Romanian card game - is more bytes
        // than it is columns.
        widest = std::max(widest, static_cast<int>(QString::fromStdString(seat.name).size()));
    }

    return widest;
}

// A round score with its sign always shown. The plus is the point: what is
// being read off this column is a delta against the total beside it, and "+0"
// says a seat scored nothing this round where a bare "0" reads as no data.
QString signedScore(int score)
{
    return score < 0 ? QString::number(score)
                     : QStringLiteral("+%1").arg(score);
}

// The card as this log spells it - rank then suit, no separator, matching
// Card::toString().
QString describe(const Card& card)
{
    return QString::fromStdString(card.toString());
}

// FIXED ASCII, not user-facing text: these three words are asserted on by the
// log tests and read out of saved logs, so they must not move when a translation
// is loaded. The status bar's own names go through tr() - see
// MainWindow::roundTypeText().
QString describe(RoundType type)
{
    switch(type)
    {
        case RoundType::Normal:   return QStringLiteral("Normal");
        case RoundType::Forehead: return QStringLiteral("Forehead");
        case RoundType::Hidden:   return QStringLiteral("Hidden");
    }

    return QStringLiteral("?");
}

// The hand as a prompt carries it: display order, with an illegal slot in
// parentheses. That is the log's version of the dimming section 18 specifies,
// and it is the one thing a snapshot's hand can never show - HandEntry::legal is
// always true on a snapshot, and means something only here.
//
// Empty in a blind round, where the prompt carries no cards at all. The caller
// prints the count of face-down backs instead; this returns an empty list rather
// than deciding that for it.
QString describeEntries(const std::vector<HandEntry>& entries)
{
    QStringList cards;

    for(const HandEntry& entry : entries)
    {
        cards << (entry.legal ? describe(entry.card)
                              : QStringLiteral("(%1)").arg(describe(entry.card)));
    }

    return cards.join(QStringLiteral(" "));
}

// Display slot -> the engine's own hand position, which is the mapping a click
// handler translates through and the one place "the card played is not the card
// clicked" comes from. Logged because it is otherwise invisible: it is carried
// even in a blind round, where nothing else about the hand is.
QString describeIndex(const std::vector<std::size_t>& handIndex)
{
    QStringList positions;

    for(std::size_t position : handIndex)
        positions << QString::number(position);

    return positions.join(QStringLiteral(" "));
}

// WHERE A TIME WOULD BE, and exactly as wide, so the fields right of it line up
// with the snapshot lines either side. A prompt is raised between snapshots and
// has no elapsed time of its own; a column of blanks would let the eye slide
// past the one line on the page that says what the player was asked.
QString askPrefix()
{
    return QStringLiteral("[   ask  ]");
}

}

namespace snapshotlog
{

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
    //
    // PADDED, like every field below that can change width. R1/26 is five
    // characters and R10/26 is six, so without this every field to the right of
    // it steps sideways at round ten and stays there - in a log whose whole
    // point is comparing a column down the page.
    parts << QStringLiteral("R%1/%2").arg(snapshot.roundNumber)
                                     .arg(snapshot.roundCount).leftJustified(6);
    parts << QStringLiteral("T%1/%2").arg(snapshot.trickNumber)
                                     .arg(snapshot.trickCount).leftJustified(5);

    parts << describe(snapshot.roundType).leftJustified(8);

    // No trump in an 8-card round, and none at all before the deal - hasTrump
    // is what says so, and printing snapshot.trump regardless would show the
    // two of clubs, which is Card's default and a real card.
    parts << QStringLiteral("trump=%1").arg(snapshot.hasTrump ? describe(snapshot.trump)
                                                              : QStringLiteral("-")).leftJustified(11);

    // The NAME is padded, not the whole field: "active=" is fixed width already,
    // and padding inside makes the width above the only number in play.
    parts << QStringLiteral("active=%1").arg(seatName(snapshot, snapshot.activeSeat)
                                                 .leftJustified(activeWidth(snapshot)));

    // Padded as one field rather than per number: the sum runs from 0 to past
    // ten within a game, and it is the group's total width that decides where
    // lead=, table=, won[] and scores[] sit on the line.
    parts << QStringLiteral("bets %1/%2 sum %3").arg(snapshot.betsPlaced)
                                                .arg(snapshot.seats.size())
                                                .arg(snapshot.betSum).leftJustified(15);

    if(!snapshot.table.empty())
    {
        QStringList played;

        // In PLAY order, which is the order the engine holds the trick in - not
        // seat order, and not rebuilt from the leader. The winning card is
        // starred from isWinning, which is the engine's own ranking.
        //
        // ONE arg() TAKING THREE, not three chained. arg() fills the lowest
        // placeholder still standing, including one that arrived inside a string
        // it just substituted - so a player who names themselves "50%2" has the
        // card land inside their own name and the real %2 left empty. Every
        // player name below is spelled this way for the same reason; phase D's
        // setup dialog is where the names stop being ours.
        for(const PlayedCardView& entry : snapshot.table)
        {
            played << QStringLiteral("%1 %2%3")
                          .arg(seatName(snapshot, static_cast<int>(entry.seatIndex)),
                               describe(entry.card),
                               entry.isWinning ? QStringLiteral("*") : QString());
        }

        parts << QStringLiteral("lead=%1").arg(seatName(snapshot, static_cast<int>(snapshot.leadSeat)));
        parts << QStringLiteral("table=[%1]").arg(played.join(QStringLiteral(", ")));
    }

    // Tricks won, once anyone has taken one. Kept off the line until then so a
    // bidding frame is not padded with four zeroes.
    bool anyWins = false;

    for(const SeatView& seat : snapshot.seats)
        anyWins = anyWins || seat.tricksWon > 0;

    if(anyWins)
    {
        QStringList wins;

        // EVERY seat, including one that has won nothing yet - the same rule
        // the scores[] block below states, for the same reason. Listing only
        // the winners makes seats appear and disappear as the round runs
        // (won[Cristina:1], then won[Cristina:1 Dan:1]), so the column a reader
        // is following down the page moves under them and a seat stuck on zero
        // is indistinguishable from a seat the log forgot.
        for(const SeatView& seat : snapshot.seats)
        {
            wins << QStringLiteral("%1:%2").arg(QString::fromStdString(seat.name),
                                                QString::number(seat.tricksWon));
        }

        parts << QStringLiteral("won[%1]").arg(wins.join(QStringLiteral(" ")));
    }

    // THE ROUND-BOUNDARY FRAMES, told apart. Without this block the frame
    // published from onRoundScored is byte-identical to the onTrickWon frame
    // before it - nothing else on the line changes between the two - so the
    // 1.2s dwell that now sits on the scored frame reads as one 2.4s hold on
    // unchanged text, and the pacing question phase A exists to answer cannot
    // be answered from the log at all.
    //
    // roundScore is what says which of the two a frame is: the engine writes it
    // when it scores the round and clears it when it commits, so it is non-zero
    // on exactly the scored frame and zero everywhere else.
    //
    // AND THAT LAST SENTENCE IS AN ASSUMPTION ABOUT THE ENGINE, not something
    // this repository pins. It holds because calculateRoundScore() returns
    // 5 + bid on a made bid and -|bid - actual| on a missed one, neither of
    // which can be 0 - but nothing here tests that, and a scoring change that
    // can yield 0 for a seat would quietly bring the identical-lines defect
    // back.
    //
    // The durable answer is for the snapshot to record which callback published
    // it, rather than the formatter reverse-engineering that from state. That
    // also answers "where does the dwell land" outright, which is phase A's
    // actual open question - so it belongs with whichever phase first needs
    // frame provenance for its own sake, not bolted on here.
    bool anyRoundScore = false;
    bool anyScore = false;

    for(const SeatView& seat : snapshot.seats)
    {
        anyRoundScore = anyRoundScore || seat.roundScore != 0;
        anyScore = anyScore || seat.roundScore != 0 || seat.totalScore != 0;
    }

    // Kept off the line until there is a score to show, same as won[] above: a
    // first-round bidding frame is not padded with four zeroes.
    if(anyScore)
    {
        QStringList scores;

        for(const SeatView& seat : snapshot.seats)
        {
            const QString name = QString::fromStdString(seat.name);

            // EVERY seat spelled the same way once any of them has a round
            // score, including one that scored +0 - the columns are being
            // compared down the log, and a seat that silently drops out of the
            // list is a seat a reader has to go looking for.
            if(anyRoundScore)
            {
                scores << QStringLiteral("%1:%2=%3").arg(name,
                                                         signedScore(seat.roundScore),
                                                         QString::number(seat.totalScore));
            }
            else
            {
                scores << QStringLiteral("%1:%2").arg(name, QString::number(seat.totalScore));
            }
        }

        parts << QStringLiteral("scores[%1]").arg(scores.join(QStringLiteral(" ")));
    }

    return parts.join(QStringLiteral("  "));
}

QString format(const BetPrompt& prompt)
{
    QStringList parts;

    parts << askPrefix();
    parts << QStringLiteral("bet?").leftJustified(6);

    // The generation, which is what a submission has to name to be accepted at
    // all. Printed because a prompt raised twice for one decision - the shape a
    // rejected answer takes, since makeBet() re-prompts rather than passing an
    // illegal bet on - is two lines here with different numbers on them, and is
    // otherwise a repeat that reads like a redraw.
    parts << QStringLiteral("#%1").arg(prompt.promptId).leftJustified(5);

    // The legal range, which is 0 through the hand size: BetContext carries no
    // trick count of its own, and the hand's size IS that number.
    parts << QStringLiteral("0..%1").arg(prompt.handSize).leftJustified(6);

    // One value or none, never a range. The engine fills it from
    // getForbiddenBet() for the final bidder only, and leaves it empty once the
    // bids already exceed the trick count.
    parts << QStringLiteral("forbidden=%1")
                 .arg(prompt.forbiddenBet ? QString::number(*prompt.forbiddenBet)
                                          : QStringLiteral("-")).leftJustified(13);

    parts << describe(prompt.roundType).leftJustified(8);
    parts << (prompt.isFirstPlayer ? QStringLiteral("first") : QStringLiteral("-")).leftJustified(5);

    // EMPTY IN A FOREHEAD OR HIDDEN ROUND, and the empty list is the point
    // rather than a gap in the log: the bidder is not entitled to their own
    // hand, so the prompt never carried it. A line showing cards in a blind
    // round is the leak the prompt shape exists to prevent, and it would show
    // up right here.
    parts << QStringLiteral("hand=[%1]").arg(describeEntries(prompt.hand));

    return parts.join(QStringLiteral("  "));
}

QString format(const PlayPrompt& prompt)
{
    QStringList parts;

    parts << askPrefix();
    parts << QStringLiteral("card?").leftJustified(6);
    parts << QStringLiteral("#%1").arg(prompt.promptId).leftJustified(5);

    parts << (prompt.blind ? QStringLiteral("blind") : QStringLiteral("-")).leftJustified(5);

    // The two halves of a blind prompt, spelled as what they are. `entries` is
    // empty and `handIndex` is not, so the hand is described by how many backs
    // to draw - which is handIndex.size(), and is what section 17 says the
    // widget clicks.
    if(prompt.blind)
        parts << QStringLiteral("backs=%1").arg(prompt.handIndex.size());
    else
        parts << QStringLiteral("hand=[%1]").arg(describeEntries(prompt.entries));

    parts << QStringLiteral("->[%1]").arg(describeIndex(prompt.handIndex));

    return parts.join(QStringLiteral("  "));
}

} // namespace snapshotlog
