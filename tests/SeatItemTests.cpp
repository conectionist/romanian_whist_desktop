#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "paint/Avatars.h"
#include "paint/CardRenderer.h"
#include "paint/Fonts.h"
#include "paint/Panel.h"
#include "paint/Theme.h"
#include "table/SeatItem.h"
#include "table/TableSpace.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSizeF>
#include <QString>
#include <QtMath>

#include <string>
#include <vector>

using namespace romanian_whist;

// The name panel, which until C4 was the one item on the table with no test file
// at all - and B2b's four findings were every one of them in a painted widget of
// this shape.
//
// WHAT C4 ADDED TO IT is section 17's other half: in a Forehead round every seat
// but your own shows the card it is holding, face up, so you can bid on what
// everyone else has. The two things that must hold are that the PANEL DOES NOT
// CHANGE SIZE - it feeds the layout, so a panel that grew for two rounds would
// move what is laid out against it twice a game - and that the TEXT IS MEASURED
// at the width it is drawn into.
//
// THE TABLE REDESIGN'S PHASE 4 gave it section 2.5's panel: a portrait, the name
// and score, and a Taken/Bid pill; the cards-left stack went. PHASE 5 moved the
// Forehead card off the panel and into the seat's rack (RackItemTests), so the
// panel is the same in every round, and the cases that placed the card on it are
// retired. The score went in the owner's experiment of 2026-10-21, and the panel
// narrowed to its pill.
namespace
{

// What a seat that can be seen looks like: canSeeHand() allowed it, so the
// snapshot carries the card. Nothing here asks "is it a Forehead round" and the
// item does not either - a non-empty hand IS the permission.
SeatView seen(const QString& name, Card card)
{
    SeatView view;
    view.name = name.toStdString();
    view.seatIndex = 1;
    view.cardsLeft = 1;
    view.hand.push_back(HandEntry{ card, true });

    return view;
}

SeatView unseen(const QString& name, unsigned int cardsLeft = 5)
{
    SeatView view;
    view.name = name.toStdString();
    view.seatIndex = 1;
    view.cardsLeft = cardsLeft;

    return view;
}

SeatView bidding(unsigned int bet, unsigned int taken)
{
    SeatView view = unseen(QStringLiteral("Ana"));
    view.hasBet = true;
    view.bet = bet;
    view.tricksWon = taken;

    return view;
}

// Both sizes a panel comes in: everyone else's, and the bottom seat's.
const std::vector<QSizeF> panelSizes = { tablespace::panelSize, tablespace::bottomPanel.size() };

// A panel in a scene. Declaration order is load-bearing the same way HandItemTests'
// is: the chrome cache outlives the item and the item leaves the scene before the
// scene goes.
struct Plaque
{
    QGraphicsScene scene;
    panel::Cache panels;
    SeatItem item{ &panels };

    explicit Plaque(const SeatView& view)
    {
        item.setPanelSize(tablespace::panelSize);
        item.setSeat(view, false, false, Theme::dark());
        scene.addItem(&item);
    }
};

}

TEST_CASE("A seat whose hand can be seen draws the same panel as one whose cannot", "[seat]")
{
    // THE GUARANTEE THE OLD FACE-CARD CASES KEPT, and the redesign's phase 5 makes it
    // total: a Forehead round's card is drawn in the seat's rack, so the panel is
    // the same picture, the same size and carries no item of its own whether or not
    // the seat's hand is visible.
    Plaque showing(seen(QStringLiteral("Ana"), Card(Rank::King, Suit::Spades)));
    Plaque hiding(unseen(QStringLiteral("Ana"), 1));

    REQUIRE(showing.item.childItems().isEmpty());
    REQUIRE(showing.item.boundingRect() == hiding.item.boundingRect());
    const auto drawn = [](Plaque& plaque)
    {
        const QRectF bounds = plaque.item.sceneBoundingRect();

        QImage image(bounds.size().toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        plaque.scene.render(&painter, QRectF(QPointF(), bounds.size()), bounds);

        // ENDED BEFORE THE IMAGE LEAVES: returned with the painter still on it, the
        // image is only safe if the copy is elided (PHASE5_REVIEW_FINDINGS.md #7).
        painter.end();

        return image;
    };

    REQUIRE(drawn(showing) == drawn(hiding));
}

TEST_CASE("The panel's parts are section 2.5's sizes", "[seat]")
{
    // page.html's .plaque: a border of 1, padding of 8 and 16, an 88 x 100
    // portrait and a gap of 12 - which leaves the text 133 units of a 259 panel,
    // yours as well as everyone else's.
    const seatitem::Layout other = seatitem::layoutFor(tablespace::panelSize);
    const seatitem::Layout yours = seatitem::layoutFor(tablespace::bottomPanel.size());

    REQUIRE(other.panel.size() == QSizeF(259.0, 118.0));
    REQUIRE(other.portrait.size() == QSizeF(88.0, 100.0));
    REQUIRE(other.portrait.topLeft() == other.panel.topLeft() + QPointF(9.0, 9.0));
    REQUIRE(other.text.width() == 133.0);
    REQUIRE(yours.text.width() == 133.0);

    // Centred on the item's origin, which TableScene puts at the panel's centre.
    REQUIRE(other.panel.center() == QPointF(0.0, 0.0));

    // NOTHING IN THE TEXT COLUMN OVERLAPS THE PORTRAIT, and both stay inside.
    for(const QSizeF& size : panelSizes)
    {
        const seatitem::Layout layout = seatitem::layoutFor(size);

        REQUIRE_FALSE(layout.text.intersects(layout.portrait));
        REQUIRE(layout.panel.contains(layout.portrait));
        REQUIRE(layout.panel.contains(layout.text));
    }
}

// AS WIDE AS THE WIDEST PILL AND NO WIDER (the owner's experiment of 2026-10-21):
// every pill the game can show fits whole, and the column has under a unit to
// spare. Taken and bid run 0 to 8.
TEST_CASE("The text column is as wide as the widest pill", "[seat]")
{
    QFont font = fonts::body();
    font.setPixelSize(seatitem::pillSize);
    const QFontMetricsF metrics(font);

    // The pill's padding of 12 and its border of 1, either side.
    const qreal frame = 2.0 * (12.0 + 1.0);

    std::vector<QString> texts = { seatitem::pillFor(unseen(QStringLiteral("Ana")), true, true).text,
                                   seatitem::pillFor(unseen(QStringLiteral("Ana")), false, true).text };

    for(unsigned int taken = 0 ; taken <= 8 ; taken++)
        for(unsigned int bet = 0 ; bet <= 8 ; bet++)
            texts.push_back(seatitem::pillFor(bidding(bet, taken), false, false).text);

    qreal widest = 0.0;

    for(const QString& text : texts)
        widest = std::max(widest, metrics.horizontalAdvance(text) + frame);

    const qreal column = seatitem::layoutFor(tablespace::panelSize).text.width();

    INFO("widest pill " << widest << ", column " << column);
    REQUIRE(widest <= column);
    REQUIRE(column - widest < 1.0);
}

TEST_CASE("A long name is elided to the column", "[seat]")
{
    // A name from section 13's dialog is arbitrary text, so it gives way rather
    // than run past the panel.
    SeatView view = unseen(QStringLiteral("Bartholomew Featherstonehaugh"));

    const qreal width = seatitem::layoutFor(tablespace::panelSize).text.width();

    for(const bool active : { false, true })
    {
        INFO("active " << active);

        const seatitem::NameLine line = seatitem::nameLineFor(view, active, width);

        QFont font = fonts::heading();
        font.setPixelSize(25);
        const QFontMetricsF metrics(font);

        REQUIRE(line.name != QString::fromStdString(view.name));

        const qreal marker = line.marker.isEmpty() ? 0.0 : metrics.horizontalAdvance(line.marker) + 6.0;

        REQUIRE(marker + metrics.horizontalAdvance(line.name) <= width);
    }
}

TEST_CASE("A short name is drawn whole", "[seat]")
{
    SeatView view = unseen(QStringLiteral("Ana"));
    view.totalScore = 44;

    const seatitem::NameLine line = seatitem::nameLineFor(view, false, 133.0);

    REQUIRE(line.marker.isEmpty());
    REQUIRE(line.name == QStringLiteral("Ana"));
}

TEST_CASE("Only the seat to move carries the marker", "[seat]")
{
    // SECTION 18: never colour alone. The ring round the active panel is gold;
    // the "▸" before its name is the shape that says the same thing.
    const SeatView view = unseen(QStringLiteral("Ana"));

    REQUIRE(seatitem::nameLineFor(view, true, 154.0).marker == QStringLiteral("▸"));
    REQUIRE(seatitem::nameLineFor(view, false, 154.0).marker.isEmpty());
}

TEST_CASE("Before a seat bids its pill says so", "[seat]")
{
    const SeatView view = unseen(QStringLiteral("Ana"));

    SECTION("waiting for its turn")
    {
        const seatitem::PillView pill = seatitem::pillFor(view, false, true);

        REQUIRE(pill.text == QStringLiteral("No bid yet"));
        REQUIRE(pill.state == seatitem::Pill::Neutral);
    }

    SECTION("being asked")
    {
        const seatitem::PillView pill = seatitem::pillFor(view, true, true);

        REQUIRE(pill.text == QStringLiteral("Bidding…"));
        REQUIRE(pill.state == seatitem::Pill::Neutral);
    }

    SECTION("asked to play, which is not to bid")
    {
        REQUIRE(seatitem::pillFor(view, true, false).text == QStringLiteral("No bid yet"));
    }
}

TEST_CASE("A bid of zero is a bid and not no bid", "[seat]")
{
    // hasBet IS THE WHOLE DIFFERENCE. A seat that bid nothing and has taken
    // nothing is on target; one that has not bid is not anything yet.
    const seatitem::PillView pill = seatitem::pillFor(bidding(0, 0), false, false);

    REQUIRE(pill.text == QStringLiteral("Taken/Bid: 0/0"));
    REQUIRE(pill.state == seatitem::Pill::OnTarget);
}

TEST_CASE("A bid is shown but not judged while others still bid", "[seat]")
{
    // The mock-up colours nothing until the bidding is over: nobody has taken a
    // trick, so "on target" would be a claim about nothing.
    const seatitem::PillView pill = seatitem::pillFor(bidding(2, 0), false, true);

    REQUIRE(pill.text == QStringLiteral("Taken/Bid: 0/2"));
    REQUIRE(pill.state == seatitem::Pill::Neutral);
}

TEST_CASE("Once play starts the pill says on target or owing or over", "[seat]")
{
    // TAKEN FIRST, THEN THE BID, and the numbers always in the text: the colour
    // makes the state quick, the text makes it knowable (section 18).
    SECTION("on target")
    {
        const seatitem::PillView pill = seatitem::pillFor(bidding(2, 2), false, false);

        REQUIRE(pill.text == QStringLiteral("Taken/Bid: 2/2"));
        REQUIRE(pill.state == seatitem::Pill::OnTarget);
    }

    SECTION("owing")
    {
        const seatitem::PillView pill = seatitem::pillFor(bidding(3, 1), true, false);

        REQUIRE(pill.text == QStringLiteral("Taken/Bid: 1/3"));
        REQUIRE(pill.state == seatitem::Pill::Owing);
    }

    SECTION("over")
    {
        const seatitem::PillView pill = seatitem::pillFor(bidding(1, 2), false, false);

        REQUIRE(pill.text == QStringLiteral("Taken/Bid: 2/1"));
        REQUIRE(pill.state == seatitem::Pill::Over);
    }
}

TEST_CASE("The panel takes the mouse and its shadow does not", "[seat]")
{
    // The bounding rect holds everything the panel paints past its edge - the
    // shadow, and the ring and glow whether or not they are showing - while the
    // shape is the panel alone, so a click on the shadow still reaches what is
    // beneath it.
    Plaque plaque(unseen(QStringLiteral("Ana")));

    const QRectF panelRect = plaque.item.layoutFor().panel;

    REQUIRE(plaque.item.boundingRect() == panelRect.marginsAdded(panel::reach()));
    REQUIRE(plaque.item.shape().boundingRect() == panelRect);

    // AND THE TURN DOES NOT MOVE IT: a rect that changed as the turn moved would
    // re-index the whole scene twice a card.
    const QRectF idle = plaque.item.boundingRect();

    plaque.item.setSeat(unseen(QStringLiteral("Ana")), true, true, Theme::dark());

    REQUIRE(plaque.item.boundingRect() == idle);
}

TEST_CASE("A snapshot that changes nothing on a panel composes nothing", "[seat]")
{
    // ~1300 SNAPSHOTS A GAME, and most change one seat's pill at most. The panel
    // is one cached picture, composed again only when what it shows changes - the
    // cards left are on the snapshot and not on the panel.
    Plaque plaque(bidding(2, 0));

    QImage image(400, 300, QImage::Format_ARGB32_Premultiplied);
    const auto render = [&]
    {
        image.fill(Qt::transparent);
        QPainter painter(&image);
        plaque.scene.render(&painter, QRectF(), plaque.item.sceneBoundingRect());
    };

    render();
    const std::size_t first = plaque.item.compositions();

    REQUIRE(first > 0);

    SeatView fewer = bidding(2, 0);
    fewer.cardsLeft = 3;
    plaque.item.setSeat(fewer, false, false, Theme::dark());
    render();

    REQUIRE(plaque.item.compositions() == first);

    // And one that does, does.
    plaque.item.setSeat(bidding(2, 1), false, false, Theme::dark());
    render();

    REQUIRE(plaque.item.compositions() == first + 1);

    plaque.item.setSeat(bidding(2, 1), true, false, Theme::dark());
    render();

    REQUIRE(plaque.item.compositions() == first + 2);
}

// ONE CHROME FOR EVERY PANEL ALIKE. Five of six seats are the same size at the
// same ratio, and each rendering its own blurred the same two shadows five times
// over on every settle of a resize.
TEST_CASE("Panels of one size and state share one chrome", "[seat]")
{
    QGraphicsScene scene;
    panel::Cache panels;
    SeatItem left(&panels);
    SeatItem right(&panels);

    for(SeatItem* item : { &left, &right })
    {
        item->setPanelSize(tablespace::panelSize);
        item->setSeat(unseen(QStringLiteral("Ana")), false, false, Theme::dark());
        scene.addItem(item);
    }

    right.setPos(400.0, 0.0);

    QImage image(1000, 400, QImage::Format_ARGB32_Premultiplied);
    const auto render = [&]
    {
        image.fill(Qt::transparent);
        QPainter painter(&image);
        scene.render(&painter, QRectF(), scene.itemsBoundingRect());
    };

    render();

    REQUIRE(left.compositions() == 1);
    REQUIRE(right.compositions() == 1);
    REQUIRE(panels.renders() == 1);

    // The active seat's chrome is the other one, rendered once and then shared.
    right.setSeat(unseen(QStringLiteral("Ana")), true, false, Theme::dark());
    render();

    REQUIRE(panels.renders() == 2);

    left.setSeat(unseen(QStringLiteral("Ana")), true, false, Theme::dark());
    render();

    REQUIRE(panels.renders() == 2);
}

// THE PORTRAIT'S ROWS ARE ITS OWN. A QBrush tiles its pixmap, so a rounded rect
// that starts a fraction of a pixel before the picture samples the row before its
// first - the picture's last, its coat - and drew a hairline of it along the top
// and bottom of every portrait. Painted at a fit ratio that puts the portrait off
// the pixel grid, with two different faces: the rows outside the picture must not
// change with the face, and the rows inside must be the picture's, one for one.
TEST_CASE("A portrait's edge shows no row from its other edge", "[seat]")
{
    const qreal ratio = 0.767;

    Plaque plaque(unseen(QStringLiteral("Ana")));
    const seatitem::Layout layout = plaque.item.layoutFor();
    const QRectF inner = layout.portrait.adjusted(1.0, 1.0, -1.0, -1.0);

    // The panel's corner on a whole pixel, as the table's view puts it.
    const QPointF origin(20.0, 20.0);
    const auto device = [&](QPointF point) { return origin + (point - layout.panel.topLeft()) * ratio; };

    const auto paintWith = [&](const std::string& look)
    {
        plaque.item.setPortrait(look);

        QImage image(qCeil(layout.panel.width() * ratio) + 40, qCeil(layout.panel.height() * ratio) + 40,
                     QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::black);

        QPainter painter(&image);
        painter.translate(origin);
        painter.scale(ratio, ratio);
        painter.translate(-layout.panel.topLeft());
        plaque.item.paint(&painter, nullptr, nullptr);

        return image;
    };

    const QImage first = paintWith(avatars::catalogue().at(0).id);
    const QImage second = paintWith(avatars::catalogue().at(1).id);

    const int top = qRound(device(inner.topLeft()).y());
    const QImage avatar = Theme::dark().avatar(avatars::catalogue().at(0).id, inner.size().toSize(), ratio).toImage();

    const int left = qRound(device(inner.topLeft()).x());
    const int from = left + avatar.width() / 4;
    const int to = left + avatar.width() * 3 / 4;

    for(int x = from ; x < to ; x++)
    {
        INFO("column " << x);

        REQUIRE(first.pixel(x, top - 1) == second.pixel(x, top - 1));
        REQUIRE(first.pixel(x, top + avatar.height()) == second.pixel(x, top + avatar.height()));

        for(int row = 0 ; row < avatar.height() ; row++)
        {
            INFO("row " << row);
            REQUIRE(first.pixel(x, top + row) == avatar.pixel(x - left, row));
        }
    }
}
