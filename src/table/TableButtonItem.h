#ifndef TABLE_BUTTON_ITEM_H
#define TABLE_BUTTON_ITEM_H

#include "paint/Icons.h"
#include "paint/Panel.h"
#include "paint/PictureMemo.h"
#include "paint/Theme.h"

#include <QGraphicsObject>
#include <QSizeF>
#include <QString>

#include <cstddef>

// THE TWO BUTTONS IN THE WINDOW'S BOTTOM-LEFT CORNER (the table redesign's
// section 2.11): Round history and Last trick. page.html's .panel.btn - the
// panel's chrome, a gold line icon and a label - at the table's own scale, pinned
// by TableScene::pinHud().
//
// IN THE SCENE, NOT WIDGETS OVER IT, as every corner panel is: the owner's call
// of 2026-10-20 (phase 8). Drawn through the scene they scale with the table, as
// the mock-up's do, and are one cached picture each - the panel's two blurred
// shadows are too dear to paint on every frame of a drag.
//
// WHAT THEY OPEN IS PHASE 11's. Until then the scene builds them disabled: drawn
// in their place, muted, taking neither the cursor nor a click.
namespace tablebutton
{

// .panel.btn: 76 tall, its border included; the label in Marcellus 21.
inline constexpr qreal height = 76.0;
inline constexpr int labelSize = 21;

// How big a button with this label is, in table units: its border, 22 of padding
// either side, the 24-unit icon, 12 between the icon and the label, and the label.
QSizeF sizeFor(const QString& label);

} // namespace tablebutton

class TableButtonItem : public QGraphicsObject
{
    Q_OBJECT

public:
    // `panels` is the chrome cache the button shares with the seats, which are
    // drawn at the same scale, owned by TableScene. Null gives the button one of
    // its own.
    TableButtonItem(icons::Icon icon, const QString& label, panel::Cache* panels,
                    QGraphicsItem* parent = nullptr);

    void setTheme(const Theme& theme);

    // The button's own rect, from its top-left at the item's origin: where it is
    // pinned from.
    QSizeF size() const { return buttonSize; }

    const QString& label() const { return text; }

    // Whether the cursor is over it. Never, while it is disabled.
    bool isHovered() const { return hovered; }

    // The button plus its drop shadow and glow.
    QRectF boundingRect() const override;

    // The panel as drawn, which is what takes the mouse.
    QPainterPath shape() const override;

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // How many times the button has been composed: READ-ONLY and for tests, as
    // SeatItem::compositions() is.
    std::size_t compositions() const { return memo.compositions(); }

signals:
    // Pressed and released on the button, while it is enabled.
    void clicked();

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

    // Enabling or disabling it changes what it takes and how it is drawn.
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    void setHovered(bool now);

    const panel::Picture& picture(qreal ratio) const;

    icons::Icon icon;
    QString text;
    QSizeF buttonSize;

    mutable panel::Cache ownPanels;
    panel::Cache* panels;

    Theme theme = Theme::dark();
    bool hovered = false;

    mutable PictureMemo memo;
};

#endif
