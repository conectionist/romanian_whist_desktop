#ifndef TABLE_SCENE_H
#define TABLE_SCENE_H

#include "core/GameSnapshot.h"
#include "paint/CardRenderer.h"
#include "paint/CardShadow.h"
#include "paint/Panel.h"
#include "paint/Theme.h"
#include "table/Motion.h"
#include "table/RackItem.h"

#include <QGraphicsScene>
#include <QPointF>
#include <QRectF>

#include <memory>
#include <optional>
#include <vector>

class BidBubbleItem;
class BidStripItem;
class HandItem;
class LeadMarkItem;
class ScoreboardItem;
class SeatItem;
class TableButtonItem;
class TableTopItem;
class TrickItem;
class TrickPanelItem;

// The table: name panels round it, racks on its rim, the trick on its felt.
//
// IN TABLE SPACE, A FIXED SET OF COORDINATES (TableSpace.h) that TableView scales
// to the window. The scene rect is the fit box and never changes, so nothing here
// knows how big the window is and a resize moves nothing - it is the view's
// transform that changes, and the whole table scales as one picture.
//
// IT READS ONE SNAPSHOT AND NOTHING ELSE. Section 3's cardinal rule is that
// widgets never touch the engine, and the reason is not tidiness: the engine has
// no locking anywhere, so a GUI-thread read racing the game thread is a torn
// read of a hand mid-mutation. Everything drawn here arrives as a value on a
// queued signal.
//
// ITEMS ARE BUILT ONCE AND UPDATED IN PLACE, not cleared and rebuilt per frame.
// A game publishes about 1300 snapshots and rebuilding six plaques and six cards
// for each of them is work for nothing - but the real reason is phase E, which
// animates a CardItem's pos from where it was to where it is going, and cannot
// if the item it was is gone.
class TableScene : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit TableScene(QObject* parent = nullptr);

    // The whole of the scene's input. Null is ignored rather than treated as an
    // empty game: a queued snapshot is a shared_ptr and nothing should be able
    // to blank the table by dropping one.
    void setSnapshot(std::shared_ptr<const GameSnapshot> snapshot);

    // THE FAN, which has a second source the snapshot cannot supply. Everything
    // else on this table is drawn from the snapshot and only the snapshot, and
    // the hand is the documented exception (section 4.0): between turns it
    // follows setSnapshot() like the rest, and on your turn MainWindow redraws it
    // from the prompt through here.
    HandItem* handItem() const { return hand; }

    // The frame the table is drawn from, or null before the first one. READ-ONLY
    // and for tests: tests/ScreenshotTool.cpp asserts which moment of the game
    // each picture shows, and this is the one place that knows it without
    // parsing a label the redesign is free to remove.
    std::shared_ptr<const GameSnapshot> lastSnapshot() const { return snapshot; }

    void setTheme(const Theme& theme);

    // HOW MUCH THE TABLE MOVES (the redesign's phase 12): Settings' "Card
    // animations", handed over by MainWindow, live, as the pace is. OFF UNTIL SAID
    // OTHERWISE, so a scene built on its own - in a test, or the screenshot tool -
    // shows each frame at once, finished; the window sets the shipped default.
    void setMotion(motion::Pace pace);
    motion::Pace motionPace() const { return animationPace; }

    // The theme the table is drawn in. TableView draws the room behind it from
    // this, so the room and the table cannot be in two different themes.
    const Theme& currentTheme() const { return theme; }

    // SECTION 2.13's BID STRIP, on the felt over the logo. Like the fan and the
    // trick panel it has a second source the snapshot cannot supply - the prompt
    // for your bid - and MainWindow hands that over through here.
    BidStripItem* bidStripItem() const { return bidStrip; }

    // WHERE THE CURSOR LAST WAS over a view of this scene, in table space, and
    // nothing once it has left. The scene lights nothing under a cursor that has
    // not moved, so an item shown under one asks here (BidStripItem::setPrompt()).
    std::optional<QPointF> cursorPosition() const { return cursor; }

    // The table itself, and the renderer every card on it is drawn by. READ-ONLY
    // and for tests, which count what each of them paints to show that a resize
    // leaves their caches alone (TableViewTests).
    const TableTopItem* tableTop() const { return top; }
    const CardRenderer& cardRenderer() const { return cards; }

    // The cards' shadow pictures - on the felt and in your hand - READ-ONLY and for
    // tests in the same way.
    const cardshadow::Cache& cardShadows() const { return shadowPictures; }

    // The name panels' and the corner buttons' chrome - not the scoreboard's or
    // the trick panel's - READ-ONLY and for tests in the same way.
    const panel::Cache& seatPanels() const { return panels; }

    // The lead rhombus, READ-ONLY and for tests in the same way.
    const LeadMarkItem* leadMarkItem() const { return leadMark; }

    // The trick, and with it the cards in flight: for tests in the same way.
    const TrickItem* trickItem() const { return trick; }

    // SEAT `seat`'S BID BUBBLE (phase 12), shown for a moment as its bid is made:
    // READ-ONLY and for tests in the same way. Null for a seat the table has never had.
    BidBubbleItem* bidBubble(std::size_t seat) const { return seat < bubbles.size() ? bubbles[seat] : nullptr; }

    // PINS THE CORNER PANELS TO THE WINDOW (section 2.1): `visible` is the part of
    // the scene the viewport shows, in table space, and `scale` the view's pixels
    // per table unit. Each panel goes tablespace::hudInset pixels in from its
    // corner. TableView calls this on every fit, and nothing else moves when it
    // does - the corner panels are the scene's only items that follow the window.
    //
    // NOT SHOWN UNTIL THERE IS SOMEWHERE TO PIN THEM AND A GAME TO SHOW: a scene
    // drawn without a view, or a table with no snapshot yet, has no corner panels.
    void pinHud(const QRectF& visible, qreal scale);

    // Section 2.9's scoreboard, top-left. READ-ONLY and for tests.
    const ScoreboardItem* scoreboardItem() const { return scoreboard; }

    // Section 2.10's trick panel, top-right. Like the fan, it has a second source
    // the snapshot cannot supply - the prompt for your card - and MainWindow hands
    // that over through here.
    TrickPanelItem* trickPanelItem() const { return trickPanel; }

    // Section 2.11's two buttons, whose clicked() MainWindow opens a sheet on.
    TableButtonItem* historyButton() const { return history; }
    TableButtonItem* lastTrickButton() const { return lastTrick; }

protected:
    // Notes where the cursor is, for cursorPosition(), and passes every event on.
    bool event(QEvent* event) override;

private:
    // Puts every item where the snapshot says, in table space. Run on a new
    // snapshot and a new theme and on nothing else: a resize is the view's
    // business, and changes no position here.
    void relayout();

    // A BID JUST MADE, between the last frame and `next`: each seat whose bid is in
    // now and was not then, in the same round, says it over its panel (phase 12). A
    // new game, or another table, clears them all.
    void announceBids(const GameSnapshot* last, const GameSnapshot& next);

    // Grown when a game has more seats than the last one did, panels and racks
    // together. Six at most, and reused across games rather than rebuilt, for the
    // reason in the class comment.
    void ensureSeats(std::size_t count);

    // Puts the corner panels where the last pinHud() said, or hides them while
    // there is no pin or no game. See pinHud().
    void placeHud();

    Theme theme = Theme::light();

    // See setMotion().
    motion::Pace animationPace = motion::Pace::Off;

    // BY VALUE, so its cache lives as long as the scene and a repaint costs
    // nothing after the first. Declared before the items, which hold a pointer
    // to it: members die in reverse declaration order, so this outlives them.
    CardRenderer cards;

    // The seats' panel chrome, shared by all of them and the corner buttons, for
    // the renderer's reasons and held the same way.
    panel::Cache panels;

    // THE SCOREBOARD'S AND THE TRICK PANEL'S, apart from the seats': drawn at
    // tablespace::hudScale, they ask for their chrome at 1.2 times the ratio the
    // seats do, and a cache holds one ratio at a time. Shared, each turn's seats
    // and corner panels cleared each other's pictures and drew their shadows again.
    panel::Cache hudPanels;

    // The racks' pictures, shared by every rack showing the same cards, and held
    // the same way.
    rackitem::Cache rackPictures;

    // The cards' shadows, shared by every card on the felt and in your hand, and
    // held the same way.
    cardshadow::Cache shadowPictures;

    std::shared_ptr<const GameSnapshot> snapshot;

    // The table and its logo, under everything else (tablespace::z).
    TableTopItem* top = nullptr;

    std::vector<SeatItem*> seats;

    // Every seat's rack, indexed as `seats` is. Yours is built with the rest and
    // never shown: your hand is the fan at the bottom (section 2.6).
    std::vector<RackItem*> racks;

    // Every seat's bid bubble, indexed as `seats` is.
    std::vector<BidBubbleItem*> bubbles;

    TrickItem* trick = nullptr;

    // The rhombus on the felt marking who leads the trick (section 2.7).
    LeadMarkItem* leadMark = nullptr;

    // The human's fan. Built for every game, and hidden in a spectator one -
    // section 14: "the bottom hand area is not drawn at all".
    HandItem* hand = nullptr;

    // The corner panels (tablespace::z::hud).
    ScoreboardItem* scoreboard = nullptr;
    TrickPanelItem* trickPanel = nullptr;
    TableButtonItem* history = nullptr;
    TableButtonItem* lastTrick = nullptr;

    // The bid strip (tablespace::z::bidStrip), hidden until you are asked to bid.
    BidStripItem* bidStrip = nullptr;

    // See cursorPosition().
    std::optional<QPointF> cursor;

    // Where pinHud() last said the viewport is: none before a view has fitted the
    // table.
    struct Pin
    {
        QRectF visible;
        qreal scale = 0.0;
    };

    std::optional<Pin> pin;
};

#endif
