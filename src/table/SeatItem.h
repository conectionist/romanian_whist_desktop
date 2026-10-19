#ifndef SEAT_ITEM_H
#define SEAT_ITEM_H

#include "core/GameSnapshot.h"
#include "paint/Panel.h"
#include "paint/PictureMemo.h"
#include "paint/Theme.h"

#include <QGraphicsObject>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <cstddef>

// One seat's name panel (the table redesign's section 2.5): a portrait, the name
// with the total score after it, the Taken/Bid pill, and the turn ring.
//
// A QGraphicsObject for the same reason CardItem is one - section 18.2 fades the
// turn ring in over 120ms in phase E, which is a property animation on an object.
//
// THE TURN RING IS DRIVEN BY snapshot.activeSeat AND NOTHING ELSE. Section 14 is
// explicit that it must never be counted forward from the leader, and the
// snapshot's own comment says why activeSeat is the right field: the engine
// engages it from just before a provider is asked until just after its answer is
// recorded, so it stays on the seat that just bid or just played and goes empty
// between turns without any callback having to remember to say so.
// WHAT GOES WHERE ON A PANEL, and what its two lines say, which is the part of
// this item with a right answer - so they are free functions, the split
// SnapshotLog.h argues for and seatlayout, infostrip and bidstrip all follow.
// They need no scene, no painter and no seat to have that answer checked.
namespace seatitem
{

// The two lines' type sizes in table units: .name is Marcellus 25 and .pill
// Alegreya Sans 19. Here rather than private because tablespace::minimumScale is
// chosen to keep the name readable, and a test holds the two together.
inline constexpr int nameSize = 25;
inline constexpr int pillSize = 19;

// page.html's .plaque, in table units, centred on the item's origin.
struct Layout
{
    // The panel's border box.
    QRectF panel;

    // The portrait's box, its 1-unit gold border included.
    QRectF portrait;

    // The column the name and the pill share: from 12 past the portrait to the
    // panel's right padding. The name is elided against this width.
    QRectF text;
};

// The panel's border box for a panel of `size`, centred on the item's origin.
QRectF panelRect(QSizeF size);

// NO CARD ON THE PANEL. A Forehead round's visible card stood in the portrait's
// place until the redesign's phase 5, which put it face up in the seat's rack
// (RackItem) - so the panel is the same in every round.
Layout layoutFor(QSizeF panel);

// The portrait's picture, inside its gold border: 86 x 98 table units. What
// Theme::avatar() is asked for at, so the Card art window can show it at the size
// the table does.
QSizeF portraitPicture();

// Section 2.5's pill, which is three states once the bidding is over and a
// neutral one before. `Neutral` covers "No bid yet", "Bidding…" and a bid made
// while others are still bidding, which the mock-up does not colour: nobody has
// taken a trick yet, so "on target" would be a claim about nothing.
enum class Pill
{
    Neutral,
    OnTarget,
    Owing,
    Over
};

struct PillView
{
    QString text;
    Pill state = Pill::Neutral;

    bool operator==(const PillView&) const = default;
};

// table.js pillFor(). `active` is whether this seat is the one being asked to
// move, and `biddingOpen` whether any seat has still to bid this round.
//
// NO BID IS NOT A BID OF ZERO. hasBet is on the snapshot precisely to tell them
// apart, and reading `bet` without it makes every seat look like it bid nothing.
// THE NUMBERS ARE ALWAYS IN THE TEXT, so the colour is never the only cue
// (section 18).
PillView pillFor(const SeatView& seat, bool active, bool biddingOpen);

// The name line, in three parts painted in three colours: the active seat's "▸ ",
// the name, and " (44)", the total score.
//
// ONLY THE NAME IS ELIDED, against what the other two leave of `width`, so a long
// name never hides the score. No " (you)" and no "AI" tag: the mock-up has
// neither, and the bottom panel is yours by where it is (the owner's decision of
// 2026-10-18; open question Q5).
struct NameLine
{
    QString marker;
    QString name;
    QString score;

    bool operator==(const NameLine&) const = default;
};

NameLine nameLineFor(const SeatView& seat, bool active, qreal width);

// EVERYTHING THE PANEL'S PICTURE SHOWS of a seat, worked out once per snapshot.
// The picture is painted from this and composed again only when it changes, so
// a thing drawn on the panel cannot be left out of that comparison: to be drawn
// it has to be in here. The chrome's palette, size and ratio and the portrait's
// look are not about the seat, and SeatItem compares them itself.
struct Shown
{
    NameLine name;
    PillView pill;

    bool operator==(const Shown&) const = default;
};

} // namespace seatitem

class SeatItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // `panels` is the chrome cache every seat shares, owned by TableScene. Null gives
    // the item one of its own.
    //
    // NO CARD RENDERER since the redesign's phase 5: a Forehead round's card is
    // drawn in the seat's rack, and the panel draws no card at all.
    explicit SeatItem(panel::Cache* panels, QGraphicsItem* parent = nullptr);

    // Everything this draws, from one snapshot's SeatView plus the facts that are
    // not on it. Taken as a whole rather than as a dozen setters because it is
    // repainted from a whole snapshot ~1300 times a game and a half-updated panel
    // is a state nothing should be able to reach.
    void setSeat(const SeatView& seat, bool active, bool biddingOpen, const Theme& theme);

    // The panel's size in TABLE units: tablespace::panelSize, or the bottom
    // panel's, which is wider.
    void setPanelSize(QSizeF size);

    // Which of the Theme's portraits this seat wears. A fact about where the seat
    // sits rather than about the snapshot, so it is set with the size.
    void setPortrait(unsigned int look);

    // The panel plus everything it paints past its edge - the drop shadow, and the
    // ring and glow whether or not they are showing: a rect that changed as the
    // turn moved would re-index the whole scene twice a card.
    QRectF boundingRect() const override;

    // The panel as drawn: the part of the item that takes the mouse, so a click on
    // it never reaches the hand beneath, while one on the shadow round it does.
    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // WHAT THIS PANEL DECIDED, exposed because it is a pure query.
    seatitem::Layout layoutFor() const;

    // How many times this panel has been composed: READ-ONLY and for tests, which
    // show with it that a resize settling, or a snapshot that changes nothing
    // here, leaves the cached panel alone - TableTopItem::renders()'s twin.
    std::size_t compositions() const { return memo.compositions(); }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // What the picture would show now, from the seat, the turn and the size.
    seatitem::Shown shownNow() const;

    void paintPortrait(QPainter& painter, const QRectF& box) const;
    void paintText(QPainter& painter, const QRectF& column) const;

    // THE WHOLE PANEL AS ONE PICTURE - the chrome, the portrait and the two lines
    // over it - composed again only when what it shows or the ratio changes. The
    // text and the portrait cost about half a millisecond each a frame over six
    // panels, which a drag, painting every panel every step, cannot spare; a
    // snapshot changes one or two panels' text, and only those are composed again.
    // `layout` is layoutFor()'s, which paint() has already asked for.
    const panel::Picture& picture(const seatitem::Layout& layout, qreal ratio) const;

    // The panel's chrome, shared with every other seat: its two shadows are blurs,
    // too dear to paint on every frame of a resize. `ownPanels` only when no
    // shared cache was given.
    mutable panel::Cache ownPanels;
    panel::Cache* panels;

    SeatView seat;
    bool active = false;
    bool biddingOpen = false;
    unsigned int look = 0;
    Theme theme = Theme::dark();
    QSizeF size;

    // What the composed picture shows, and is painted from.
    seatitem::Shown shown;

    // The composed panel, kept until what it shows or the ratio changes.
    mutable PictureMemo memo;
};

#endif
