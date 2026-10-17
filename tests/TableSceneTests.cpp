#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "table/TableScene.h"
#include "table/CardItem.h"
#include "table/HandItem.h"
#include "table/SeatItem.h"
#include "table/TableSpace.h"
#include "table/TrickItem.h"
#include "engine/Prompts.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <romanian_whist/Card.h>

#include <QCoreApplication>
#include <QGraphicsItem>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QTransform>

#include <cmath>
#include <memory>
#include <vector>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// THE HALF OF THE dpr CONTRACT THAT IS NOT THE PIXMAP'S.
//
// Theme::room() rasterizes at view * dpr and stamps the ratio on, and ThemeTests
// already holds that to account. What no test there covers is the other end:
// drawBackground() handing that pixmap to a painter. A drawPixmap TARGET rect is
// logical and a SOURCE rect is not - Qt reads the source in the pixmap's own
// pixels - so a scene that passes the same rect as both samples the top-left
// 1/dpr of the room and stretches it over the whole viewport.
//
// That is invisible at dpr 1, which is where the suite and the developer's
// screen both are, and it is the reason this file renders at 2 rather than
// asserting anything about the picture itself.
//
// AND UNDER A VIEW TRANSFORM, since the table redesign's phase 2: the scene is in
// table space, the view scales it, and the room is drawn in the viewport's own
// pixels regardless. So each case paints through the kind of transform TableView
// sets - a scale and an offset - and expects the room laid over the canvas as if
// there were none.
namespace
{

// drawBackground() is protected, which is right - nothing outside the scene
// should be able to repaint the table. A using-declaration is the whole of what
// a test needs to reach it, and it needs no friend on the class under test.
struct ProbeScene : TableScene
{
    using TableScene::drawBackground;
};

const QSize view(320, 200);

constexpr qreal ratio = 2.0;

// WHAT A VIEW WOULD SET: table space scaled down and moved in. Powers of two and
// whole numbers, so the transform and its inverse are exact in floating point and
// a pixel comparison compares the blit rather than a rounding error. The fit box
// lands at (58.5, 21.25), 235 x 165.5 - inside the canvas with room all round,
// which is the letterbox the room has to cover as well.
const QTransform viewTransform = QTransform::fromTranslate(80.0, 40.0).scale(0.125, 0.125);

// A device-pixel-ratio-2 canvas the size the scene believes it is: 640x400
// physical pixels laid out over 320x200 logical ones, which is exactly what a
// viewport on a retina screen hands its scene.
QImage canvas()
{
    QImage image(view * ratio, QImage::Format_ARGB32);

    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);

    return image;
}

// The canvas region given, as the scene rect QGraphicsView would report exposed.
QRectF exposedScene(const QRectF& onCanvas)
{
    return viewTransform.inverted().mapRect(onCanvas);
}

QImage expectedRoom()
{
    return Theme::light().room(view, ratio).toImage().convertToFormat(QImage::Format_ARGB32);
}

}

TEST_CASE("The room is blitted 1:1 at a device pixel ratio of 2", "[table]")
{
    ProbeScene scene;

    scene.setTheme(Theme::light());

    QImage rendered = canvas();

    {
        QPainter painter(&rendered);
        painter.setWorldTransform(viewTransform);
        scene.drawBackground(&painter, exposedScene(QRectF(QPointF(0.0, 0.0), view)));
    }

    // The pixmap the scene should have drawn, asked for exactly as the scene
    // asks for it. Painted at 640x400, drawn over 320x200 logical pixels: one
    // pixmap pixel per canvas pixel, no scaling anywhere - and no trace of the
    // world transform, because the room belongs to the viewport, not the table.
    const QImage expected = expectedRoom();

    REQUIRE(rendered.size() == expected.size());

    // Before the dpr fix this is the pixmap's top-left quarter blown up 2x: the
    // gradient runs at half speed and its centre sits toward the bottom-right
    // corner. Drawn through the view's transform instead, it is the room shrunk
    // into the fit box.
    REQUIRE(rendered == expected);
}

// The size is only half of it. The source rect is also OFFSET by the exposed
// rect, so a partial repaint - which is what every repaint is once a card is
// moving over the table - reads from the wrong place besides being the wrong
// size. A fix that scaled the size and left the origin alone would pass the case
// above and fail every frame of a real game.
TEST_CASE("A partial repaint samples its own region of the room", "[table]")
{
    ProbeScene scene;

    scene.setTheme(Theme::light());

    const QRectF exposed(160.0, 0.0, 160.0, 200.0);

    QImage rendered = canvas();

    {
        QPainter painter(&rendered);
        painter.setWorldTransform(viewTransform);
        scene.drawBackground(&painter, exposedScene(exposed));
    }

    const QImage expected = expectedRoom();

    // The same region, in the physical pixels both images are addressed in.
    const QRect region(static_cast<int>(exposed.x() * ratio),
                       static_cast<int>(exposed.y() * ratio),
                       static_cast<int>(exposed.width() * ratio),
                       static_cast<int>(exposed.height() * ratio));

    REQUIRE(rendered.copy(region) == expected.copy(region));

    // And nothing outside it was touched, which is what the exposed rect is for.
    REQUIRE(rendered.pixelColor(0, 0).alpha() == 0);
}

// THE LETTERBOX. The fit box keeps the table's shape, so a window of any other
// shape shows bands of room beside or above it - and those bands are outside the
// scene rect. A room drawn into the scene rect leaves them unpainted, which on a
// wide screen is two black strips down the sides of the table.
TEST_CASE("The room covers the viewport, not just the table", "[table]")
{
    ProbeScene scene;

    scene.setTheme(Theme::light());

    QImage rendered = canvas();

    {
        QPainter painter(&rendered);
        painter.setWorldTransform(viewTransform);
        scene.drawBackground(&painter, exposedScene(QRectF(QPointF(0.0, 0.0), view)));
    }

    // The premise: the canvas's corner is outside the table as it was drawn.
    const QRectF table = viewTransform.mapRect(scene.sceneRect());
    REQUIRE_FALSE(table.contains(QPointF(0.0, 0.0)));

    REQUIRE(rendered.pixelColor(0, 0).alpha() == 255);
    REQUIRE(rendered.pixelColor(0, 0) == expectedRoom().pixelColor(0, 0));
}

// ---- table space, added in the table redesign's phase 2 ----
//
// The scene is a fixed set of coordinates the view scales, so where everything
// goes is a fact about the scene alone - asserted here in table units, against
// section 2's numbers rather than against the scene's own arithmetic.

namespace
{
GameSnapshot seatedTable(unsigned int seats, int humanSeat)
{
    GameSnapshot snapshot;

    snapshot.humanSeat = humanSeat;
    snapshot.roundNumber = 1;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.seats.resize(seats);

    for(unsigned int i = 0 ; i < seats ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].isHuman = static_cast<int>(i) == humanSeat;
    }

    return snapshot;
}

// A seated table with three cards in the human's hand, so the fan has something
// to measure.
GameSnapshot holdingThree(unsigned int seats, int humanSeat)
{
    GameSnapshot snapshot = seatedTable(seats, humanSeat);

    snapshot.hand = { HandEntry{ Card(Rank::Two, Suit::Clubs) },
                      HandEntry{ Card(Rank::Ten, Suit::Hearts) },
                      HandEntry{ Card(Rank::Ace, Suit::Spades) } };

    return snapshot;
}

template<typename Item>
std::vector<Item*> itemsOf(const QGraphicsScene& scene)
{
    std::vector<Item*> found;

    for(QGraphicsItem* item : scene.items())
    {
        if(Item* match = dynamic_cast<Item*>(item))
            found.push_back(match);
    }

    return found;
}

// The plaque whose centre is the human's spot, section 2.5's (768, 1105). Found by
// where it is rather than by seat index, because that is the claim: the seated
// player's plaque is at the bottom.
SeatItem* plaqueAtBottom(const QGraphicsScene& scene)
{
    for(SeatItem* seat : itemsOf<SeatItem>(scene))
    {
        if(seat->isVisible() && seat->sceneBoundingRect().contains(QPointF(768.0, 1105.0)))
            return seat;
    }

    return nullptr;
}

// The played cards, which are TrickItem's children.
std::vector<CardItem*> playedCards(const QGraphicsScene& scene)
{
    std::vector<CardItem*> played;

    for(CardItem* card : itemsOf<CardItem>(scene))
    {
        if(card->isVisible() && dynamic_cast<TrickItem*>(card->parentItem()))
            played.push_back(card);
    }

    return played;
}
}

TEST_CASE("The scene is the fit box, whatever the window", "[table]")
{
    // A resize changes the view's transform and nothing here, so there is no
    // size for the scene to be told - it is born the size it will always be.
    TableScene scene;

    REQUIRE(scene.sceneRect() == tablespace::fitBox);

    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    REQUIRE(scene.sceneRect() == tablespace::fitBox);
}

TEST_CASE("A seated table holds the hand where section 2.8 puts it", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    REQUIRE(hand->isVisible());
    REQUIRE(hand->slotCount() == 3);

    // Centred on the table, its cards' top edge at y = 850 and their height the
    // mock-up's 225. The fan's bounding rect reaches above the cards by the hover
    // lift, so it is the BOTTOM edge that says where the cards are.
    REQUIRE(hand->pos().x() == Catch::Approx(tablespace::centre.x()));

    const QRectF fan = hand->sceneBoundingRect();

    REQUIRE(fan.center().x() == Catch::Approx(tablespace::centre.x()));
    REQUIRE(fan.bottom() == Catch::Approx(tablespace::handTop + tablespace::handCardHeight));

    for(CardItem* card : itemsOf<CardItem>(scene))
    {
        if(card->parentItem() == hand)
            REQUIRE(card->boundingRect().height() == Catch::Approx(tablespace::handCardHeight));
    }
}

TEST_CASE("A spectated table draws no hand", "[table]")
{
    // SECTION 14'S RULE: a spectated game "has no human... and the bottom hand
    // area is not drawn at all". Seat 0 takes the human's spot instead.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, -1)));

    REQUIRE_FALSE(scene.handItem()->isVisible());
    REQUIRE(plaqueAtBottom(scene) != nullptr);
}

TEST_CASE("Your own plaque is drawn over your hand", "[table]")
{
    // SECTION 2.8: "the human's panel covers the bottom of the hand; that is
    // intended". The two overlap in table space, so which is on top is a real
    // question - and the plaque carries your name and your pill, so it has to be
    // the answer.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);

    // The premise, asserted rather than assumed: they do overlap.
    REQUIRE(mine->sceneBoundingRect().intersects(scene.handItem()->sceneBoundingRect()));

    REQUIRE(mine->zValue() > scene.handItem()->zValue());
}

// ---- your plaque over your hand, and the mouse ----
//
// DRAWN ON TOP IS NOT THE SAME AS ON TOP FOR THE MOUSE. Qt hands a press to the
// items under it top first, but an item that does not take the press passes it
// down - and a plain QGraphicsItem does not take one. So the plaque covering the
// bottom of your fan let a click on your own name through to the card under it,
// and played that card. Hover went the same way: the hand is the only item that
// asks for hover, so it lifted a card the plaque was hiding.

namespace
{

// Your turn, with the three cards holdingThree() deals, all of them legal.
PlayPrompt playingThree()
{
    PlayPrompt prompt;

    prompt.promptId = 9;
    prompt.entries = holdingThree(4, 1).hand;
    prompt.handIndex = { 0, 1, 2 };

    return prompt;
}

// WHERE YOUR PLAQUE AND YOUR FAN OVERLAP. The plaque is drawn from y = 1063 (its
// centre at 1105, 84 tall) and the cards reach down to 1075 (section 2.8: 850 plus
// 225).
//
// AND ON TWO CARDS AT ONCE, x = 808 being where the third card overlaps the
// second. A press reaches the scene with a hover in front of it, and a hover
// lifts the card under the point - which moves it out from under the point. On
// one card alone the press then finds nothing and the case passes whether the
// plaque blocks it or not; here the second card is still underneath, and a press
// that gets through plays it.
const QPointF underYourPlaque(808.0, 1070.0);

// The middle card, well clear of the plaque.
const QPointF onTheCardAlone(768.0, 950.0);

// THROUGH THE SCENE, not to an item, because which item gets the event is the
// question. No view, so the scene finds the items under the point itself.
void pressAt(QGraphicsScene& scene, QPointF where)
{
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);

    press.setScenePos(where);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &press);

    // And let go, so whatever took the press is not still holding the mouse when
    // the next one arrives.
    QGraphicsSceneMouseEvent release(QEvent::GraphicsSceneMouseRelease);

    release.setScenePos(where);
    release.setButton(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &release);
}

void hoverAt(QGraphicsScene& scene, QPointF where)
{
    QGraphicsSceneHoverEvent hover(QEvent::GraphicsSceneHoverMove);

    hover.setScenePos(where);

    QCoreApplication::sendEvent(&scene, &hover);
}

// A lifted card is one moved up off the fan's line, which is y = 0.
bool aCardIsLifted(const HandItem& hand)
{
    for(QGraphicsItem* child : hand.childItems())
    {
        if(child->pos().y() < 0.0)
            return true;
    }

    return false;
}

// The premise both cases below rest on: the point really is on your plaque as
// drawn, and on one of your cards.
void requireOverlapAt(const TableScene& scene, QPointF where)
{
    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);
    REQUIRE(mine->contains(mine->mapFromScene(where)));

    bool onACard = false;

    for(QGraphicsItem* child : scene.handItem()->childItems())
        onACard = onACard || child->sceneBoundingRect().contains(where);

    REQUIRE(onACard);
}

}

TEST_CASE("A click on your plaque does not play the card beneath it", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    hand->setPrompt(playingThree());
    hand->setPlayable(true);

    int clicks = 0;
    QObject::connect(hand, &HandItem::cardClicked, [&clicks](std::size_t) { clicks++; });

    requireOverlapAt(scene, underYourPlaque);

    pressAt(scene, underYourPlaque);

    REQUIRE(clicks == 0);

    // The control: the same card, clicked where nothing covers it, is played -
    // so the zero above is the plaque's doing and not a click that went nowhere.
    pressAt(scene, onTheCardAlone);

    REQUIRE(clicks == 1);
}

TEST_CASE("Hovering your plaque lifts no card", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    HandItem* hand = scene.handItem();

    hand->setPrompt(playingThree());
    hand->setPlayable(true);

    requireOverlapAt(scene, underYourPlaque);

    // From nowhere straight onto the plaque. Not from the card first: a lifted
    // card rises out from under this point, which would clear the lift on its
    // own and hide the defect.
    hoverAt(scene, underYourPlaque);

    REQUIRE_FALSE(aCardIsLifted(*hand));

    // The control: the card lifts where nothing covers it.
    hoverAt(scene, onTheCardAlone);

    REQUIRE(aCardIsLifted(*hand));
}

TEST_CASE("Played cards are section 2.7's size, yours above your hand", "[table]")
{
    // Four seats with you in seat 1, and two cards down: yours and the next
    // seat's. Yours sits 60 above the top of your hand; the other on its owner's
    // side, its outer end 60 inside the felt's edge.
    GameSnapshot snapshot = seatedTable(4, 1);

    snapshot.table = { PlayedCardView{ 1, Card(Rank::Ten, Suit::Hearts) },
                       PlayedCardView{ 2, Card(Rank::King, Suit::Hearts) } };

    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    const std::vector<CardItem*> played = playedCards(scene);
    REQUIRE(played.size() == 2);

    const qreal height = tablespace::playedCardHeight(4);
    const QPointF yours(tablespace::centre.x(), tablespace::handTop - 60.0 - height / 2.0);
    const qreal radius = tablespace::feltRadius - 60.0 - height / 2.0;

    bool sawYours = false;
    bool sawTheirs = false;

    for(CardItem* card : played)
    {
        // The art's own rounding of 205.2: sizeForHeight() hands back whole pixels.
        REQUIRE(card->boundingRect().height() == Catch::Approx(std::round(height)));

        const QPointF at = card->scenePos();
        const QPointF offset = at - tablespace::centre;

        if(std::abs(at.x() - yours.x()) < 0.5 && std::abs(at.y() - yours.y()) < 0.5)
            sawYours = true;
        else if(std::abs(std::hypot(offset.x(), offset.y()) - radius) < 0.5)
            sawTheirs = true;
    }

    REQUIRE(sawYours);
    REQUIRE(sawTheirs);

    // And the smaller card from five seats on, the same scene redrawn.
    snapshot.seats.resize(5);
    snapshot.seats[4].seatIndex = 4;
    scene.setSnapshot(std::make_shared<const GameSnapshot>(snapshot));

    for(CardItem* card : playedCards(scene))
    {
        REQUIRE(card->boundingRect().height()
                == Catch::Approx(std::round(tablespace::playedCardHeight(5))));
    }
}

// ---- the band the bid strip sits in, added in C2 ----
//
// SECTION 15.1's strip "rises over the bottom of the table", and the bottom of
// the table is where your hand and your own plaque are. So "the bottom" and
// "where the strip goes" are different places, and getting that wrong is not
// subtle: C2's first run drew the strip straight through the player's own name.
//
// Geometry with a right answer, which is what this file is for.

TEST_CASE("The bottom band clears your hand and your own plaque", "[table]")
{
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(holdingThree(4, 1)));

    const QRectF band = scene.overlayBand();

    // NOT EMPTY, which is the assertion that actually failed first. An empty
    // band sends TableView to its fallback - the viewport's own bottom edge -
    // and the strip lands under the plaque instead of above it, which looks
    // like a placement bug rather than a band that was never computed.
    REQUIRE_FALSE(band.isEmpty());
    REQUIRE(band.top() == Catch::Approx(scene.sceneRect().top()));

    // ABOVE THE CARDS YOU ARE BIDDING ON, which in table space sit higher than
    // your plaque: C3's half of this, and the one the new layout turned round.
    REQUIRE(band.bottom() <= tablespace::handTop);

    // And above your plaque, C2's half, bounded by the item itself rather than
    // by the scene's arithmetic.
    SeatItem* mine = plaqueAtBottom(scene);
    REQUIRE(mine != nullptr);
    REQUIRE(band.bottom() <= mine->sceneBoundingRect().top());
}

TEST_CASE("The bottom band is empty before there is anything to place it against", "[table]")
{
    // No snapshot means no seats, so there is no plaque to clear and no band -
    // and the caller is handed an empty rect to fall back on rather than a
    // negative one to lay a widget out in.
    TableScene scene;

    REQUIRE(scene.overlayBand().isEmpty());
}

TEST_CASE("A theme change during a play prompt leaves the prompt's fan standing", "[table]")
{
    // THE TWO SOURCES MEETING AT THE WORST MOMENT. relayout() re-sources the fan
    // from the snapshot, and a theme change runs it - the system's colour scheme
    // can flip at any moment the user likes, while the engine is parked in
    // playCard() waiting to be told which card. Until the table redesign's phase
    // 2 every resize did the same, the View > Log toggle's included, which is the
    // case this was written for.
    //
    // seatedTable()'s snapshot carries no hand, which is not a gap in the fixture:
    // that is exactly the shape a Forehead or Hidden round publishes (section 17),
    // and a fan re-sourced from it here empties out. Nothing left to click, no
    // digit key in range, and an answer the game can never be given.
    TableScene scene;

    scene.setTheme(Theme::light());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(seatedTable(4, 1)));

    PlayPrompt prompt;

    prompt.promptId = 5;
    prompt.blind = true;
    prompt.handIndex.push_back(0);

    scene.handItem()->setPrompt(prompt);
    scene.handItem()->setPlayable(true);

    scene.setTheme(Theme::dark());

    REQUIRE(scene.handItem()->slotCount() == 1);
    REQUIRE(scene.handItem()->isPlayable(0));
}

// ---- a card under a scaled view, added in the table redesign's phase 2 ----

// B1'S CONTRACT, WHICH THE SCALED VIEW WOULD HAVE BROKEN. CardRenderer rasterizes
// a card at exactly the size it is drawn, so the blit is 1:1 and the card is
// crisp. Under a view scaled to half, a pixmap rasterized at the card's TABLE size
// is shrunk by half on its way to the screen - resampled, which is the softness
// the renderer exists to avoid. So CardItem asks for the size the card really
// lands at, and what reaches the canvas is the renderer's own pixmap at that size,
// pixel for pixel.
TEST_CASE("A card under a scaled view is rasterized at the size it is drawn", "[table]")
{
    // Declared before the scene, which deletes its items: the renderer has to
    // outlive the card, as it does in TableScene.
    CardRenderer renderer;
    QGraphicsScene scene;

    const Card card(Rank::Queen, Suit::Hearts);

    auto* item = new CardItem(&renderer);
    item->setFace(card);
    item->setCardHeight(200.0);
    scene.addItem(item);

    const QSize full = renderer.sizeForHeight(200.0);
    const QRectF source = item->boundingRect();

    // Half size, which is the scale a 1000-pixel-tall window gives the table.
    const QSize half(full.width() / 2, full.height() / 2);

    // An even width and height, so half of it is whole pixels and the card's
    // corner lands on one: otherwise the comparison is of a half-pixel offset.
    REQUIRE(full.width() % 2 == 0);
    REQUIRE(full.height() % 2 == 0);

    QImage rendered(half, QImage::Format_ARGB32_Premultiplied);
    rendered.fill(Qt::transparent);

    {
        QPainter painter(&rendered);
        scene.render(&painter, QRectF(QPointF(0.0, 0.0), half), source);
    }

    // The renderer's pixmap copied 1:1. Its ratio is taken off first, because a
    // ratio of a half would have drawImage() lay it out at twice its pixels -
    // which is the scaling this case is checking the card did NOT need.
    QImage face = renderer.face(card, full, 0.5).toImage();
    face.setDevicePixelRatio(1.0);

    QImage expected(half, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);

    {
        QPainter painter(&expected);
        painter.drawImage(QPointF(0.0, 0.0), face);
    }

    // The premise: the pixmap the renderer hands back at a ratio of a half IS
    // half size, so matching it means the card was not resampled.
    REQUIRE(renderer.face(card, full, 0.5).size() == half);

    REQUIRE(rendered == expected);
}

// THE RATIO IS THE TARGET'S, not the widget's. A scene rendered somewhere other
// than a view - QGraphicsScene::render() into an image, as the case above does -
// paints with no widget, and CardItem used to read 1.0 there whatever the image's
// own ratio was. Into a ratio-2 image at half scale the card has to be rasterized
// at 2 x 0.5 = 1, its full size; reading the ratio as 1 gives a half-size pixmap
// stretched over twice its pixels.
TEST_CASE("A card rendered into a HiDPI image is rasterized at the image's ratio", "[table]")
{
    CardRenderer renderer;
    QGraphicsScene scene;

    const Card card(Rank::Queen, Suit::Hearts);

    auto* item = new CardItem(&renderer);
    item->setFace(card);
    item->setCardHeight(200.0);
    scene.addItem(item);

    const QSize full = renderer.sizeForHeight(200.0);
    const QRectF source = item->boundingRect();

    REQUIRE(full.width() % 2 == 0);
    REQUIRE(full.height() % 2 == 0);

    // Half the card in logical pixels, which at a ratio of 2 is all of it in
    // physical ones.
    const QSize half(full.width() / 2, full.height() / 2);

    QImage rendered(full, QImage::Format_ARGB32_Premultiplied);
    rendered.setDevicePixelRatio(2.0);
    rendered.fill(Qt::transparent);

    {
        QPainter painter(&rendered);
        scene.render(&painter, QRectF(QPointF(0.0, 0.0), half), source);
    }

    QImage expected(full, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);

    {
        QPainter painter(&expected);
        painter.drawImage(QPointF(0.0, 0.0), renderer.face(card, full, 1.0).toImage());
    }

    // Compared as pixels: the ratio was the input, not part of the answer.
    rendered.setDevicePixelRatio(1.0);

    REQUIRE(rendered == expected);
}
