#include <catch2/catch_test_macros.hpp>

#include "GameFixtures.h"

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"
#include "table/BidStripItem.h"
#include "table/ScoreboardItem.h"
#include "table/SeatLayout.h"
#include "table/TableButtonItem.h"
#include "table/TableScene.h"
#include "table/TableSpace.h"
#include "table/TableView.h"
#include "table/TrickPanelItem.h"

#include <QCoreApplication>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QImage>
#include <QLineF>
#include <QObject>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QString>

#include <cmath>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

// Section 15.1's strip, in two halves.
//
// FIRST, what it says and what a key means - fieldsFor() and answerKey(), the
// parts with a right answer, tested without a scene, which is the split the
// scoreboard and SnapshotLog both argue for. THEN the strip on the table: where it
// sits, and what a click and the cursor do to it.
//
// THE ENGLISH IS SAFE TO ASSERT ON for the reason ScoreboardTests states: these
// strings go through QCoreApplication::translate() and TestApp installs no
// translator, so translate() returns its source text.

namespace
{
BetPrompt promptFor(std::size_t handSize, std::optional<unsigned int> forbidden = std::nullopt)
{
    BetPrompt prompt;
    prompt.promptId = 7;
    prompt.handSize = handSize;
    prompt.forbiddenBet = forbidden;

    return prompt;
}

// A table of `seats`, you at seat 0, with `placed` bids in that add up to `sum`.
GameSnapshot biddingTable(unsigned int seats, unsigned int placed = 0, unsigned int sum = 0)
{
    GameSnapshot snapshot;

    snapshot.humanSeat = 0;
    snapshot.roundNumber = 1;
    snapshot.roundCount = 26;
    snapshot.trickCount = 8;
    snapshot.betsPlaced = placed;
    snapshot.betSum = sum;
    snapshot.seats.resize(seats);

    for(unsigned int i = 0 ; i < seats ; i++)
    {
        snapshot.seats[i].seatIndex = i;
        snapshot.seats[i].isHuman = i == 0;
    }

    return snapshot;
}

bidstrip::Fields fieldsOf(std::size_t handSize, std::optional<unsigned int> forbidden = std::nullopt)
{
    return bidstrip::fieldsFor(promptFor(handSize, forbidden), biddingTable(4));
}

// THROUGH THE SCENE, as HudTests' presses go: which item takes the event is part of
// the question.
void pressAndRelease(QGraphicsScene& scene, QPointF press, QPointF release)
{
    QGraphicsSceneMouseEvent down(QEvent::GraphicsSceneMousePress);

    down.setScenePos(press);
    down.setButton(Qt::LeftButton);
    down.setButtons(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &down);

    QGraphicsSceneMouseEvent up(QEvent::GraphicsSceneMouseRelease);

    up.setScenePos(release);
    up.setButton(Qt::LeftButton);

    QCoreApplication::sendEvent(&scene, &up);
}

void hoverAt(QGraphicsScene& scene, QPointF where)
{
    QGraphicsSceneHoverEvent hover(QEvent::GraphicsSceneHoverMove);

    hover.setScenePos(where);

    QCoreApplication::sendEvent(&scene, &hover);
}

// A strip on a scene of its own, at its place in table space, with what it bids
// recorded. A lambda rather than QSignalSpy, which lives in Qt6::Test and would be
// a whole module linked for one assertion.
struct LoneStrip
{
    QGraphicsScene scene;
    BidStripItem* strip = new BidStripItem(nullptr);
    std::vector<unsigned int> chosen;

    LoneStrip()
    {
        scene.addItem(strip);
        strip->setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(4)), Theme::dark());

        QObject::connect(strip, &BidStripItem::bidChosen,
                         [this](unsigned int bet) { chosen.push_back(bet); });
    }

    // The middle of `bet`'s button, in the scene.
    QPointF on(unsigned int bet) const
    {
        return strip->mapToScene(strip->buttonRect(bet).center());
    }
};
}

// ---- fieldsFor(), no scene ----

TEST_CASE("The strip offers every bid from zero to the hand size", "[bid-strip]")
{
    // INCLUSIVE at both ends, which is the off-by-one to get right: an 8-card
    // round has nine legal bids, because bidding zero is a real choice rather
    // than the absence of one.
    const bidstrip::Fields fields = fieldsOf(8);

    REQUIRE(fields.highest == 8);
    REQUIRE_FALSE(fields.forbidden.has_value());

    for(unsigned int bet = 0 ; bet <= 8 ; bet++)
        REQUIRE(bidstrip::offers(fields, bet));

    REQUIRE_FALSE(bidstrip::offers(fields, 9));

    REQUIRE(fields.question == QStringLiteral("How many tricks will you take?"));
    REQUIRE_FALSE(fields.note.isEmpty());
}

TEST_CASE("A forbidden bid is carried with the reason for it", "[bid-strip]")
{
    const bidstrip::Fields fields = fieldsOf(6, 3u);

    REQUIRE(fields.forbidden == 3u);
    REQUIRE_FALSE(bidstrip::offers(fields, 3));

    // BOTH NUMBERS, because either alone is unreadable: "3 is not allowed" does
    // not say why, and "the bids would add up to 6" does not say which bid.
    REQUIRE(fields.note == QStringLiteral("You bid last. 3 would make the bids add up to 6, "
                                          "and somebody has to be wrong."));
}

TEST_CASE("A forbidden bid of zero is still a forbidden bid", "[bid-strip]")
{
    // THE OFF-BY-ONE THIS EXISTS TO CATCH: zero is a legal bid, so an optional
    // holding 0 must not be read as "nothing forbidden". A strip that tested the
    // value rather than the engagement would leave the one illegal button live
    // in exactly the round where the bids are tightest.
    const bidstrip::Fields fields = fieldsOf(4, 0u);

    REQUIRE(fields.forbidden == 0u);
    REQUIRE_FALSE(bidstrip::offers(fields, 0));
    REQUIRE(bidstrip::offers(fields, 1));
    REQUIRE(fields.note.startsWith(QStringLiteral("You bid last.")));
}

TEST_CASE("Without a forbidden bid the note says how the bids stand", "[bid-strip]")
{
    // The mock-up's other sentence: how many have bid, and what their bids add up
    // to against the round's tricks - read off the frame the engine published as
    // it asked (GameBridge::onBetRequested()), not added up here.
    const bidstrip::Fields six = bidstrip::fieldsFor(promptFor(6), biddingTable(4, 2, 3));

    REQUIRE(six.note == QStringLiteral("2 bid so far, totalling 3 of 6 tricks."));

    // ONE TRICK IS ONE TRICK, as the scoreboard's tension line says it.
    const bidstrip::Fields one = bidstrip::fieldsFor(promptFor(1), biddingTable(4));

    REQUIRE(one.note == QStringLiteral("0 bid so far, totalling 0 of 1 trick."));
}

TEST_CASE("A blind round's strip is built from the count, never from a card", "[bid-strip]")
{
    // Forehead and Hidden are 1-trick rounds, so the strip is 0 and 1 - and
    // BetPrompt::hand is EMPTY there, because the bidder is not entitled to
    // their own hand. Fields carries no card of any kind, which is what makes
    // that leak impossible here rather than merely avoided.
    BetPrompt prompt = promptFor(1);
    prompt.roundType = romanian_whist::RoundType::Hidden;

    REQUIRE(prompt.hand.empty());

    const bidstrip::Fields fields = bidstrip::fieldsFor(prompt, biddingTable(4));

    REQUIRE(fields.highest == 1);
}

// ---- the keyboard half of section 15.1, answerKey(), no window ----

TEST_CASE("A digit key bids that digit", "[bid-strip]")
{
    const bidstrip::Fields fields = fieldsOf(8);

    REQUIRE(bidstrip::answerKey(Qt::Key_2, Qt::NoModifier, fields)
            == bidstrip::KeyAnswer{ bidstrip::KeyAnswer::Kind::Bid, 2 });

    // ZERO-BASED: a 0 bids nothing tricks, where the fan's 1 is its first card.
    REQUIRE(bidstrip::answerKey(Qt::Key_0, Qt::NoModifier, fields)
            == bidstrip::KeyAnswer{ bidstrip::KeyAnswer::Kind::Bid, 0 });
}

TEST_CASE("A digit held with a modifier bids nothing", "[bid-strip]")
{
    // A BID IS IRREVOCABLE, so this is not a cosmetic miss: Ctrl+2 reaching the
    // strip submits a two and the round proceeds on it. The chord belongs to
    // whatever the window binds it to, so it is passed on, not swallowed.
    const bidstrip::Fields fields = fieldsOf(8);

    REQUIRE(bidstrip::answerKey(Qt::Key_2, Qt::ControlModifier, fields).kind
            == bidstrip::KeyAnswer::Kind::Pass);
    REQUIRE(bidstrip::answerKey(Qt::Key_2, Qt::AltModifier, fields).kind
            == bidstrip::KeyAnswer::Kind::Pass);

    // THE KEYPAD'S OWN MODIFIER IS NOT ONE OF THEM. Keypad 2 is the same 2 to
    // the person pressing it, and platforms tag it - so a guard written as a
    // flat comparison against NoModifier would take the numpad out with Ctrl.
    REQUIRE(bidstrip::answerKey(Qt::Key_2, Qt::KeypadModifier, fields)
            == bidstrip::KeyAnswer{ bidstrip::KeyAnswer::Kind::Bid, 2 });
}

TEST_CASE("The forbidden digit's key does nothing", "[bid-strip]")
{
    // The keyboard asks offers(), the rule the mouse goes through, so this is the
    // case that holds the two halves of section 15.1 to the same rule.
    const bidstrip::Fields fields = fieldsOf(8, 3u);

    REQUIRE(bidstrip::answerKey(Qt::Key_3, Qt::NoModifier, fields).kind
            == bidstrip::KeyAnswer::Kind::Refuse);

    // And the digits either side of it still work, which is what says the strip
    // ignored one bid rather than the keyboard.
    REQUIRE(bidstrip::answerKey(Qt::Key_4, Qt::NoModifier, fields)
            == bidstrip::KeyAnswer{ bidstrip::KeyAnswer::Kind::Bid, 4 });
}

TEST_CASE("A digit the strip does not offer is passed on", "[bid-strip]")
{
    // Not a bid this round has, and not a key: passed on rather than swallowed, so
    // a key the window binds still reaches it.
    const bidstrip::Fields one = fieldsOf(1);

    REQUIRE(bidstrip::answerKey(Qt::Key_5, Qt::NoModifier, one).kind == bidstrip::KeyAnswer::Kind::Pass);
    REQUIRE(bidstrip::answerKey(Qt::Key_A, Qt::NoModifier, one).kind == bidstrip::KeyAnswer::Kind::Pass);
    REQUIRE(bidstrip::answerKey(Qt::Key_Space, Qt::NoModifier, one).kind == bidstrip::KeyAnswer::Kind::Pass);
}

// ---- the strip on the table ----

TEST_CASE("The strip builds one button per bid", "[bid-strip]")
{
    LoneStrip lone;
    lone.strip->setPrompt(promptFor(8));

    REQUIRE(lone.strip->isVisible());

    // NINE BIDS FIT AN 8-CARD ROUND, side by side inside the strip's padding, and
    // none overlaps the next.
    const QRectF content(23.0, 0.0, 634.0, lone.strip->size().height());

    for(unsigned int bet = 0 ; bet <= 8 ; bet++)
    {
        INFO("bid " << bet);

        const QRectF button = lone.strip->buttonRect(bet);

        REQUIRE_FALSE(button.isEmpty());
        REQUIRE(content.contains(button));

        if(bet > 0)
            REQUIRE(lone.strip->buttonRect(bet - 1).right() < button.left());
    }

    REQUIRE(lone.strip->buttonRect(9).isEmpty());

    // Centred, as .opts is.
    const qreal left = lone.strip->buttonRect(0).left();
    const qreal right = lone.strip->size().width() - lone.strip->buttonRect(8).right();

    REQUIRE(std::abs(left - right) < 1e-9);
}

TEST_CASE("Only the forbidden bid refuses a click", "[bid-strip]")
{
    // The assertion the strip exists for. QtMoveProvider::makeBet() re-prompts on
    // an illegal bet rather than passing it to the engine, so missing this is a
    // strip that reappears rather than a crash - but the player is told nothing
    // about why.
    LoneStrip lone;
    lone.strip->setPrompt(promptFor(6, 3u));

    for(unsigned int bet = 0 ; bet <= 6 ; bet++)
        pressAndRelease(lone.scene, lone.on(bet), lone.on(bet));

    REQUIRE(lone.chosen == std::vector<unsigned int>{ 0, 1, 2, 4, 5, 6 });

    // And the keyboard's road refuses it too.
    REQUIRE_FALSE(lone.strip->activate(3));
    REQUIRE(lone.strip->activate(4));
}

TEST_CASE("Clicking a button reports that button's bid", "[bid-strip]")
{
    // THE BID, NOT THE POSITION. They are equal here because the buttons run
    // 0..highest with none missing - which is exactly what makes the mistake
    // invisible - and it is the same class of error as submitting a display slot
    // where the engine wants a hand position.
    LoneStrip lone;
    lone.strip->setPrompt(promptFor(6, 3u));

    pressAndRelease(lone.scene, lone.on(5), lone.on(5));

    REQUIRE(lone.chosen == std::vector<unsigned int>{ 5 });

    // A press let go on another bid, or off the strip, is no click: the player who
    // presses and thinks better of it drags away.
    pressAndRelease(lone.scene, lone.on(5), lone.on(6));
    pressAndRelease(lone.scene, lone.on(5), QPointF(-500.0, -500.0));

    REQUIRE(lone.chosen.size() == 1);
}

TEST_CASE("A second prompt leaves no bids from the first", "[bid-strip]")
{
    // The schedule runs 1 card up to 8 and back down, so the strip is rebuilt
    // every round and most rebuilds are SMALLER than the last. A strip that kept
    // the old buttons would offer bids the round does not have.
    LoneStrip lone;
    lone.strip->setPrompt(promptFor(8));

    const QPointF eight = lone.on(8);

    lone.strip->setPrompt(promptFor(1));

    REQUIRE(lone.strip->fields().highest == 1);
    REQUIRE(lone.strip->buttonRect(2).isEmpty());

    pressAndRelease(lone.scene, eight, eight);
    REQUIRE(lone.chosen.empty());

    // Taken back, the strip goes, and nothing it showed can be chosen.
    lone.strip->clearPrompt();

    REQUIRE_FALSE(lone.strip->isVisible());
    REQUIRE_FALSE(lone.strip->activate(0));
}

TEST_CASE("A bid lights under the cursor and the struck-out one never does", "[bid-strip]")
{
    LoneStrip lone;
    lone.strip->setPrompt(promptFor(6, 3u));

    hoverAt(lone.scene, lone.on(2));
    REQUIRE(lone.strip->hoveredBid() == 2u);

    // A button that brightens says it can be pressed.
    hoverAt(lone.scene, lone.on(3));
    REQUIRE_FALSE(lone.strip->hoveredBid().has_value());

    hoverAt(lone.scene, lone.on(4));
    REQUIRE(lone.strip->hoveredBid() == 4u);

    // Off the strip, it goes out again.
    hoverAt(lone.scene, QPointF(-500.0, -500.0));
    REQUIRE_FALSE(lone.strip->hoveredBid().has_value());
}

TEST_CASE("A press on the strip between its bids goes no further", "[bid-strip]")
{
    // What lies beneath: something that takes presses, and counts them - the felt,
    // a card, the logo.
    struct Beneath : QGraphicsRectItem
    {
        int presses = 0;

        explicit Beneath(const QRectF& rect) : QGraphicsRectItem(rect)
        {
            setAcceptedMouseButtons(Qt::LeftButton);
        }

        void mousePressEvent(QGraphicsSceneMouseEvent* event) override
        {
            presses++;
            event->accept();
        }
    };

    LoneStrip lone;
    lone.strip->setPrompt(promptFor(6));

    Beneath* beneath = new Beneath(lone.strip->sceneBoundingRect());
    beneath->setZValue(lone.strip->zValue() - 1.0);
    lone.scene.addItem(beneath);

    // On the question, and in the gap between two bids.
    const QPointF question = lone.strip->mapToScene(QPointF(lone.strip->size().width() / 2.0, 20.0));
    const QPointF gap = (lone.on(0) + lone.on(1)) / 2.0;

    pressAndRelease(lone.scene, question, question);
    pressAndRelease(lone.scene, gap, gap);

    REQUIRE(beneath->presses == 0);
    REQUIRE(lone.chosen.empty());
}

TEST_CASE("The strip lies on the felt clear of your hand and your panel", "[bid-strip]")
{
    // SECTION 2.13's place, over the logo. The strip used to be bottom-aligned
    // above your hand, which C2's first run drew straight through your plaque; on
    // the felt it clears both by construction, and this holds it to that for every
    // table and for the taller strip a forbidden bid's note makes.
    for(unsigned int seats = 2 ; seats <= 6 ; seats++)
    {
        TableScene scene;
        scene.setTheme(Theme::dark());
        scene.setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(seats)));

        BidStripItem* strip = scene.bidStripItem();
        REQUIRE(strip != nullptr);

        for(const std::optional<unsigned int> forbidden : { std::optional<unsigned int>(), std::optional<unsigned int>(2u) })
        {
            INFO(seats << " seats, " << (forbidden ? "a" : "no") << " forbidden bid");

            strip->setPrompt(promptFor(8, forbidden));

            const QRectF placed = strip->mapRectToScene(QRectF(QPointF(), strip->size()));

            REQUIRE(placed.topLeft() == tablespace::bidStripOrigin);
            REQUIRE(placed.width() == tablespace::bidStripWidth);

            // ON THE FELT: every corner inside its circle.
            for(const QPointF corner : { placed.topLeft(), placed.topRight(),
                                         placed.bottomLeft(), placed.bottomRight() })
            {
                REQUIRE(QLineF(tablespace::centre, corner).length() < tablespace::feltRadius);
            }

            REQUIRE(placed.bottom() < tablespace::handTop);
            REQUIRE_FALSE(placed.intersects(seatlayout::panelRect(seats, 0, 0)));

            // And over everything else on the table.
            for(QGraphicsItem* item : scene.items())
            {
                if(item != strip && !item->parentItem())
                    REQUIRE(item->zValue() < strip->zValue());
            }
        }
    }
}

// THE CORNER PANELS FOLLOW THE WINDOW and the strip does not: it is in table space
// on the felt, and they are pinned to the window's corners. At a shape the panels
// reach furthest into the table, the strip must still lie clear of all four, or the
// strip (drawn over them) would hide a part of the scoreboard.
TEST_CASE("The strip clears the corner panels at the sizes people play at", "[bid-strip]")
{
    const QSize windows[] = { QSize(1920, 1080), QSize(2560, 1440), QSize(1920, 1200),
                              QSize(1440, 900) };

    for(unsigned int seats = 2 ; seats <= 6 ; seats++)
    {
        TableScene scene;
        scene.setTheme(Theme::dark());
        scene.setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(seats)));

        TableView view(&scene);
        view.show();

        BidStripItem* strip = scene.bidStripItem();
        REQUIRE(strip != nullptr);

        // The tallest strip: a forbidden bid's note is the longer sentence.
        strip->setPrompt(promptFor(8, std::optional<unsigned int>(2u)));

        const QRectF placed = strip->mapRectToScene(QRectF(QPointF(), strip->size()));

        for(const QSize& window : windows)
        {
            INFO(seats << " seats, window " << window.width() << "x" << window.height());

            view.resize(window);
            QCoreApplication::processEvents();

            // The panel's own rect, as HudTests measures it: its shadow is not part of
            // what a reader loses to the strip.
            const auto clear = [&placed](const auto& corner)
            {
                REQUIRE(corner.isVisible());
                REQUIRE_FALSE(placed.intersects(corner.mapRectToScene(QRectF(QPointF(), corner.size()))));
            };

            clear(*scene.scoreboardItem());
            clear(*scene.trickPanelItem());
            clear(*scene.historyButton());
            clear(*scene.lastTrickButton());
        }
    }
}

TEST_CASE("A long note wraps and the strip grows to hold it", "[bid-strip]")
{
    // A translation may need two lines where English needs one. The strip keeps its
    // width - section 2.13's 680 - and grows down instead.
    bidstrip::Fields shortNote = fieldsOf(6);
    bidstrip::Fields longNote = shortNote;

    longNote.note = shortNote.note + QLatin1Char(' ') + shortNote.note + QLatin1Char(' ') + shortNote.note
                  + QLatin1Char(' ') + shortNote.note;

    REQUIRE(bidstrip::sizeFor(longNote).width() == tablespace::bidStripWidth);
    REQUIRE(bidstrip::sizeFor(longNote).height() > bidstrip::sizeFor(shortNote).height());

    // The bids stay where they were: the note is under them.
    REQUIRE(bidstrip::buttonRect(longNote, 0) == bidstrip::buttonRect(shortNote, 0));
}

TEST_CASE("A bid under a still cursor lights when you are asked", "[bid-strip]")
{
    // THE SCENE LIGHTS NOTHING UNTIL THE MOUSE MOVES, and the strip appears wherever
    // the cursor was left - over it, often, in a round dealt from the setup
    // dialog's Deal. The mock-up's browser lights the bid at once; phase 7 found
    // the same on the hand.
    TableScene scene;
    scene.setTheme(Theme::dark());

    const GameSnapshot table = biddingTable(4);
    scene.setSnapshot(std::make_shared<const GameSnapshot>(table));

    const BetPrompt prompt = promptFor(6);
    const QPointF one = tablespace::bidStripOrigin
                      + bidstrip::buttonRect(bidstrip::fieldsFor(prompt, table), 1).center();

    // The cursor comes to rest there while nothing is asking.
    hoverAt(scene, one);
    REQUIRE_FALSE(scene.bidStripItem()->isVisible());

    scene.bidStripItem()->setPrompt(prompt);
    REQUIRE(scene.bidStripItem()->hoveredBid() == 1u);

    // A cursor that has left the view lights nothing.
    scene.bidStripItem()->clearPrompt();

    // As a view sends it: the scene reads the view through the viewport the event
    // names, and a leave naming none crashes it.
    TableView view(&scene);

    QGraphicsSceneEvent leave(QEvent::GraphicsSceneLeave);
    leave.setWidget(view.viewport());
    QCoreApplication::sendEvent(&scene, &leave);

    scene.bidStripItem()->setPrompt(prompt);
    REQUIRE_FALSE(scene.bidStripItem()->hoveredBid().has_value());
}

TEST_CASE("A bid lit under a still cursor goes out when the cursor leaves the strip", "[bid-strip]")
{
    // PR #36's review, finding #3. A strip lit by setPrompt() was never in the
    // scene's hover list - the cursor had not moved over it - so the scene sent it
    // no hover leave: a cursor that left in one jump, or left the view, left the
    // bid lit with nothing on it.
    TableScene scene;
    scene.setTheme(Theme::dark());

    const GameSnapshot table = biddingTable(4);
    scene.setSnapshot(std::make_shared<const GameSnapshot>(table));

    BidStripItem* strip = scene.bidStripItem();
    const BetPrompt prompt = promptFor(6);
    const QPointF one = tablespace::bidStripOrigin
                      + bidstrip::buttonRect(bidstrip::fieldsFor(prompt, table), 1).center();

    // Off the strip in one move.
    hoverAt(scene, one);
    strip->setPrompt(prompt);
    REQUIRE(strip->hoveredBid() == 1u);

    hoverAt(scene, QPointF(-500.0, -500.0));
    REQUIRE_FALSE(strip->hoveredBid().has_value());

    // Off the view. As a view sends it - see "A bid under a still cursor lights
    // when you are asked".
    strip->clearPrompt();
    hoverAt(scene, one);
    strip->setPrompt(prompt);
    REQUIRE(strip->hoveredBid() == 1u);

    TableView view(&scene);

    QGraphicsSceneEvent leave(QEvent::GraphicsSceneLeave);
    leave.setWidget(view.viewport());
    QCoreApplication::sendEvent(&scene, &leave);

    REQUIRE_FALSE(strip->hoveredBid().has_value());
}

TEST_CASE("A bid lit under a still cursor stays lit when the note changes", "[bid-strip]")
{
    // PR #36's review, finding #3, the other way round: refresh() put the light out
    // for any change to what the strip says, and nothing lit it again until the
    // mouse moved. The bids do not move when the note does.
    TableScene scene;
    scene.setTheme(Theme::dark());

    const GameSnapshot table = biddingTable(4);
    scene.setSnapshot(std::make_shared<const GameSnapshot>(table));

    BidStripItem* strip = scene.bidStripItem();
    const BetPrompt prompt = promptFor(6);
    const QPointF one = tablespace::bidStripOrigin
                      + bidstrip::buttonRect(bidstrip::fieldsFor(prompt, table), 1).center();

    hoverAt(scene, one);
    strip->setPrompt(prompt);
    REQUIRE(strip->hoveredBid() == 1u);

    const bidstrip::Fields before = strip->fields();
    scene.setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(4, 1, 2)));

    REQUIRE(strip->fields().note != before.note);
    REQUIRE(strip->hoveredBid() == 1u);
}

TEST_CASE("A snapshot that changes nothing does not compose the strip again", "[bid-strip]")
{
    // relayout() hands the strip every snapshot, about 1300 a game. One that leaves
    // what the strip says alone must not have it composed again - two blurred
    // shadows and up to nine buttons' - nor repainted.
    TableScene scene;
    scene.setTheme(Theme::dark());
    scene.setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(4, 1, 2)));

    BidStripItem* strip = scene.bidStripItem();
    strip->setPrompt(promptFor(8));

    const auto render = [&scene]
    {
        QImage image(QSize(940, 662), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        scene.render(&painter, QRectF(QPointF(), QSizeF(image.size())), scene.sceneRect());
    };

    render();
    const std::size_t composed = strip->compositions();
    REQUIRE(composed >= 1);

    scene.setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(4, 1, 2)));
    render();

    REQUIRE(strip->compositions() == composed);

    // The control: a frame that changes the note is seen to.
    scene.setSnapshot(std::make_shared<const GameSnapshot>(biddingTable(4, 2, 5)));
    render();

    REQUIRE(strip->compositions() == composed + 1);
}

// ---- the answer the seated-game tests bid with ----
//
// These three moved here from PromptStandInTests in C2, with the function they
// cover: BidStrip took chooseBet()'s only production caller, so it became
// fixtures::lowestLegalBid(). They are still worth keeping - HumanSeatTests
// answers every bid of three whole games with it, and a wrong answer there is a
// re-prompt loop rather than a failure.

TEST_CASE("The tests' own bidder bids zero when zero is allowed", "[bid-strip]")
{
    REQUIRE(fixtures::lowestLegalBid(promptFor(8, 3u)) == 0);
}

TEST_CASE("The tests' own bidder bids one when zero is forbidden", "[bid-strip]")
{
    // The whole reason it is a function rather than a literal 0.
    REQUIRE(fixtures::lowestLegalBid(promptFor(8, 0u)) == 1);
}

TEST_CASE("The tests' own bidder bids zero when nothing is forbidden", "[bid-strip]")
{
    REQUIRE(fixtures::lowestLegalBid(promptFor(1)) == 0);
}
