#ifndef TRICK_PANEL_ITEM_H
#define TRICK_PANEL_ITEM_H

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/Panel.h"
#include "paint/PictureMemo.h"
#include "paint/TextRuns.h"
#include "paint/Theme.h"

#include <QGraphicsObject>
#include <QSizeF>
#include <QString>

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

// SECTION 2.10's CURRENT-TRICK PANEL, pinned to the window's top-right corner:
// which trick this is, who led it and with what, who is winning it - and, under a
// rule, where the game talks to you: "Your turn. You must follow ♥.", "Ana is
// thinking…", "You win the trick.".
//
// IT REPLACES THE PROMPT BAR, section 12's "one line, always in the same place,
// so you learn where to look" - which this still is, in a corner of the table
// rather than along the window's bottom edge.
//
// WHAT IT SAYS IS DECIDED HERE, in free functions that need no scene and no
// painter, and the item below only draws it - the split SnapshotLog.h argues for.
//
// Translated through QCoreApplication::translate() with an explicit context, for
// the reason table/ScoreboardItem.h gives at length.
namespace trickpanel
{

// page.html's .trickinfo is 264 wide in its own units, which are drawn at
// tablespace::hudScale times the table's.
inline constexpr qreal width = 264.0;

// THE LINE FOR YOUR OWN TURN TO PLAY, which is the question a dimmed hand raises.
// Legality lighting says WHICH cards you may play; this says WHY the others are
// out. It was the prompt bar's, promptbar::fieldsFor(), from phase C3 on.
//
// The prompt is what says it is your turn at all, and the snapshot is what says
// which suit is being followed. Both are needed: legality lives only on the
// prompt, and the lead suit only on the snapshot.
//
// A RESTRICTED HAND IS ONE WITH AN ILLEGAL CARD IN IT, asked of the entries
// rather than inferred from the lead suit. They usually agree and they come apart
// in the case that matters: a player void in the lead suit may play anything -
// unless they hold a trump, which they must then play (RULES.md section 2).
QString yourTurn(const PlayPrompt& prompt, const GameSnapshot& snapshot);

// Everything the panel says.
struct Fields
{
    // "Trick 3 of 6", "Bidding", "Round over".
    QString title;

    // "Leader: Bogdan · Led ♥", "Winning: Costel, K♥" - the names and the card
    // stand out, in cream.
    std::vector<textruns::Line> rows;

    // Under the rule, in gold-bright. Empty while nobody is being asked anything,
    // and the panel keeps its line for it all the same.
    textruns::Line prompt;

    bool operator==(const Fields&) const = default;
};

// From the snapshot, and - only while the engine is waiting for your card - the
// prompt it is waiting on. "You" stands for your own seat in every sentence.
Fields fieldsFor(const GameSnapshot& snapshot, const PlayPrompt* yourPrompt);

// The panel's size in its own units for what it says: section 2.10's width, and
// the height its lines stack to.
QSizeF sizeFor(const Fields& fields);

} // namespace trickpanel

class TrickPanelItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // `panels` is the corner panels' chrome cache, owned by TableScene. Null gives
    // the item one of its own.
    explicit TrickPanelItem(panel::Cache* panels, QGraphicsItem* parent = nullptr);

    // The panel's first source: every snapshot, from TableScene. HELD, NOT COPIED:
    // it runs on all ~1300 frames of a game, and is read again only when the
    // second source changes (PR 34's review, finding #7).
    void setSnapshot(std::shared_ptr<const GameSnapshot> snapshot, const Theme& theme);

    // ITS SECOND SOURCE, and the same split as the hand's: MainWindow hands over
    // the prompt the engine is waiting on for your card, and takes it back once
    // you have played or the game has moved on. A snapshot does not clear it - a
    // theme change redraws the table while the engine is still waiting.
    void setPrompt(const PlayPrompt& prompt);
    void clearPrompt();

    // What the panel says: READ-ONLY and for tests.
    const trickpanel::Fields& fields() const { return showing; }

    // The panel's size in its own units, its top-left at the item's origin. The
    // scene draws it at tablespace::hudScale.
    QSizeF size() const { return panelSize; }

    QRectF boundingRect() const override;

    // The panel as drawn, which is what takes the mouse.
    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // How many times the panel has been composed: READ-ONLY and for tests.
    std::size_t compositions() const { return memo.compositions(); }

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // Works out what the panel says from its two sources, and redraws it only if
    // that changed.
    void refresh();

    const panel::Picture& picture(qreal ratio) const;

    mutable panel::Cache ownPanels;
    panel::Cache* panels;

    Theme theme = Theme::dark();
    std::shared_ptr<const GameSnapshot> snapshot;
    std::optional<PlayPrompt> prompt;

    trickpanel::Fields showing;
    QSizeF panelSize;

    mutable PictureMemo memo;
};

#endif
