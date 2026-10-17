#include "TableScene.h"

#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/SeatItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"
#include "table/TrickItem.h"


#include <algorithm>

namespace
{

// THE INTERIM SEAT RING, which the redesign's phase 4 deletes.
//
// Phase 2 took away the layout the plaques used to be fitted with - an ellipse
// sized to the window - and phase 4 is what gives each seat its own spot (section
// 2.5's table) and its new panel. In between, the existing plaques sit on one
// fixed ellipse round the table, which seatlayout lays out exactly as it always
// has. The radii put the side seats and the top one where section 2.5 does at four
// players - (106, 512), (768, -81), (1430, 512) - and the human at (768, 1105),
// which is section 2.5's centre for the human's panel.
//
// AND A FIXED PLAQUE, from the same item at a unit of 150, which comes out 275 x
// 119: the size of section 2.5's panel give or take five units, so the interim
// table reads at the proportions the finished one will have. At five and six seats
// the diagonal plaques' inner corners reach about 25 units onto the wooden rim -
// never the felt - and phase 4's position table is what clears them.
constexpr qreal seatRadiusX = 662.0;
constexpr qreal seatRadiusY = 593.0;
constexpr qreal plaqueUnit = 150.0;

// How far inside the felt's edge a played card's outer end sits, and how far above
// the hand the human's card does (section 2.7: 60 in both places).
constexpr qreal playedInset = 60.0;

// THE HAND UNDER EVERY PLAQUE. Your own plaque overlaps the bottom of your fan
// (section 2.8: "the human's panel covers the bottom of the hand; that is
// intended"), and it is the plaque that has to win - it carries your name and
// your pill. The first rung of section 3's stacking order, which phase 3 sets out
// in full.
constexpr qreal handZ = 1.0;
constexpr qreal seatZ = 2.0;

// NOBODY SEATED, NO HAND. Section 14: a spectated table "has no human... and the
// bottom hand area is not drawn at all". The same range test seatlayout applies,
// so the two cannot disagree about whether somebody is sitting there.
bool seated(const GameSnapshot& snapshot)
{
    return snapshot.humanSeat >= 0
           && static_cast<std::size_t>(snapshot.humanSeat) < snapshot.seats.size();
}

}

TableScene::TableScene(QObject* parent)
    : QGraphicsScene(parent)
{
    // FIXED, and the whole of what a resize used to recompute. TableView fits
    // this box to the window; nothing in the scene is told the window's size.
    setSceneRect(tablespace::fitBox);

    trick = new TrickItem(&cards);
    addItem(trick);

    hand = new HandItem(&cards);
    hand->setZValue(handZ);
    addItem(hand);
}

void TableScene::setSnapshot(std::shared_ptr<const GameSnapshot> next)
{
    if(!next)
        return;

    snapshot = std::move(next);
    relayout();
}

void TableScene::setTheme(const Theme& palette)
{
    theme = palette;

    hand->setTheme(theme);

    // The room is drawn by TableView::drawBackground() rather than held in an
    // item, so this has to say so - nothing else would repaint it. Invalidating
    // the scene's background layer is what tells every view to redraw its own.
    invalidate(sceneRect(), QGraphicsScene::BackgroundLayer);
    relayout();
}

void TableScene::ensureSeats(std::size_t count)
{
    while(seats.size() < count)
    {
        SeatItem* seat = new SeatItem(&cards);
        seat->setZValue(seatZ);
        addItem(seat);
        seats.push_back(seat);
    }

    // Hidden rather than deleted: a second game may seat more players again,
    // and phase D starts one in the same process.
    for(std::size_t i = count ; i < seats.size() ; i++)
        seats[i]->hide();
}

std::vector<QPointF> TableScene::seatPositions(std::size_t seatCount) const
{
    return seatlayout::seatCentres(static_cast<unsigned int>(seatCount),
                                   snapshot ? snapshot->humanSeat : -1,
                                   seatlayout::ringRect(tablespace::centre,
                                                        seatRadiusX, seatRadiusY));
}

// SECTION 2.7'S PLACES, as near as phase 2 can have them. Each card sits on the
// line from the middle of the table toward its owner, with its outer end 60 inside
// the felt's edge; the human's sits above their hand instead, which is where a
// card you are about to play would go.
//
// THE OWNER'S DIRECTION IS THE SEAT'S NOMINAL ANGLE here, the same one the plaque
// ring uses, because the rack each card will line up with does not exist until
// phase 5 - and the same function as the plaques, so a card is always on the side
// of the table its owner sits on. One ring at one radius suffices where the old
// layout needed one sized per seat count: the cards are the mock-up's size, not a
// fit, and at six seats on a radius of 289 their centres stay 254 apart against a
// card 181 tall.
std::vector<QPointF> TableScene::trickPositions(std::size_t seatCount) const
{
    const qreal cardHeight = tablespace::playedCardHeight(seatCount);
    const qreal radius = tablespace::feltRadius - playedInset - cardHeight / 2.0;

    std::vector<QPointF> positions =
        seatlayout::seatCentres(static_cast<unsigned int>(seatCount),
                                snapshot ? snapshot->humanSeat : -1,
                                seatlayout::ringRect(tablespace::centre, radius));

    // seated() and not a range test of its own, so this and the hand cannot
    // disagree about whether you are at the table.
    if(snapshot && seated(*snapshot) && positions.size() == snapshot->seats.size())
    {
        positions[static_cast<std::size_t>(snapshot->humanSeat)] =
            QPointF(tablespace::centre.x(),
                    tablespace::handTop - playedInset - cardHeight / 2.0);
    }

    return positions;
}

std::optional<qreal> TableScene::overlayBottom() const
{
    if(!snapshot || snapshot->seats.empty())
        return std::nullopt;

    const std::size_t seatCount = snapshot->seats.size();
    const std::vector<QPointF> centres = seatPositions(seatCount);

    if(centres.size() != seatCount)
        return std::nullopt;

    const qreal plaqueHeight = SeatItem::plaqueSize(plaqueUnit).height();

    // The plaque nearest the bottom, which is the human's whenever there is one
    // - seatlayout puts the seat it is told about at bottom-centre. Found by
    // MEASURING rather than by indexing humanSeat, so a spectator game, where
    // seat 0 goes there instead, needs no branch.
    qreal lowestCentre = centres.front().y();

    for(const QPointF& centre : centres)
        lowestCentre = std::max(lowestCentre, centre.y());

    qreal bottom = lowestCentre - plaqueHeight / 2.0;

    // AND ABOVE THE HAND, which in table space sits higher than the plaque it is
    // drawn under: the strip must not cover the cards you are bidding on.
    if(seated(*snapshot))
        bottom = std::min(bottom, tablespace::handTop);

    // A GAP, so the strip does not sit flush against what it clears and read as
    // one object with it. A fraction of the plaque rather than a constant of its
    // own, as it always was.
    return bottom - plaqueHeight / 8.0;
}

void TableScene::relayout()
{
    if(!snapshot)
        return;

    const std::size_t seatCount = snapshot->seats.size();

    ensureSeats(seatCount);

    const std::vector<QPointF> centres = seatPositions(seatCount);

    if(centres.size() != seatCount)
        return;

    for(std::size_t i = 0 ; i < seatCount ; i++)
    {
        SeatItem* item = seats[i];

        item->setCardHeight(plaqueUnit);
        item->setPos(centres[i]);

        // activeSeat is -1 between turns, which is a real state and not a
        // missing one: no ring anywhere is what the table should show while
        // nobody is being asked to move.
        const bool active = snapshot->activeSeat >= 0
                            && static_cast<std::size_t>(snapshot->activeSeat) == i;

        item->setSeat(snapshot->seats[i], active, theme);
        item->show();
    }

    trick->setTrick(*snapshot, trickPositions(seatCount), tablespace::centre,
                    tablespace::playedCardHeight(seatCount), theme);

    // THE FAN FOLLOWS THE SNAPSHOT HERE AND NOWHERE ELSE. Between turns that is
    // the whole of it - drawn, never playable, since a snapshot carries no
    // legality and is empty in a blind round (section 4.0). MainWindow redraws it
    // from the prompt when it is your turn, which is the second source and the
    // only one that can be clicked.
    //
    // AND THE CALL IS REFUSED while a prompt is live: HandItem::setHand() holds
    // that line, not this file. This runs on every theme change as well as every
    // snapshot, and the colour scheme can flip while the engine is parked waiting
    // for a card - in a blind round the snapshot hand it would take is empty,
    // which would leave nothing to click and no way to answer.
    hand->setCardHeight(tablespace::handCardHeight);
    hand->setHand(snapshot->hand);

    // Section 2.8: the fan's top edge at y = 850, centred on the table. The
    // fan's origin is the middle of its cards, hence the half card.
    hand->setPos(tablespace::centre.x(), tablespace::handTop + tablespace::handCardHeight / 2.0);
    hand->setVisible(seated(*snapshot));
}
