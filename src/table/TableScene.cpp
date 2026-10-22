#include "TableScene.h"

#include "table/BidStripItem.h"
#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/LeadMarkItem.h"
#include "table/RackItem.h"
#include "table/ScoreboardItem.h"
#include "table/SeatItem.h"
#include "table/SeatLayout.h"
#include "table/TableButtonItem.h"
#include "table/TableSpace.h"
#include "table/TableTopItem.h"
#include "table/TrickItem.h"
#include "table/TrickPanelItem.h"
#include "table/Turn.h"

#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>

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

    hand = new HandItem(&cards, &shadowPictures);
    hand->setZValue(tablespace::z::hand);
    addItem(hand);

    // THE SCOREBOARD (section 2.9), drawn larger than the table as the mock-up's
    // is, and hidden until placeHud() has a corner and a game for it.
    scoreboard = new ScoreboardItem(&cards, &hudPanels);
    scoreboard->setZValue(tablespace::z::hud);
    scoreboard->setScale(tablespace::hudScale);
    scoreboard->hide();
    addItem(scoreboard);

    // THE TRICK PANEL (section 2.10), the same.
    trickPanel = new TrickPanelItem(&hudPanels);
    trickPanel->setZValue(tablespace::z::hud);
    trickPanel->setScale(tablespace::hudScale);
    trickPanel->hide();
    addItem(trickPanel);

    // THE CORNER BUTTONS (section 2.11). MainWindow opens their sheets on a click
    // (phase 11; phase 8 drew them disabled until there were sheets to open).
    history = new TableButtonItem(icons::Icon::RoundHistory, tr("Round history"), &panels);
    lastTrick = new TableButtonItem(icons::Icon::LastTrick, tr("Last trick"), &panels);

    for(TableButtonItem* button : { history, lastTrick })
    {
        button->setZValue(tablespace::z::hud);
        button->setTheme(theme);
        button->hide();
        addItem(button);
    }

    // THE BID STRIP (section 2.13), at the seats' scale and so on their chrome
    // cache. It places itself, and shows only while it holds a prompt.
    bidStrip = new BidStripItem(&panels);
    addItem(bidStrip);
}

void TableScene::setSnapshot(std::shared_ptr<const GameSnapshot> next)
{
    if(!next)
        return;

    snapshot = std::move(next);
    relayout();
}

void TableScene::setMotion(motion::Pace pace)
{
    animationPace = pace;
}

void TableScene::setTheme(const Theme& palette)
{
    theme = palette;

    top->setTheme(theme);
    hand->setTheme(theme);
    history->setTheme(theme);
    lastTrick->setTheme(theme);

    // Handed over by relayout() below whenever there is a snapshot - only a theme
    // set before the first one has to reach the strip from here.
    if(!snapshot)
        bidStrip->setSnapshot(snapshot, theme);

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

void TableScene::pinHud(const QRectF& visible, qreal scale)
{
    if(visible.isEmpty() || scale <= 0.0)
        return;

    pin = Pin{ visible, scale };
    placeHud();
}

void TableScene::placeHud()
{
    const bool shown = pin && snapshot;

    scoreboard->setVisible(shown);
    trickPanel->setVisible(shown);

    for(TableButtonItem* button : { history, lastTrick })
        button->setVisible(shown);

    if(!shown)
        return;

    // The inset is in the window's pixels, so in table units it is larger the
    // smaller the table is drawn.
    const qreal inset = tablespace::hudInset / pin->scale;

    // TOP-LEFT, growing right and down.
    scoreboard->setPos(pin->visible.left() + inset, pin->visible.top() + inset);

    // TOP-RIGHT, growing left and down: placed by its right edge, at its drawn
    // width.
    trickPanel->setPos(pin->visible.right() - inset - trickPanel->size().width() * tablespace::hudScale,
                       pin->visible.top() + inset);

    // BOTTOM-LEFT, side by side (table.js fit()).
    const QPointF corner(pin->visible.left() + inset, pin->visible.bottom() - inset);

    history->setPos(corner.x(), corner.y() - history->size().height());
    lastTrick->setPos(history->pos().x() + history->size().width() + tablespace::buttonGap,
                      corner.y() - lastTrick->size().height());
}

bool TableScene::event(QEvent* event)
{
    // A view sends the scene its mouse moves, with or without a button down, and
    // a leave when the cursor goes off it; a test may send hover moves straight to
    // the scene. All three say where the cursor is.
    bool moved = true;

    switch(event->type())
    {
    case QEvent::GraphicsSceneMouseMove:
        cursor = static_cast<QGraphicsSceneMouseEvent*>(event)->scenePos();
        break;

    case QEvent::GraphicsSceneHoverEnter:
    case QEvent::GraphicsSceneHoverMove:
        cursor = static_cast<QGraphicsSceneHoverEvent*>(event)->scenePos();
        break;

    case QEvent::GraphicsSceneHoverLeave:
    case QEvent::GraphicsSceneLeave:
        cursor.reset();
        break;

    default:
        moved = false;
        break;
    }

    // And the strip hears it: a bid it lit under a resting cursor is one the scene
    // would never tell it the cursor had left (BidStripItem::followCursor()).
    if(moved)
        bidStrip->followCursor(cursor);

    return QGraphicsScene::event(event);
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

        rack->setPos(seatlayout::rackOrigin(*place->angle));
        turnTo(*rack, rackitem::frameRotation(*place->angle));
        rack->setRack(snapshot->seats[i], theme);
        rack->show();
    }

    // THE TRICK AND WHO LEADS IT (section 2.7): each card in front of its owner's
    // rack, and the rhombus on the felt in line with the leader's.
    trick->setTrick(*snapshot, places, theme);

    const std::optional<unsigned int> lead = trickitem::leadSeat(*snapshot);
    std::optional<tablespace::Placement> mark;

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
    //
    // THE FAN LAYS ITSELF OUT in table space (handitem::cardPlacement(), section
    // 2.8), so the item stays at the scene's origin, as the trick does.
    hand->setCardHeight(tablespace::handCardHeight);
    hand->setHand(snapshot->hand);

    // NOBODY SEATED, NO HAND. Section 14: a spectated table "has no human... and the
    // bottom hand area is not drawn at all".
    hand->setVisible(seatlayout::humanSeated(static_cast<unsigned int>(seatCount), snapshot->humanSeat));

    // The corner panels, shown from the first snapshot on.
    scoreboard->setSnapshot(*snapshot, theme);
    trickPanel->setSnapshot(snapshot, theme);
    placeHud();

    // And the strip, which reads how the others have bid from the frame.
    bidStrip->setSnapshot(snapshot, theme);
}
