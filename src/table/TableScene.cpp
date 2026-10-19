#include "TableScene.h"

#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/LeadMarkItem.h"
#include "table/RackItem.h"
#include "table/SeatItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"
#include "table/TrickItem.h"

#include <algorithm>

TableScene::TableScene(QObject* parent)
    : QGraphicsScene(parent)
{
    // FIXED, and the whole of what a resize used to recompute. TableView fits
    // this box to the window; nothing in the scene is told the window's size.
    setSceneRect(tablespace::fitBox);

    // Every item's place in the stack comes from tablespace::z, so no two files
    // can disagree about what is drawn over what.
    top = new TableTopItem;
    top->setTheme(theme);
    addItem(top);

    trick = new TrickItem(&cards, &shadowPictures);
    trick->setZValue(tablespace::z::playedCards);
    addItem(trick);

    leadMark = new LeadMarkItem;
    addItem(leadMark);

    hand = new HandItem(&cards);
    hand->setZValue(tablespace::z::hand);
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

    top->setTheme(theme);
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
        SeatItem* seat = new SeatItem(&panels);
        addItem(seat);
        seats.push_back(seat);

        RackItem* rack = new RackItem(&cards, &rackPictures);
        rack->setZValue(tablespace::z::racks);
        addItem(rack);
        racks.push_back(rack);
    }

    // Hidden rather than deleted: a second game may seat more players again,
    // and phase D starts one in the same process.
    for(std::size_t i = count ; i < seats.size() ; i++)
    {
        seats[i]->hide();
        racks[i]->hide();
    }
}

std::optional<qreal> TableScene::overlayBottom() const
{
    if(!snapshot || snapshot->seats.empty())
        return std::nullopt;

    const unsigned int seatCount = static_cast<unsigned int>(snapshot->seats.size());

    // The panel nearest the bottom, which is the human's whenever there is one
    // - seatlayout puts the seat it is told about at bottom-centre. Found by
    // MEASURING rather than by indexing humanSeat, so a spectator game, where
    // seat 0 goes there instead, needs no branch.
    std::optional<qreal> lowestTop;

    for(unsigned int seat = 0 ; seat < seatCount ; seat++)
    {
        const QRectF panel = seatlayout::panelRect(seatCount, snapshot->humanSeat, seat);

        if(!panel.isEmpty())
            lowestTop = std::max(lowestTop.value_or(panel.top()), panel.top());
    }

    if(!lowestTop)
        return std::nullopt;

    qreal bottom = *lowestTop;

    // AND ABOVE THE HAND, which in table space sits higher than the panel it is
    // drawn under: the strip must not cover the cards you are bidding on.
    if(seatlayout::humanSeated(seatCount, snapshot->humanSeat))
        bottom = std::min(bottom, tablespace::handTop);

    // A GAP, so the strip does not sit flush against what it clears and read as
    // one object with it. A fraction of the panel rather than a constant of its
    // own, as it always was.
    return bottom - tablespace::panelSize.height() / 8.0;
}

void TableScene::relayout()
{
    if(!snapshot)
        return;

    const std::size_t seatCount = snapshot->seats.size();

    ensureSeats(seatCount);

    // EVERY SEAT'S PLACE, ONCE: its panel, and its rack's angle, which the rack, its
    // played card and the rhombus all line up with.
    const seatlayout::Places places = seatlayout::places(static_cast<unsigned int>(seatCount),
                                                         snapshot->humanSeat);

    // Whether anyone has still to bid, which is all the pills need to know about
    // the phase: until then a bid is shown, not judged.
    const bool biddingOpen = snapshot->betsPlaced < seatCount;

    for(std::size_t i = 0 ; i < seatCount ; i++)
    {
        SeatItem* item = seats[i];

        const unsigned int seat = static_cast<unsigned int>(i);
        const unsigned int count = static_cast<unsigned int>(seatCount);
        const std::optional<seatlayout::Place>& place = places[i];

        RackItem* rack = racks[i];

        // A player count section 2.5 has no row for: the seat is not drawn rather
        // than drawn somewhere invented.
        if(!place)
        {
            item->hide();
            rack->hide();
            continue;
        }

        item->setPanelSize(place->panel.size());
        item->setPos(place->panel.center());

        // THE LOOK FOLLOWS THE PLACE, not the seat index: the bottom seat wears the
        // first, as the mock-up's "You" does, and the rest go clockwise from it.
        item->setPortrait(seatlayout::stepsFromBottom(count, snapshot->humanSeat, seat));

        // YOURS UNDER THE OTHERS, AND OVER YOUR HAND (tablespace::z). Set here
        // rather than when the panel is made, because which panel is yours is a
        // fact about this game: the items are reused from the last one.
        item->setZValue(static_cast<int>(i) == snapshot->humanSeat ? tablespace::z::humanPanel
                                                                   : tablespace::z::panels);

        // activeSeat is -1 between turns, which is a real state and not a
        // missing one: no ring anywhere is what the table should show while
        // nobody is being asked to move.
        const bool active = snapshot->activeSeat >= 0
                            && static_cast<std::size_t>(snapshot->activeSeat) == i;

        item->setSeat(snapshot->seats[i], active, biddingOpen, theme);
        item->show();

        // EVERY RACK BUT YOURS (section 2.6): your hand is the fan at the bottom. A
        // spectated game has nobody there, so seat 0's rack stands on the rim at the
        // bottom, in line with its panel.
        if(place->human)
        {
            rack->hide();
            continue;
        }

        rack->setPos(seatlayout::rackOrigin(place->angle));
        rack->setRotation(rackitem::frameRotation(place->angle));
        rack->setRack(snapshot->seats[i], theme);
        rack->show();
    }

    // THE TRICK AND WHO LEADS IT (section 2.7): each card in front of its owner's
    // rack, and the rhombus on the felt in line with the leader's.
    trick->setTrick(*snapshot, places, theme);

    const std::optional<unsigned int> lead = trickitem::leadSeat(*snapshot);
    std::optional<trickitem::Placement> mark;

    if(lead && *lead < places.size() && places[*lead])
        mark = trickitem::leadMarkPlacement(*places[*lead]);

    leadMark->setMark(mark, theme.table);

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
    // NOBODY SEATED, NO HAND. Section 14: a spectated table "has no human... and the
    // bottom hand area is not drawn at all".
    hand->setVisible(seatlayout::humanSeated(static_cast<unsigned int>(seatCount), snapshot->humanSeat));
}
