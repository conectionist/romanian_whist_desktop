#ifndef TABLE_SCENE_H
#define TABLE_SCENE_H

#include "core/GameSnapshot.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <QGraphicsScene>
#include <QPointF>
#include <QRectF>

#include <memory>
#include <optional>
#include <vector>

class HandItem;
class SeatItem;
class TableTopItem;
class TrickItem;

// The table: seats on an ellipse, the trick in the middle.
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

    // The theme the table is drawn in. TableView draws the room behind it from
    // this, so the room and the table cannot be in two different themes.
    const Theme& currentTheme() const { return theme; }

    // WHERE AN OVERLAY MUST STOP: the table-space y just above your hand - or, at
    // a table with no hand, above the lowest name panel. Section 15.1's bid strip is
    // bottom-aligned above it until the redesign's phase 10 moves the strip onto
    // the felt. In SCENE units: TableView maps it to the viewport.
    //
    // IT HAS TO COME FROM HERE because the bottom of the table is not where the
    // strip goes. Your hand and your own panel are both down there, and a strip
    // pinned to the viewport's bottom edge draws straight through them - which is
    // what C2's first run did, through the plaque.
    //
    // A FLOOR AND NO CEILING. The strip may rise over the trick, which is not a
    // compromise: bidding finishes before the first card of the round is played,
    // so the trick area is empty for the whole time the strip is up. And it may
    // rise past the top of the table into the room above, which is the view's to
    // give - so only the bottom is the scene's to say. The hand is a different
    // matter, because you bid on the cards you are holding.
    //
    // Nothing before the first snapshot, and the caller falls back.
    std::optional<qreal> overlayBottom() const;

    // The table itself, and the renderer every card on it is drawn by. READ-ONLY
    // and for tests, which count what each of them paints to show that a resize
    // leaves their caches alone (TableViewTests).
    const TableTopItem* tableTop() const { return top; }
    const CardRenderer& cardRenderer() const { return cards; }

private:
    // Puts every item where the snapshot says, in table space. Run on a new
    // snapshot and a new theme and on nothing else: a resize is the view's
    // business, and changes no position here.
    void relayout();

    // Grown when a game has more seats than the last one did. Six at most, and
    // reused across games rather than rebuilt, for the reason in the class
    // comment.
    void ensureSeats(std::size_t count);

    // Played-card positions, indexed by seat. See the definition for where they
    // come from; the seats' own panels are seatlayout::panelRect()'s.
    std::vector<QPointF> trickPositions(std::size_t seatCount) const;

    Theme theme = Theme::light();

    // BY VALUE, so its cache lives as long as the scene and a repaint costs
    // nothing after the first. Declared before the items, which hold a pointer
    // to it: members die in reverse declaration order, so this outlives them.
    CardRenderer cards;

    std::shared_ptr<const GameSnapshot> snapshot;

    // The table and its logo, under everything else (tablespace::z).
    TableTopItem* top = nullptr;

    std::vector<SeatItem*> seats;
    TrickItem* trick = nullptr;

    // The human's fan. Built for every game, and hidden in a spectator one -
    // section 14: "the bottom hand area is not drawn at all".
    HandItem* hand = nullptr;
};

#endif
