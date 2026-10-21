#ifndef BID_STRIP_ITEM_H
#define BID_STRIP_ITEM_H

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/Panel.h"
#include "paint/PictureMemo.h"
#include "paint/Theme.h"

#include <QGraphicsObject>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>

// Section 15.1's strip: the question, one button per legal bid, and a line under
// them - the reason one bid is struck out, or how the bids stand so far.
//
// ON THE FELT SINCE THE TABLE REDESIGN'S PHASE 10 (section 2.13): a panel at
// (428, 486), 680 wide, over the logo, drawn and scaled with the table like every
// other panel on it. It was a widget floated over the bottom of the view until
// then, placed above your hand by TableScene::overlayBottom().
//
// IN THE SCENE, NOT A WIDGET OVER IT, by the owner's call of 2026-10-21 - the
// corner panels' arrangement (phase 8): one cached picture, at the pixels it lands
// on, which a window being dragged scales rather than paints again.
//
// A PANEL AND NOT A QDialog, which is a shutdown decision rather than a taste one.
// QDialog::exec() would spin a nested event loop for exactly as long as the game
// thread is parked in makeBet(), which is unbounded - and a modal dialog takes the
// close button with it, so MainWindow::closeEvent never runs, stop() is never
// called, and abandon() never wakes the provider. See section 4.4.
namespace bidstrip
{

// TRANSLATED, through QCoreApplication::translate() with an explicit context at
// every call site, NOT a tr() of our own: Q_DECLARE_TR_FUNCTIONS is class-only
// whatever its name suggests, and a hand-rolled wrapper hides every string from
// lupdate.

// What the strip says, from the prompt and the frame it arrived on. No painter
// here, which is what makes it the part worth a test - the split SnapshotLog.h
// argues for and the scoreboard follows.
struct Fields
{
    QString question;

    // Bids are 0..highest INCLUSIVE, so an 8-card round has nine of them.
    // BetContext carries no trick count of its own; the hand's size IS the number
    // of tricks and so the highest legal bid.
    unsigned int highest = 0;

    // Exactly one value or none. The engine fills it from getForbiddenBet() for the
    // final bidder only, and leaves it empty once the bids so far already exceed
    // the trick count. THE CLIENT ASKS; IT DOES NOT REIMPLEMENT THE ARITHMETIC.
    std::optional<unsigned int> forbidden;

    // The line under the bids, never empty: why the struck-out bid is out, or -
    // the mock-up's other sentence - how many have bid so far and what their bids
    // add up to.
    QString note;

    bool operator==(const Fields&) const = default;
};

// THE TWO SOURCES: the prompt says what may be bid, and the snapshot published
// just before it (GameBridge::onBetRequested()) says how the others have bid.
Fields fieldsFor(const BetPrompt& prompt, const GameSnapshot& snapshot);

// Whether `bet` is one the strip offers: on it, and not the forbidden one.
bool offers(const Fields& fields, unsigned int bet);

// WHAT A KEY PRESS MEANS WHILE THE STRIP IS UP (section 15.1): the digit keys bid
// directly. Read on the window rather than on the strip, as the hand's digits are,
// so the window asks here and does what it is told.
struct KeyAnswer
{
    enum class Kind
    {
        // Not the strip's key: the window passes it on, so a key it binds still
        // reaches it.
        Pass,

        // The forbidden bid's digit, which section 15.1 asks to be ignored: taken,
        // and nothing bid.
        Refuse,

        Bid
    };

    Kind kind = Kind::Pass;
    unsigned int bet = 0;

    bool operator==(const KeyAnswer&) const = default;
};

// UNMODIFIED DIGITS ONLY. A bid is irrevocable, so Ctrl+2 must not place one: that
// chord belongs to whatever the window binds it to. The keypad's own modifier is
// masked back in, because keypad 2 is the same 2 to the person pressing it. ZERO-
// BASED, where the hand's digits count from 1: a bid of zero is a real bid.
KeyAnswer answerKey(int key, Qt::KeyboardModifiers modifiers, const Fields& fields);

// The strip's size in table units, its top-left at its origin: section 2.13's
// width, and the height its question, bids and note stack to.
QSizeF sizeFor(const Fields& fields);

// Where the button for `bet` is, in the strip's own frame. Empty for a bid the
// strip has no button for.
QRectF buttonRect(const Fields& fields, unsigned int bet);

// The bid whose button is under `point`, in the strip's own frame - the forbidden
// one included; offers() says whether it can be chosen.
std::optional<unsigned int> bidAt(const Fields& fields, QPointF point);

} // namespace bidstrip

// The strip on the table.
//
// IT KNOWS NOTHING ABOUT PROVIDERS OR PROMPT IDS. It emits the bid that was chosen
// and MainWindow does the rest - which is what keeps the generation gate (section
// 4.2) in one place instead of two.
//
// TWO SOURCES, as the trick panel has: every snapshot from TableScene, and the
// prompt from MainWindow, which hands it over when the engine asks for your bid and
// takes it back once you have answered or the game has moved on. The strip is shown
// exactly while it holds a prompt.
class BidStripItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // `panels` is the chrome cache the strip shares with the seats, which are drawn
    // at the same scale, owned by TableScene. Null gives the strip one of its own.
    explicit BidStripItem(panel::Cache* panels, QGraphicsItem* parent = nullptr);

    void setSnapshot(std::shared_ptr<const GameSnapshot> snapshot, const Theme& theme);

    // REBUILT PER PROMPT rather than built once: handSize changes every round - the
    // schedule runs 1 card up to 8 and back down - so the strip has as many bids as
    // this round offers and no more.
    //
    // A BID UNDER A CURSOR THAT HAS NOT MOVED LIGHTS AT ONCE, as the mock-up's
    // browser lights it: the scene sends no hover until the mouse moves, and the
    // strip appears under wherever the cursor was left (TableScene::cursorPosition()).
    void setPrompt(const BetPrompt& prompt);
    void clearPrompt();

    // WHERE THE CURSOR WENT, in table space, or nothing once it has left the view:
    // from TableScene::event(), on every move. Puts out a bid lit under a resting
    // cursor (setPrompt()) once the cursor is off the strip - the scene sends no
    // hover leave to an item it never sent a hover to.
    void followCursor(std::optional<QPointF> scenePos);

    // Bids `bet` if the strip offers it, as a click on its button would: the
    // keyboard's road, and the tests'. False, and nothing bid, otherwise.
    bool activate(unsigned int bet);

    // What the strip says, the bid lit under the cursor, and where a bid's button
    // is in the strip's frame: READ-ONLY and for tests.
    const bidstrip::Fields& fields() const { return showing; }
    std::optional<unsigned int> hoveredBid() const { return hovered; }
    QRectF buttonRect(unsigned int bet) const { return bidstrip::buttonRect(showing, bet); }

    // The strip's own rect, its top-left at the item's origin.
    QSizeF size() const { return stripSize; }

    // The panel plus its drop shadow.
    QRectF boundingRect() const override;

    // The panel as drawn, which is what takes the mouse.
    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // How many times the strip has been composed: READ-ONLY and for tests, as
    // SeatItem::compositions() is.
    std::size_t compositions() const { return memo.compositions(); }

signals:
    // THE BID, not the button's position. They are equal today because the buttons
    // run 0..highest with none missing, and writing the index would still be wrong:
    // it is the same class of mistake as submitting a display slot where the engine
    // wants a hand position, and the play side has a whole type
    // (PlayPrompt::handIndex) built to prevent it.
    void bidChosen(unsigned int bet);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverMoveEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // Works out what the strip says from its two sources, and redraws it only if
    // that changed.
    void refresh();

    // Lights the bid offered under `point`, in the strip's frame, or none.
    void hoverAt(std::optional<QPointF> point);

    // The bid that would light under `point`, in the strip's frame: an offered one,
    // while a prompt is held.
    std::optional<unsigned int> offeredAt(std::optional<QPointF> point) const;

    // Where the scene last saw the cursor (TableScene::cursorPosition()), in the
    // strip's frame, or nothing.
    std::optional<QPointF> cursorHere() const;

    const panel::Picture& picture(qreal ratio) const;

    mutable panel::Cache ownPanels;
    panel::Cache* panels;

    Theme theme = Theme::dark();
    std::shared_ptr<const GameSnapshot> snapshot;
    std::optional<BetPrompt> prompt;

    bidstrip::Fields showing;
    QSizeF stripSize;

    // The first bid's button in the strip's frame, worked out with `showing`
    // (refresh()). The others follow it at a fixed step.
    QRectF firstBid;

    std::optional<unsigned int> hovered;

    // The bid a press landed on: a click is a press and a release on the same one.
    std::optional<unsigned int> pressed;

    mutable PictureMemo memo;
};

#endif
