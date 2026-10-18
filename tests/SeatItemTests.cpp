#include <catch2/catch_test_macros.hpp>

#include "CardStringMaker.h"

#include "paint/CardRenderer.h"
#include "paint/Fonts.h"
#include "paint/Panel.h"
#include "paint/Theme.h"
#include "table/CardItem.h"
#include "table/SeatItem.h"
#include "table/TableSpace.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QSizeF>
#include <QString>

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
// and score, and a Taken/Bid pill. The card now stands in the portrait's place
// until phase 5 moves it to the rack, so the text column no longer narrows for
// it, and the cards-left stack is gone (phase 5's racks show the count).
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

QSizeF faceSize()
{
    CardRenderer renderer;

    return renderer.sizeForHeight(seatitem::faceHeight());
}

// Both sizes a panel comes in: everyone else's, and the bottom seat's.
const std::vector<QSizeF> panelSizes = { tablespace::panelSize, tablespace::bottomPanel.size() };

// A panel in a scene, so its child items can be asked about. Declaration order
// is load-bearing the same way HandItemTests' is: the renderer outlives the item
// and the item leaves the scene before the scene goes.
struct Plaque
{
    QGraphicsScene scene;
    CardRenderer renderer;
    SeatItem item{ &renderer };

    explicit Plaque(const SeatView& view)
    {
        item.setPanelSize(tablespace::panelSize);
        item.setSeat(view, false, false, Theme::dark());
        scene.addItem(&item);
    }

    // The face-up card, if one is showing. Found by type rather than tracked, so
    // the test asks the item what it built.
    CardItem* face() const
    {
        for(QGraphicsItem* child : item.childItems())
        {
            if(auto* card = dynamic_cast<CardItem*>(child))
            {
                if(card->isVisible())
                    return card;
            }
        }

        return nullptr;
    }
};

}

TEST_CASE("The panel is the same size whether or not a card is shown", "[seat]")
{
    // THE GUARANTEE THE WHOLE PLACEMENT DECISION EXISTS TO KEEP. A panel that
    // grew for the two blind rounds would move what is laid out against it twice
    // a game, which is the defect B2b recorded against the info strip, one layer
    // out.
    Plaque showing(seen(QStringLiteral("Ana"), Card(Rank::King, Suit::Spades)));
    Plaque hiding(unseen(QStringLiteral("Ana")));

    REQUIRE(showing.face() != nullptr);
    REQUIRE(hiding.face() == nullptr);

    REQUIRE(showing.item.boundingRect() == hiding.item.boundingRect());
    REQUIRE(showing.item.layoutFor().panel == hiding.item.layoutFor().panel);
}

TEST_CASE("A seat whose hand can be seen shows it, and one whose cannot does not", "[seat]")
{
    // BOTH DIRECTIONS, so a panel that always drew a card fails too. The empty
    // case is the ordinary one - twenty-four rounds of twenty-six, plus every
    // seat in a Hidden round, plus your own seat in a Forehead one.
    Plaque showing(seen(QStringLiteral("Bogdan"), Card(Rank::Ace, Suit::Hearts)));

    REQUIRE(showing.face() != nullptr);

    Plaque hiding(unseen(QStringLiteral("Bogdan")));

    REQUIRE(hiding.face() == nullptr);
}

TEST_CASE("Your own seat shows no face in a Forehead round", "[seat]")
{
    // THE CASE THAT WOULD LEAK YOUR OWN CARD TO YOU. canSeeHand(human, human) is
    // false in Forehead, so refreshFromEngine() leaves your own seat's hand empty
    // - and the panel has to fall back to the portrait rather than inventing
    // something to draw. Section 17: your card is face-down in your own fan and
    // nowhere else.
    SeatView mine = unseen(QStringLiteral("You"), 1);
    mine.isHuman = true;

    REQUIRE(mine.hand.empty());

    Plaque plaque(mine);

    REQUIRE(plaque.face() == nullptr);
}

TEST_CASE("The panel's parts are section 2.5's sizes", "[seat]")
{
    // page.html's .plaque: a border of 1, padding of 8 and 16, an 88 x 100
    // portrait and a gap of 12 - which leaves the text 154 units of a 280 panel
    // and 174 of your 300.
    const seatitem::Layout other = seatitem::layoutFor(tablespace::panelSize, QSizeF());
    const seatitem::Layout yours = seatitem::layoutFor(tablespace::bottomPanel.size(), QSizeF());

    REQUIRE(other.panel.size() == QSizeF(280.0, 118.0));
    REQUIRE(other.portrait.size() == QSizeF(88.0, 100.0));
    REQUIRE(other.portrait.topLeft() == other.panel.topLeft() + QPointF(9.0, 9.0));
    REQUIRE(other.text.width() == 154.0);
    REQUIRE(yours.text.width() == 174.0);

    // Centred on the item's origin, which TableScene puts at the panel's centre.
    REQUIRE(other.panel.center() == QPointF(0.0, 0.0));

    // NOTHING IN THE TEXT COLUMN OVERLAPS THE PORTRAIT, and both stay inside.
    for(const QSizeF& size : panelSizes)
    {
        const seatitem::Layout layout = seatitem::layoutFor(size, QSizeF());

        REQUIRE_FALSE(layout.text.intersects(layout.portrait));
        REQUIRE(layout.panel.contains(layout.portrait));
        REQUIRE(layout.panel.contains(layout.text));
    }
}

TEST_CASE("The face-up card stands in the portrait's place", "[seat]")
{
    // Until the redesign's phase 5 gives it the rack, the card takes the
    // portrait's box: as tall as the portrait, so it is as large as the panel
    // allows, and narrower, so it fits inside the frame.
    for(const QSizeF& size : panelSizes)
    {
        const seatitem::Layout layout = seatitem::layoutFor(size, faceSize());

        REQUIRE_FALSE(layout.card.isEmpty());
        REQUIRE(layout.portrait.contains(layout.card));
        REQUIRE(layout.card.center() == layout.portrait.center());
    }
}

TEST_CASE("The text column is the same with or without a card", "[seat]")
{
    // B2b'S FINDING, one panel further on: a widget measuring itself with
    // something other than what it later draws with. The card no longer takes
    // width from the text, so the name is measured against one column in every
    // round - and still never runs under the card.
    const seatitem::Layout without = seatitem::layoutFor(tablespace::panelSize, QSizeF());
    const seatitem::Layout with = seatitem::layoutFor(tablespace::panelSize, faceSize());

    REQUIRE(with.text == without.text);
    REQUIRE_FALSE(with.text.intersects(with.card));
}

TEST_CASE("A seat with nothing to show lays out as though nothing were there", "[seat]")
{
    // THE OTHER CONSEQUENCE of believing you have a card, and the one the card
    // itself does not reveal: refreshFace() checks the hand again before it
    // dereferences, so a seat that wrongly thought it had one would still draw no
    // card - and would draw no portrait either, leaving a hole in the panel for
    // twenty-four rounds of twenty-six. Nothing would crash.
    Plaque hiding(unseen(QStringLiteral("Cristina")));

    REQUIRE_FALSE(hiding.item.showsFace());
    REQUIRE(hiding.item.layoutFor().card.isEmpty());
}

TEST_CASE("A seat with a card to show puts it where its portrait was", "[seat]")
{
    // The same question from the other side, asked of the ITEM rather than of the
    // free function - so the panel's own decision is under test and not just the
    // arithmetic it delegates to.
    Plaque showing(seen(QStringLiteral("Dan"), Card(Rank::Ten, Suit::Clubs)));

    REQUIRE(showing.item.showsFace());

    const seatitem::Layout layout = showing.item.layoutFor();

    REQUIRE_FALSE(layout.card.isEmpty());
    REQUIRE(showing.face()->pos() == layout.card.center());
}

TEST_CASE("A long name is elided and its score still shows", "[seat]")
{
    // A name from section 13's dialog is arbitrary text, and the score after it is
    // the one number every panel owes the player - so the name gives way and the
    // score does not.
    SeatView view = unseen(QStringLiteral("Bartholomew Featherstonehaugh"));
    view.totalScore = -123;

    const qreal width = seatitem::layoutFor(tablespace::panelSize, QSizeF()).text.width();

    for(const bool active : { false, true })
    {
        INFO("active " << active);

        const seatitem::NameLine line = seatitem::nameLineFor(view, active, width);

        QFont font = fonts::heading();
        font.setPixelSize(25);
        const QFontMetricsF metrics(font);

        REQUIRE(line.name != QString::fromStdString(view.name));
        REQUIRE(line.score == QStringLiteral(" (-123)"));

        const qreal marker = line.marker.isEmpty() ? 0.0 : metrics.horizontalAdvance(line.marker) + 6.0;

        REQUIRE(marker + metrics.horizontalAdvance(line.name) + metrics.horizontalAdvance(line.score)
                <= width);
    }
}

TEST_CASE("A short name is drawn whole", "[seat]")
{
    SeatView view = unseen(QStringLiteral("Ana"));
    view.totalScore = 44;

    const seatitem::NameLine line = seatitem::nameLineFor(view, false, 154.0);

    REQUIRE(line.marker.isEmpty());
    REQUIRE(line.name == QStringLiteral("Ana"));
    REQUIRE(line.score == QStringLiteral(" (44)"));
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
