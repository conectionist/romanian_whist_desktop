#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "table/TrickItem.h"
#include "table/CardItem.h"
#include "table/LeadMarkItem.h"
#include "table/RackItem.h"
#include "table/SeatLayout.h"
#include "table/TableSpace.h"
#include "core/GameSnapshot.h"
#include "paint/CardShadow.h"
#include "paint/Theme.h"

#include <QGraphicsScene>
#include <QImage>
#include <QPainter>
#include <QList>
#include <QMarginsF>
#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QtMath>

#include <cmath>
#include <optional>
#include <vector>

// The trick and the lead rhombus: the table redesign's section 2.7, from table.js
// trickPos() and renderTrick().
namespace
{

// A table of `seatCount` with every seat's bid in, so play has begun.
GameSnapshot tableOf(unsigned int seatCount, int humanSeat, std::vector<PlayedCardView> table = {})
{
    GameSnapshot snapshot;
    snapshot.seats.resize(seatCount);

    for(unsigned int i = 0 ; i < seatCount ; i++)
        snapshot.seats[i].seatIndex = i;

    snapshot.humanSeat = humanSeat;
    snapshot.betsPlaced = seatCount;
    snapshot.phase = romanian_whist::GamePhase::Playing;
    snapshot.table = std::move(table);
    return snapshot;
}

// TrickItem::setTrick() with every seat's place worked out for the snapshot's
// table, as TableScene works them out.
void setTrickOf(TrickItem& trick, const GameSnapshot& snapshot, const Theme& theme)
{
    trick.setTrick(snapshot,
                   seatlayout::places(static_cast<unsigned int>(snapshot.seats.size()), snapshot.humanSeat),
                   theme);
}

PlayedCardView played(unsigned int seat, romanian_whist::Rank rank, bool winning = false)
{
    PlayedCardView entry;
    entry.seatIndex = seat;
    entry.card = romanian_whist::Card(rank, romanian_whist::Suit::Hearts);
    entry.isWinning = winning;
    return entry;
}

// The cards in creation order, which is the order cardAt() grows them and so is
// the trick-entry order the assertions are written in.
std::vector<CardItem*> cardsOf(const TrickItem& trick)
{
    std::vector<CardItem*> cards;

    for(QGraphicsItem* child : trick.childItems())
    {
        if(CardItem* card = dynamic_cast<CardItem*>(child))
            cards.push_back(card);
    }

    return cards;
}

qreal distanceFromCentre(QPointF point)
{
    const QPointF offset = point - tablespace::centre;
    return std::hypot(offset.x(), offset.y());
}

// The angle from the table's centre to `point`, radians, y down.
qreal angleOf(QPointF point)
{
    const QPointF offset = point - tablespace::centre;
    return std::atan2(offset.y(), offset.x());
}

// Two angles in degrees the same up to whole turns.
bool sameTurn(qreal a, qreal b)
{
    return std::abs(std::remainder(a - b, 360.0)) < 1e-6;
}

// The played card's outline in table space: the mock-up's size, turned and placed.
QPolygonF outlineOf(unsigned int seatCount, const tablespace::Placement& place)
{
    const qreal height = tablespace::playedCardHeight(seatCount);
    const qreal width = seatCount <= 4 ? 141.6 : 124.8;

    QTransform turn;
    turn.translate(place.centre.x(), place.centre.y());
    turn.rotate(place.rotation);

    return turn.map(QPolygonF(QRectF(-width / 2.0, -height / 2.0, width, height)));
}

}

// ---- where each card goes ----

TEST_CASE("An opponent's card lies in front of their rack, turned to it", "[trick]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        const qreal radius = tablespace::feltRadius - tablespace::playedInset
                             - tablespace::playedCardHeight(seatCount) / 2.0;

        for(int humanSeat = -1 ; humanSeat < static_cast<int>(seatCount) ; humanSeat++)
        {
            for(unsigned int seat = 0 ; seat < seatCount ; seat++)
            {
                if(static_cast<int>(seat) == humanSeat)
                    continue;

                INFO("seatCount " << seatCount << ", humanSeat " << humanSeat << ", seat " << seat);

                const std::optional<tablespace::Placement> place =
                    trickitem::cardPlacement(seatCount, humanSeat, seat);
                REQUIRE(place);

                const qreal rack = seatlayout::place(seatCount, humanSeat, seat)->angle.value();

                // On the line to the rack, its outer end 60 inside the felt's edge...
                REQUIRE(distanceFromCentre(place->centre) == Catch::Approx(radius));
                REQUIRE(std::cos(angleOf(place->centre) - rack) == Catch::Approx(1.0));

                // ...and turned as the rack is: table.js's a - 90 degrees.
                REQUIRE(sameTurn(place->rotation, qRadiansToDegrees(rack) - 90.0));
                REQUIRE(place->rotation == rackitem::frameRotation(rack));
            }
        }
    }
}

TEST_CASE("Four players' cards are turned by exact quarter turns", "[trick]")
{
    // Left, top and right: lying sideways, upside down, and sideways the other way,
    // each reading the right way up to its owner. EXACT, so QTransform takes its
    // quarter-turn path and the art lies square on the pixel grid.
    REQUIRE(trickitem::cardPlacement(4, 0, 1)->rotation == 90.0);
    REQUIRE(trickitem::cardPlacement(4, 0, 2)->rotation == 180.0);
    REQUIRE(trickitem::cardPlacement(4, 0, 3)->rotation == -90.0);

    // And the card lies on the axis, not a hair off it.
    REQUIRE(trickitem::cardPlacement(4, 0, 2)->centre.x() == Catch::Approx(tablespace::centre.x()));
    REQUIRE(trickitem::cardPlacement(4, 0, 1)->centre.y() == Catch::Approx(tablespace::centre.y()));
}

TEST_CASE("Your card lies above your hand, unturned", "[trick]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        const qreal height = tablespace::playedCardHeight(seatCount);

        for(unsigned int human = 0 ; human < seatCount ; human++)
        {
            INFO("seatCount " << seatCount << ", human " << human);

            const std::optional<tablespace::Placement> place =
                trickitem::cardPlacement(seatCount, static_cast<int>(human), human);
            REQUIRE(place);

            // Its bottom edge 60 above the top of your hand.
            REQUIRE(place->centre.x() == tablespace::centre.x());
            REQUIRE(place->centre.y() + height / 2.0
                    == Catch::Approx(tablespace::handTop - tablespace::playedInset));
            REQUIRE(place->rotation == 0.0);
        }
    }
}

TEST_CASE("A spectated seat 0's card lies in front of its rack at the bottom", "[trick]")
{
    // Nobody's hand is at the bottom, so seat 0 is drawn as an opponent: its rack
    // stands on the rim straight below the centre, and its card in front of it.
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        INFO("seatCount " << seatCount);

        const std::optional<tablespace::Placement> place = trickitem::cardPlacement(seatCount, -1, 0);
        REQUIRE(place);

        const qreal radius = tablespace::feltRadius - tablespace::playedInset
                             - tablespace::playedCardHeight(seatCount) / 2.0;

        REQUIRE(place->centre.x() == Catch::Approx(tablespace::centre.x()));
        REQUIRE(place->centre.y() == Catch::Approx(tablespace::centre.y() + radius));
        REQUIRE(place->rotation == 0.0);
    }
}

TEST_CASE("No two played cards touch, and every one stays on the felt", "[trick]")
{
    // A FULL TRICK AT EVERY TABLE: the felt is shared by up to six cards, and five
    // and six take the smaller size for exactly this.
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(int humanSeat = -1 ; humanSeat < static_cast<int>(seatCount) ; humanSeat++)
        {
            std::vector<QPolygonF> cards;

            for(unsigned int seat = 0 ; seat < seatCount ; seat++)
                cards.push_back(outlineOf(seatCount, *trickitem::cardPlacement(seatCount, humanSeat, seat)));

            for(std::size_t left = 0 ; left < cards.size() ; left++)
            {
                INFO("seatCount " << seatCount << ", humanSeat " << humanSeat << ", seat " << left);

                for(const QPointF& corner : cards[left])
                    REQUIRE(distanceFromCentre(corner) < tablespace::feltRadius);

                for(std::size_t right = left + 1 ; right < cards.size() ; right++)
                {
                    INFO("and seat " << right);
                    REQUIRE_FALSE(cards[left].intersects(cards[right]));
                }
            }
        }
    }
}

TEST_CASE("A seat with no panel has no card place and no rhombus", "[trick]")
{
    REQUIRE_FALSE(trickitem::cardPlacement(4, 0, 4));
    REQUIRE_FALSE(trickitem::cardPlacement(7, 0, 1));
    REQUIRE_FALSE(trickitem::leadMarkPlacement(4, 0, 99));
}

// ---- the item ----

// The defensive branch, asserted rather than assumed.
//
// setTrick() skips a table entry whose seat this table cannot place, which is the
// right call - a seat this scene cannot place is a card it cannot draw. What the
// skip has to ALSO do is hide the item at that index, because the hide pass that
// cleans up after a shrinking trick starts past the end of the table and never
// reaches an index inside it.
//
// The engine never produces such a snapshot today, which is exactly why this case
// exists: an unreachable branch is one nothing corrects when it stops being
// unreachable, and its failure mode here is silent - last frame's card left on
// the table under a seat that no longer has one.
TEST_CASE("A trick entry the table cannot place hides its card rather than stranding it", "[trick]")
{
    // Null renderer: nothing here paints, and CardItem's paint() and
    // sizeForHeight() both guard against one. Building a real CardRenderer would
    // only add the card art to a test about visibility.
    TrickItem trick(nullptr);
    const Theme theme = Theme::dark();

    // Frame one: two cards, both placeable, both on the table.
    setTrickOf(trick, tableOf(4, 0, { played(0, romanian_whist::Rank::Ace),
                                   played(1, romanian_whist::Rank::King) }),
                   theme);

    std::vector<CardItem*> cards = cardsOf(trick);

    REQUIRE(cards.size() == 2);
    REQUIRE(cards[0]->isVisible());
    REQUIRE(cards[1]->isVisible());

    // Frame two: the same two entries, but the first names a seat this table does
    // not have. THE TABLE HAS NOT SHRUNK - it is still two entries long - so the
    // hide pass below the loop starts at index 2 and never touches index 0.
    setTrickOf(trick, tableOf(4, 0, { played(99, romanian_whist::Rank::Queen),
                                   played(1, romanian_whist::Rank::King) }),
                   theme);

    cards = cardsOf(trick);

    REQUIRE(cards.size() == 2);

    // The whole finding: without the hide, this is still showing frame one's ace
    // at seat 0, with its turn and its winning highlight.
    REQUIRE_FALSE(cards[0]->isVisible());

    // And the entry that IS placeable is untouched by its neighbour's skip.
    REQUIRE(cards[1]->isVisible());
}

TEST_CASE("Each card is placed and turned where cardPlacement() says", "[trick]")
{
    TrickItem trick(nullptr);

    setTrickOf(trick, tableOf(6, 2, { played(3, romanian_whist::Rank::Two),
                                   played(4, romanian_whist::Rank::Three),
                                   played(5, romanian_whist::Rank::Four),
                                   played(0, romanian_whist::Rank::Five),
                                   played(1, romanian_whist::Rank::Six),
                                   played(2, romanian_whist::Rank::Seven) }),
                   Theme::dark());

    const std::vector<CardItem*> cards = cardsOf(trick);
    REQUIRE(cards.size() == 6);

    const unsigned int seats[] = { 3, 4, 5, 0, 1, 2 };

    for(std::size_t i = 0 ; i < cards.size() ; i++)
    {
        INFO("entry " << i);

        const tablespace::Placement place = *trickitem::cardPlacement(6, 2, seats[i]);

        REQUIRE(cards[i]->pos() == place.centre);
        REQUIRE(cards[i]->rotation() == place.rotation);
        REQUIRE(cards[i]->isPlayed());

        // Later cards over earlier ones, as they were played.
        if(i > 0)
            REQUIRE(cards[i]->zValue() > cards[i - 1]->zValue());
    }
}

TEST_CASE("The winner is highlighted from the second card, and only as the engine says", "[trick]")
{
    TrickItem trick(nullptr);

    // ONE CARD: the engine ranks it winning, and it is; but there is nothing yet
    // for it to beat, and table.js draws no highlight.
    setTrickOf(trick, tableOf(4, 0, { played(1, romanian_whist::Rank::Ten, true) }), Theme::dark());

    std::vector<CardItem*> cards = cardsOf(trick);
    REQUIRE(cards.size() == 1);
    REQUIRE_FALSE(cards[0]->isWinning());

    // TWO, AND THE LOWER ONE WINNING - a trump, say. The highlight is isWinning and
    // is never worked out here, so it goes where the engine put it.
    setTrickOf(trick, tableOf(4, 0, { played(1, romanian_whist::Rank::Ten),
                                   played(2, romanian_whist::Rank::Two, true) }),
                   Theme::dark());

    cards = cardsOf(trick);
    REQUIRE(cards.size() == 2);
    REQUIRE_FALSE(cards[0]->isWinning());
    REQUIRE(cards[1]->isWinning());
}

TEST_CASE("The winning card rises toward the table's centre", "[trick]")
{
    // .tcard.winning's translateY(-8px), after the card's rotate: up its own frame,
    // which on every seat's card points at the table's centre - so the rise is
    // straight in from the owner, whoever that is.
    for(unsigned int seatCount = 3 ; seatCount <= 6 ; seatCount++)
    {
        for(unsigned int seat = 0 ; seat < seatCount ; seat++)
        {
            INFO("seatCount " << seatCount << ", seat " << seat);

            const tablespace::Placement place = *trickitem::cardPlacement(seatCount, 0, seat);

            QTransform turn;
            turn.translate(place.centre.x(), place.centre.y());
            turn.rotate(place.rotation);

            const QPointF risen = turn.map(QPointF(0.0, -cardshadow::winningLift));
            const QPointF toward = risen - place.centre;
            const QPointF inward = tablespace::centre - place.centre;

            REQUIRE(std::hypot(toward.x(), toward.y()) == Catch::Approx(cardshadow::winningLift));

            // In line with the centre and on its side - your card's too, which sits
            // straight below it.
            const qreal along = (toward.x() * inward.x() + toward.y() * inward.y())
                                / std::hypot(inward.x(), inward.y());
            REQUIRE(along == Catch::Approx(cardshadow::winningLift));
        }
    }
}

// ---- the lead rhombus ----

TEST_CASE("The rhombus marks nobody while the bids are coming in", "[trick]")
{
    SECTION("with a bid still to come")
    {
        GameSnapshot snapshot = tableOf(4, 0);
        snapshot.phase = romanian_whist::GamePhase::Betting;
        snapshot.betsPlaced = 2;
        snapshot.activeSeat = 2;

        // The seat being asked is the bidder, not a leader.
        REQUIRE_FALSE(trickitem::leadSeat(snapshot));
    }

    SECTION("on the last bid's own frame, with its bidder still on turn")
    {
        // Every bet is in, the table is empty and the last bidder is still the
        // active seat - onBetPlaced, before the engine moves to Playing. Seat 0 bid
        // last; seat 1 leads.
        GameSnapshot snapshot = tableOf(4, 0);
        snapshot.phase = romanian_whist::GamePhase::Betting;
        snapshot.activeSeat = 0;
        snapshot.leadSeat = 1;

        REQUIRE_FALSE(trickitem::leadSeat(snapshot));
    }

    SECTION("with no table at all")
    {
        REQUIRE_FALSE(trickitem::leadSeat(GameSnapshot()));
    }
}

TEST_CASE("The rhombus marks whoever leads the trick", "[trick]")
{
    SECTION("once the bids are complete, before anyone is asked")
    {
        // onBettingComplete and onTrickStarted: nobody on turn, nothing down.
        GameSnapshot snapshot = tableOf(4, 0);
        snapshot.leadSeat = 3;

        REQUIRE(trickitem::leadSeat(snapshot) == 3u);
    }

    SECTION("before the first card, while the leader is asked to play")
    {
        GameSnapshot snapshot = tableOf(4, 0);
        snapshot.activeSeat = 3;
        snapshot.leadSeat = 3;

        REQUIRE(trickitem::leadSeat(snapshot) == 3u);
    }

    SECTION("once a card is down, its owner - whoever is asked next")
    {
        GameSnapshot snapshot = tableOf(4, 0, { played(2, romanian_whist::Rank::Ace) });
        snapshot.activeSeat = 3;

        REQUIRE(trickitem::leadSeat(snapshot) == 2u);
    }

    SECTION("while a full trick waits to be cleared, still the first card's owner")
    {
        // By now the engine's own trick leader has moved on to the winner, which is
        // why the trick's first entry is what is read.
        GameSnapshot snapshot = tableOf(4, 0, { played(2, romanian_whist::Rank::Two),
                                                played(3, romanian_whist::Rank::Ace, true),
                                                played(0, romanian_whist::Rank::Three),
                                                played(1, romanian_whist::Rank::Four) });
        snapshot.leadSeat = 3;

        REQUIRE(trickitem::leadSeat(snapshot) == 2u);
    }
}

TEST_CASE("The rhombus marks nobody once the round's play is over", "[trick]")
{
    // The round's last trick stays on the table under the round sheet, and the
    // engine's leader still names somebody - neither is a trick being led.
    const std::vector<PlayedCardView> last = { played(2, romanian_whist::Rank::Two),
                                               played(3, romanian_whist::Rank::Ace, true),
                                               played(0, romanian_whist::Rank::Three),
                                               played(1, romanian_whist::Rank::Four) };

    SECTION("while the round is scored")
    {
        GameSnapshot snapshot = tableOf(4, 0, last);
        snapshot.phase = romanian_whist::GamePhase::RoundScored;
        snapshot.leadSeat = 3;

        REQUIRE_FALSE(trickitem::leadSeat(snapshot));

        snapshot.table.clear();
        REQUIRE_FALSE(trickitem::leadSeat(snapshot));
    }

    SECTION("once the game is over")
    {
        GameSnapshot snapshot = tableOf(4, 0, last);
        snapshot.phase = romanian_whist::GamePhase::GameOver;
        snapshot.leadSeat = 3;

        REQUIRE_FALSE(trickitem::leadSeat(snapshot));
    }
}

TEST_CASE("The rhombus sits on the gold circle, or between your card and your hand", "[trick]")
{
    for(unsigned int seatCount = 2 ; seatCount <= 6 ; seatCount++)
    {
        for(unsigned int seat = 1 ; seat < seatCount ; seat++)
        {
            INFO("seatCount " << seatCount << ", seat " << seat);

            const std::optional<tablespace::Placement> mark = trickitem::leadMarkPlacement(seatCount, 0, seat);
            REQUIRE(mark);

            const qreal rack = seatlayout::place(seatCount, 0, seat)->angle.value();

            REQUIRE(distanceFromCentre(mark->centre) == Catch::Approx(tablespace::goldLineRadius));
            REQUIRE(std::cos(angleOf(mark->centre) - rack) == Catch::Approx(1.0));
            REQUIRE(mark->rotation == rackitem::frameRotation(rack));

            // Between the rack and the card, clear of both.
            const qreal card = distanceFromCentre(trickitem::cardPlacement(seatCount, 0, seat)->centre)
                               + tablespace::playedCardHeight(seatCount) / 2.0;
            REQUIRE(card < tablespace::goldLineRadius);
            REQUIRE(tablespace::goldLineRadius < tablespace::rackRadius);
        }

        // Yours, half way between your card and your hand.
        const std::optional<tablespace::Placement> yours = trickitem::leadMarkPlacement(seatCount, 0, 0);
        REQUIRE(yours);
        REQUIRE(yours->centre == QPointF(768.0, 820.0));
        REQUIRE(yours->rotation == 0.0);
    }
}

TEST_CASE("The rhombus is drawn long along the radius", "[trick]")
{
    // An 11 x 11 square turned 45 degrees and stretched 1.45 along the item's y,
    // which its rotation turns out along the radius toward the leader. Measured on
    // what paint() draws - the opaque gold of the square, inside its glow and
    // outline - so a stretch applied along the wrong axis fails here.
    const Theme theme = Theme::dark();
    const qreal scale = 4.0;

    for(const qreal rotation : { 0.0, 90.0 })
    {
        INFO("rotation " << rotation);

        QGraphicsScene scene;
        auto* mark = new LeadMarkItem;
        scene.addItem(mark);
        mark->setMark(tablespace::Placement{ QPointF(0.0, 0.0), rotation }, theme.table);

        const QRectF source(-40.0, -40.0, 80.0, 80.0);
        QImage image((source.size() * scale).toSize(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        scene.render(&painter, QRectF(QPointF(), image.size()), source);
        painter.end();

        REQUIRE(mark->renders() == 1);

        // The square is the only thing drawn opaque, and it is gold. Its spread, not
        // its bounding box: anti-aliasing thins the sharp tips more along the long
        // axis than the short, but a filled rhombus's spread along each axis is in
        // the ratio of its diagonals exactly, whatever that does to the tips.
        qreal count = 0.0;
        QPointF sum;

        std::vector<QPointF> lit;

        for(int y = 0 ; y < image.height() ; y++)
        {
            for(int x = 0 ; x < image.width() ; x++)
            {
                const QColor colour = image.pixelColor(x, y);

                if(colour.alpha() == 255 && std::abs(colour.red() - theme.table.goldBright.red()) < 8
                   && std::abs(colour.blue() - theme.table.goldBright.blue()) < 8)
                {
                    lit.emplace_back(x + 0.5, y + 0.5);
                    sum += lit.back();
                    count += 1.0;
                }
            }
        }

        REQUIRE(count > 0.0);

        const QPointF centre = sum / count;
        qreal spreadX = 0.0;
        qreal spreadY = 0.0;

        for(const QPointF& point : lit)
        {
            spreadX += (point.x() - centre.x()) * (point.x() - centre.x());
            spreadY += (point.y() - centre.y()) * (point.y() - centre.y());
        }

        // Long along the item's y: the scene's y unturned, the scene's x a quarter
        // turn on.
        const qreal ratio = rotation == 0.0 ? std::sqrt(spreadY / spreadX) : std::sqrt(spreadX / spreadY);

        REQUIRE(ratio == Catch::Approx(1.45).margin(0.08));

        // About the size of an 11 x 11 square: its area, at this scale.
        REQUIRE(count == Catch::Approx(11.0 * 11.0 * 1.45 * scale * scale).epsilon(0.15));

        // And centred where it was placed.
        REQUIRE(centre.x() == Catch::Approx(image.width() / 2.0).margin(1.0));
        REQUIRE(centre.y() == Catch::Approx(image.height() / 2.0).margin(1.0));
    }
}

TEST_CASE("A rhombus with nowhere to go is hidden", "[trick]")
{
    LeadMarkItem mark;
    const Theme theme = Theme::dark();

    mark.setMark(trickitem::leadMarkPlacement(4, 0, 2), theme.table);

    REQUIRE(mark.isVisible());
    REQUIRE(mark.pos() == trickitem::leadMarkPlacement(4, 0, 2)->centre);
    REQUIRE(mark.rotation() == 180.0);

    // The lead is a seat this table cannot place: nothing is marked rather than a
    // rhombus left over the last seat it was on.
    mark.setMark(trickitem::leadMarkPlacement(4, 0, 99), theme.table);

    REQUIRE_FALSE(mark.isVisible());
}

// ---- the shadows ----

TEST_CASE("A played card's shadow reaches further when it wins", "[trick]")
{
    const QMarginsF plain = cardshadow::reach(cardshadow::Look::Played);
    const QMarginsF winning = cardshadow::reach(cardshadow::Look::Winning);

    // Both fall toward the card's own bottom, so they reach furthest there.
    REQUIRE(plain.bottom() > plain.top());
    REQUIRE(winning.bottom() > winning.top());

    // The glow and the deeper shadow reach past the plain one on every side.
    REQUIRE(winning.left() > plain.left());
    REQUIRE(winning.top() > plain.top());
    REQUIRE(winning.bottom() > plain.bottom());
}

TEST_CASE("Played cards share their shadow pictures", "[trick]")
{
    const Theme theme = Theme::dark();
    cardshadow::Cache shadows;

    const QSizeF size(141.0, 205.0);

    const cardshadow::Picture first =
        shadows.picture(size, cardshadow::Look::Played, cardshadow::colours(theme.table), 1.0);

    REQUIRE_FALSE(first.pixmap.isNull());
    REQUIRE(shadows.renders() == 1);

    // Asked again, by another card: the same picture.
    shadows.picture(size, cardshadow::Look::Played, cardshadow::colours(theme.table), 1.0);
    REQUIRE(shadows.renders() == 1);

    // The winner's is its own, and a second size is too.
    shadows.picture(size, cardshadow::Look::Winning, cardshadow::colours(theme.table), 1.0);
    shadows.picture(QSizeF(125.0, 181.0), cardshadow::Look::Played, cardshadow::colours(theme.table), 1.0);
    REQUIRE(shadows.renders() == 3);

    // A new ratio starts again.
    shadows.picture(size, cardshadow::Look::Played, cardshadow::colours(theme.table), 2.0);
    REQUIRE(shadows.renders() == 4);
}

TEST_CASE("The shadow picture reaches past the card, furthest below it", "[trick]")
{
    const Theme theme = Theme::dark();
    const QSizeF size(141.0, 205.0);

    for(const cardshadow::Look look : { cardshadow::Look::Played, cardshadow::Look::Winning })
    {
        const bool winning = look == cardshadow::Look::Winning;
        INFO("winning " << winning);

        const cardshadow::Picture picture = cardshadow::render(size, look, cardshadow::colours(theme.table), 1.0);
        REQUIRE_FALSE(picture.pixmap.isNull());

        const QRectF card = cardshadow::cardRect(size);
        const QRectF covered(picture.offset, QSizeF(picture.pixmap.size()));

        REQUIRE(covered.contains(card));
        REQUIRE(covered.bottom() - card.bottom() > card.top() - covered.top());

        // NOTHING UNDER A PLAIN CARD: CSS clips an outer shadow to outside the
        // element, so the middle of the picture is clear. UNDER THE WINNER, THE RING
        // FILLED THROUGH, so its edge against the art is the art's alone.
        const QImage image = picture.pixmap.toImage();
        const QPoint middle = (-picture.offset).toPoint();

        if(!winning)
            REQUIRE(qAlpha(image.pixel(middle)) == 0);
        else
            REQUIRE(image.pixelColor(middle) == theme.table.goldBright);
    }
}

TEST_CASE("The winner's ring has no inner edge for the felt to show through", "[trick]")
{
    // Across the card's edge, from the ring into the card: opaque gold all the way,
    // and on a cut-out band the pixels the art's edge shares would be part clear.
    const Theme theme = Theme::dark();
    const QSizeF size(141.0, 205.0);
    const qreal ratio = 0.78;

    const cardshadow::Picture picture =
        cardshadow::render(size, cardshadow::Look::Winning, cardshadow::colours(theme.table), ratio);
    const QImage image = picture.pixmap.toImage();

    const QRectF card = cardshadow::cardRect(size);

    // Each side's middle, from just outside the card - well inside the 2-unit ring,
    // whose outer edge is anti-aliased over the glow - to a few units inside it.
    const auto opaqueAcross = [&](QPointF from, QPointF to)
    {
        for(int step = 0 ; step <= 10 ; step++)
        {
            const QPointF at = from + (to - from) * (step / 10.0);
            const QPoint pixel(qFloor((at.x() - picture.offset.x()) * ratio),
                               qFloor((at.y() - picture.offset.y()) * ratio));

            if(qAlpha(image.pixel(pixel)) != 255)
                return false;
        }

        return true;
    };

    REQUIRE(opaqueAcross(QPointF(card.left() - 0.3, 0.0), QPointF(card.left() + 6.0, 0.0)));
    REQUIRE(opaqueAcross(QPointF(card.right() + 0.3, 0.0), QPointF(card.right() - 6.0, 0.0)));
    REQUIRE(opaqueAcross(QPointF(0.0, card.top() - 0.3), QPointF(0.0, card.top() + 6.0)));
    REQUIRE(opaqueAcross(QPointF(0.0, card.bottom() + 0.3), QPointF(0.0, card.bottom() - 6.0)));
}

TEST_CASE("A played card's shadow lies whole pixels from the card's corner", "[trick]")
{
    // At a view scale of 0.78 the card's corner is off the grid (-55.2 units is
    // -43.06 pixels), so a picture cornered on the origin's grid would sit a
    // fraction of a pixel from the art CardItem lays at that corner. 1.5 and 2 are
    // the HiDPI ratios (phase 12): -55.2 is -82.8 and -110.4 pixels there, off the
    // grid again.
    const Theme theme = Theme::dark();
    const QSizeF size(110.4, 160.0);
    const qreal ratio = GENERATE(0.78, 1.5, 2.0);

    INFO("ratio " << ratio);

    const QPointF cardCorner = cardshadow::cardRect(size).topLeft();

    for(const cardshadow::Look look : { cardshadow::Look::Played, cardshadow::Look::Winning })
    {
        INFO("winning " << (look == cardshadow::Look::Winning));

        const cardshadow::Picture picture =
            cardshadow::render(size, look, cardshadow::colours(theme.table), ratio);
        const QPointF steps = (picture.offset - cardCorner) * ratio;

        REQUIRE(steps.x() == Catch::Approx(std::round(steps.x())).margin(1e-9));
        REQUIRE(steps.y() == Catch::Approx(std::round(steps.y())).margin(1e-9));
    }
}
