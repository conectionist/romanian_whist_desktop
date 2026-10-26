#ifndef SCOREBOARD_ITEM_H
#define SCOREBOARD_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/CardRenderer.h"
#include "paint/Panel.h"
#include "paint/PictureMemo.h"
#include "paint/TextRuns.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QCoreApplication>
#include <QGraphicsObject>
#include <QSizeF>
#include <QString>

#include <cstddef>
#include <vector>

// SECTION 2.9's SCOREBOARD, pinned to the window's top-right corner (top-left in the
// mock-up; the owner's call of 2026-10-24): the round, the cards dealt and the round
// type; how the bids stand against the tricks; a row per seat in bid order; and the
// trump, its card stacked under its name.
//
// WHAT IT SAYS IS DECIDED HERE, in free functions that need no scene and no
// painter - the split SnapshotLog.h argues for and seatitem follows - and the item
// below only draws it.
//
// NEVER ANIMATED, which section 12 states as a rule rather than a preference: "a
// number that slides is a number you cannot read".
namespace scoreboard
{

// page.html's .score is 236 wide in its own units, which are drawn at
// tablespace::hudScale times the table's. Here it is the widest the panel grows:
// it is as wide as its columns (see columnsFor()), and names that would take it
// past this are elided.
inline constexpr qreal mockupWidth = 236.0;

// The first two lines: "Round 11 of 30", then "6 cards dealt · Normal".
struct Header
{
    QString round;
    QString dealt;
    QString roundType;

    // Forehead and Hidden are named in gold-bright (infostrip::isCalledOut()).
    bool calledOut = false;

    bool operator==(const Header&) const = default;
};

Header headerFor(const GameSnapshot& snapshot);

// "6 cards dealt", or "1 card dealt". Its own pair of strings rather than
// infostrip::cardsText() and a word after it: a translation may need the word
// anywhere.
QString dealtText(unsigned int trickCount);

// HOW THE BIDS STAND AGAINST THE TRICKS, with the number that matters in bold:
// "Bids so far: 3 of 6 tricks" while anyone has still to bid, then "Bids total 7
// for 6 tricks: over by 1", or "… under by 1". betsPlaced and betSum are the
// snapshot's own, so nothing here adds bids up.
textruns::Line tensionFor(const GameSnapshot& snapshot);

// A seat's run of exact bids, or of misses: RULES.md pays the fifth either way.
enum class Streak
{
    None,
    Winning,
    Losing
};

// One row, already spelled. The item places these and colours them; every
// decision about WHAT a cell says is made here, where it can be tested without a
// window.
struct Row
{
    QString name;

    // A DASH, NOT A ZERO, until this seat has bid. hasBet exists on the snapshot
    // precisely to tell a genuine bid of zero from no bid at all, and a scoreboard
    // that reads `bet` without it shows every seat bidding nothing and sitting
    // exactly on target for the whole of the bidding.
    QString bid;

    // A dash while the round is being bid: nobody has taken a trick, and a column
    // of zeros reads as a result.
    QString taken;

    // The engine's total, never the round added to it: inside onRoundScored() the
    // two deliberately disagree, which is section 16.1's "34 -> 41".
    QString points;

    // The run: "W2" for two exact bids running, "L3" for three misses, empty for
    // neither - which is never both, since one of them ends the other. THE LETTER
    // CARRIES WHICH, and the colour reinforces it (section 18). The owner's call
    // of 2026-10-20: the old round-score column went, and the streak stayed as
    // this.
    QString run;
    Streak streak = Streak::None;

    // Whether this seat is the one being asked to move: drawn gold-bright, with a
    // "▸" before the name. snapshot.activeSeat and nothing else, as the turn ring
    // round its panel is.
    bool turn = false;

    bool operator==(const Row&) const = default;
};

// One row per seat, IN BID ORDER (SeatView::bidOrder): who bids first this round
// is the top row. A seat whose order is not known yet sorts by its seat number.
std::vector<Row> rowsFor(const GameSnapshot& snapshot);

// The column headings, in the order they are drawn. Drawn in capitals, which is
// the font's doing, so a translation is written as a word rather than shouted.
QString headingPlayer();
QString headingBid();
QString headingTaken();
QString headingPoints();
QString headingRun();

// The trump: its card alone - or, in a round with no trump, no card and "None this
// round". The mock-up names the suit under its card ("♣ Clubs"); the client does
// not, by the owner's call of 2026-10-23: the card is a hand card's size, and its
// face says its suit.
struct Trump
{
    bool hasCard = false;
    romanian_whist::Card card{};
    QString name;

    bool operator==(const Trump&) const = default;
};

Trump trumpFor(const GameSnapshot& snapshot);

// EVERYTHING THE PANEL'S PICTURE SHOWS, worked out once per snapshot. The picture
// is painted from this and composed again only when it changes, so a thing drawn
// on the panel cannot be left out of that comparison: to be drawn it has to be in
// here (seatitem::Shown's rule).
struct Shown
{
    Header header;
    textruns::Line tension;
    std::vector<Row> rows;
    Trump trump;

    bool operator==(const Shown&) const = default;
};

Shown shownFor(const GameSnapshot& snapshot);

// THE TABLE'S COLUMNS, as a browser lays out the mock-up's: each column as wide
// as its widest content - its heading or its widest cell - and the room left over
// shared among them in proportion to that width, which is CSS's automatic table
// layout. That is what puts the mock-up's BID a little right of the middle and
// gives TAKEN the most air.
//
// THE PANEL IS AS WIDE AS ITS COLUMNS (the owner's call, 2026-10-20), not the
// mock-up's fixed 236 shared out: each column its widest content, and a gap before
// each number column. The cells measured are the names - each as though it
// carried the turn's "▸", which moves round the table - and for the numbers their
// widest value: so the columns do not shift as the turn and the scores move, and
// change only between games, with the names.
//
// Never narrower than the bid tension at its widest, so that line never wraps:
// columns that would be share the room over in proportion. Names that would take
// the panel past the mock-up's 236 are what gives way: every number column keeps
// its widest content, and the names get what is left, elided.
struct Columns
{
    qreal player = 0.0;
    qreal bid = 0.0;
    qreal taken = 0.0;
    qreal points = 0.0;
    qreal run = 0.0;

    qreal sum() const { return player + bid + taken + points + run; }

    bool operator==(const Columns&) const = default;
};

Columns columnsFor(const std::vector<Row>& rows);

// The panel's size in its own units, for what it shows: the content's width and
// the height its lines stack to, with its border and padding. `cards` is what
// knows how tall the trump card is.
QSizeF sizeFor(const Shown& shown, const CardRenderer& cards);

} // namespace scoreboard

class ScoreboardItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // `cards` draws the trump and `panels` is the corner panels' chrome cache, both
    // owned by TableScene, which outlives this. A null `panels` gives the item one
    // of its own.
    ScoreboardItem(const CardRenderer* cards, panel::Cache* panels, QGraphicsItem* parent = nullptr);

    // Everything this draws, from one snapshot.
    //
    // COMPOSED AGAIN ONLY WHEN WHAT IT SHOWS CHANGES, as a seat's panel is: the
    // scoreboard changes with each bid and each trick, and most of a game's ~1300
    // snapshots change none of it.
    void setSnapshot(const GameSnapshot& snapshot, const Theme& theme);

    // What the panel shows: READ-ONLY and for tests.
    const scoreboard::Shown& shown() const { return showing; }

    // The panel's size in its own units, its top-left at the item's origin. The
    // scene draws the item at tablespace::hudScale.
    QSizeF size() const { return panelSize; }

    QRectF boundingRect() const override;

    // The panel as drawn, which is what takes the mouse: a press on the scoreboard
    // never reaches what is under it.
    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // How many times the panel has been composed: READ-ONLY and for tests, as
    // SeatItem::compositions() is.
    std::size_t compositions() const { return memo.compositions(); }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    const panel::Picture& picture(qreal ratio) const;

    void paintContent(QPainter& painter, qreal ratio) const;

    const CardRenderer* cards;

    mutable panel::Cache ownPanels;
    panel::Cache* panels;

    Theme theme = Theme::dark();
    scoreboard::Shown showing;
    QSizeF panelSize;

    // columnsFor()'s for `showing`, worked out once when it changed and painted
    // from, rather than measured again on every composition.
    scoreboard::Columns columns;

    mutable PictureMemo memo;
};

#endif
