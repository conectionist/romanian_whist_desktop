#include "SeatItem.h"

#include "paint/CardRenderer.h"
#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"
#include "table/CardItem.h"

#include <QBrush>
#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTransform>

#include <algorithm>

namespace
{

// page.html's .plaque, in table units: a border of 1, padding of 8 at the top,
// bottom and left and 16 at the right, and a gap of 12 between the portrait and the
// text. The panel's height is the portrait's plus those, which is how 118 comes
// about.
constexpr qreal borderWidth = 1.0;
constexpr qreal paddingLeft = 8.0;
constexpr qreal paddingRight = 16.0;
constexpr qreal paddingY = 8.0;
constexpr qreal gap = 12.0;

// .portrait: 88 x 100, border-box, rounded 7, with a 1-unit gold border - so the
// picture inside it is 86 x 98, rounded 6.
constexpr QSizeF portraitSize(88.0, 100.0);
constexpr qreal portraitRadius = 7.0;
constexpr qreal portraitBorder = 1.0;

// The active seat's "▸" sits 6 before the name.
constexpr qreal markerGap = 6.0;

// .stats: 5 under the name. .pill: padding 2 and 12, a 1-unit border, fully
// rounded.
constexpr qreal statsGap = 5.0;
constexpr qreal pillPadX = 12.0;
constexpr qreal pillPadY = 2.0;
constexpr qreal pillBorder = 1.0;

QFont nameFont()
{
    QFont font = fonts::heading();
    font.setPixelSize(seatitem::nameSize);
    return font;
}

QFont pillFont()
{
    QFont font = fonts::body();
    font.setPixelSize(seatitem::pillSize);
    return font;
}

// CSS's line-height: normal, which is the font's own line spacing.
qreal lineHeight(const QFontMetricsF& metrics)
{
    return metrics.lineSpacing();
}

}

qreal seatitem::faceHeight()
{
    return portraitSize.height();
}

QRectF seatitem::panelRect(QSizeF panel)
{
    if(panel.isEmpty())
        return QRectF();

    return QRectF(-panel.width() / 2.0, -panel.height() / 2.0, panel.width(), panel.height());
}

seatitem::Layout seatitem::layoutFor(QSizeF panel, QSizeF faceSize)
{
    Layout layout;

    if(panel.isEmpty())
        return layout;

    layout.panel = panelRect(panel);

    const QRectF content = layout.panel.adjusted(borderWidth + paddingLeft, borderWidth + paddingY,
                                                 -borderWidth - paddingRight, -borderWidth - paddingY);

    // align-items: center, though the portrait fills the content box's height and
    // so lands at its top.
    layout.portrait = QRectF(QPointF(content.left(),
                                     content.center().y() - portraitSize.height() / 2.0),
                             portraitSize);

    layout.text = QRectF(layout.portrait.right() + gap, content.top(),
                         std::max(0.0, content.right() - layout.portrait.right() - gap),
                         content.height());

    // IN THE PORTRAIT'S PLACE, centred on it. The text column does not move for it,
    // so a Forehead round reflows nothing.
    if(!faceSize.isEmpty())
    {
        layout.card = QRectF(layout.portrait.center().x() - faceSize.width() / 2.0,
                             layout.portrait.center().y() - faceSize.height() / 2.0,
                             faceSize.width(), faceSize.height());
    }

    return layout;
}

seatitem::PillView seatitem::pillFor(const SeatView& seat, bool active, bool biddingOpen)
{
    // An explicit context and a literal at every call site, for BidStrip.h's reason:
    // a wrapper of our own would hide these from lupdate.
    if(!seat.hasBet)
        return { active && biddingOpen ? QCoreApplication::translate("SeatItem", "Bidding…")
                                       : QCoreApplication::translate("SeatItem", "No bid yet"),
                 Pill::Neutral };

    // TRICKS TAKEN FIRST, then the bid, as the label says.
    PillView view{ QCoreApplication::translate("SeatItem", "Taken/Bid: %1/%2")
                       .arg(seat.tricksWon)
                       .arg(seat.bet),
                   Pill::Neutral };

    if(biddingOpen)
        return view;

    if(seat.tricksWon == seat.bet)
        view.state = Pill::OnTarget;
    else
        view.state = seat.tricksWon < seat.bet ? Pill::Owing : Pill::Over;

    return view;
}

seatitem::NameLine seatitem::nameLineFor(const SeatView& seat, bool active, qreal width)
{
    const QFontMetricsF metrics(nameFont());

    NameLine line;
    line.marker = active ? QStringLiteral("▸") : QString();
    line.score = QStringLiteral(" (%1)").arg(seat.totalScore);

    const qreal markerWidth = line.marker.isEmpty() ? 0.0
                                                    : metrics.horizontalAdvance(line.marker) + markerGap;

    const qreal room = std::max(0.0, width - markerWidth - metrics.horizontalAdvance(line.score));

    // A name is arbitrary text from a setup dialog, so it is elided rather than
    // allowed to run past the panel.
    line.name = metrics.elidedText(QString::fromStdString(seat.name), Qt::ElideRight, room);

    return line;
}

SeatItem::SeatItem(const CardRenderer* _renderer, panel::Cache* _panels, QGraphicsItem* parent)
    : QGraphicsObject(parent), renderer(_renderer), panels(_panels ? _panels : &ownPanels)
{
    face = new CardItem(renderer, this);
    face->hide();

    // YOUR PANEL COVERS THE BOTTOM OF YOUR HAND (section 2.8), and Qt sends hover
    // only to the topmost item that asks for it. Without this the hand is that
    // item, and lifts a card the panel is hiding. See mousePressEvent() for the
    // other half.
    setAcceptHoverEvents(true);
}

seatitem::Shown SeatItem::shownNow() const
{
    // The text column is the same with or without a card, so the width is asked
    // without one - and without the renderer.
    const qreal width = seatitem::layoutFor(size, QSizeF()).text.width();

    return { seatitem::nameLineFor(seat, active, width), seatitem::pillFor(seat, active, biddingOpen),
             showsFace() };
}

// COMPARED BY WHAT IT DRAWS, not by the snapshot's fields: the cards left, the
// round score and the streaks change on most snapshots and are not on the panel,
// so a snapshot that changes none of what is costs no composing.
void SeatItem::setSeat(const SeatView& view, bool isActive, bool isBiddingOpen, const Theme& palette)
{
    seat = view;
    active = isActive;
    biddingOpen = isBiddingOpen;

    const seatitem::Shown now = shownNow();

    // The active seat's chrome differs too, and `active` is in the marker.
    if(now != shown || theme.table != palette.table)
    {
        shown = now;
        stale = true;
    }

    theme = palette;
    update();

    refreshFace();
}

void SeatItem::setPanelSize(QSizeF panelSize)
{
    if(size == panelSize)
        return;

    prepareGeometryChange();
    size = panelSize;
    shown = shownNow();
    stale = true;
    update();

    refreshFace();
}

void SeatItem::setPortrait(unsigned int portrait)
{
    if(look == portrait)
        return;

    look = portrait;
    stale = true;
    update();
}

// FROM THE SIZE ALONE, not layoutFor(): the scene asks for both on every paint and
// every hit test, and neither depends on the portrait, the text or the card.
QRectF SeatItem::boundingRect() const
{
    if(size.isEmpty())
        return QRectF();

    return seatitem::panelRect(size).marginsAdded(panel::reach());
}

QPainterPath SeatItem::shape() const
{
    if(size.isEmpty())
        return QPainterPath();

    return panel::shape(seatitem::panelRect(size));
}

// TAKEN, AND NOTHING DONE WITH IT. QGraphicsItem's own handler ignores a press on
// an item that is neither movable nor selectable, and an ignored press goes on to
// the item beneath: your own panel let a click on your name through to the card
// under it, and played that card.
void SeatItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    event->accept();
}

bool SeatItem::showsFace() const
{
    // NOT "is this a Forehead round". The snapshot does not need to be asked:
    // refreshFromEngine() fills this hand only where canSeeHand(viewer, holder)
    // allows, so a non-empty hand IS the permission - and a spectator game fills
    // none at all, which is why a spectated Forehead round shows no faces.
    return !seat.hand.empty();
}

seatitem::Layout SeatItem::layoutFor() const
{
    if(!showsFace() || !renderer)
        return seatitem::layoutFor(size, QSizeF());

    return seatitem::layoutFor(size, renderer->sizeForHeight(seatitem::faceHeight()));
}

void SeatItem::refreshFace()
{
    const seatitem::Layout layout = layoutFor();

    // BOTH, and the hand is asked FIRST. The layout's card rect is derived from
    // showsFace(), so today the two cannot disagree - and the day they did, the
    // front() below would be a read off an empty vector rather than a panel
    // drawn slightly wrong. Guarding on the thing actually dereferenced is what
    // makes that a hidden card instead of undefined behaviour.
    if(seat.hand.empty() || layout.card.isEmpty())
    {
        face->hide();
        return;
    }

    // THE FIRST CARD AND ONLY THE FIRST. A Forehead round is a one-trick round,
    // so a visible hand is one card. Drawing a hand of several here would be
    // inventing a layout for a state the engine's own schedule cannot produce.
    face->setFace(seat.hand.front().card);
    face->setCardHeight(seatitem::faceHeight());

    // CardItem's origin is its centre, which is what these two lines are about.
    face->setPos(layout.card.center());
    face->show();
}

void SeatItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    if(size.isEmpty())
        return;

    painter->setRenderHint(QPainter::Antialiasing);

    // ONE ANSWER ABOUT WHERE THINGS GO, shared with refreshFace() so the card the
    // child is placed at and the portrait it replaces cannot disagree.
    const seatitem::Layout layout = layoutFor();

    // At the pixels it lands on - or, while a resize settles, at the scale it
    // began from, so the cached panel is drawn scaled rather than composed again.
    const panel::Picture& panel = picture(layout, devicepixels::effectiveRatio(*painter));

    if(panel.pixmap.isNull())
        return;

    // On a whole device pixel, as the picture's own pixels were laid out to be.
    painter->drawPixmap(devicepixels::snapped(*painter, layout.panel.topLeft() + panel.offset),
                        panel.pixmap);
}

const panel::Picture& SeatItem::picture(const seatitem::Layout& layout, qreal ratio) const
{
    const int key = devicepixels::ratioKey(ratio);

    if(!stale && composedRatio == key && !composed.pixmap.isNull())
        return composed;

    const panel::Picture frame = panels->picture(size, theme.table, active, ratio);

    // NO CHROME, NOTHING TO COMPOSE: a ratio of zero, from a degenerate transform.
    // The panel is left stale, so the next real paint composes rather than finding
    // a null picture marked as current.
    if(frame.pixmap.isNull())
    {
        static const panel::Picture none;
        return none;
    }

    composed.pixmap = QPixmap(frame.pixmap.size());
    composed.pixmap.setDevicePixelRatio(ratio);
    composed.pixmap.fill(Qt::transparent);
    composed.offset = frame.offset;

    QPainter painter(&composed.pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.drawPixmap(QPointF(0.0, 0.0), frame.pixmap);

    // The item's own units from here: the panel's top-left is where the chrome's
    // offset says it is in the picture.
    painter.translate(-frame.offset - layout.panel.topLeft());

    // THE CARD OR THE PORTRAIT, never both: the card is a child item drawn over
    // this one, in the portrait's place.
    if(layout.card.isEmpty())
        paintPortrait(painter, layout.portrait);

    paintText(painter, layout.text);
    painter.end();

    composedRatio = key;
    stale = false;
    composedCount++;

    return composed;
}

void SeatItem::paintPortrait(QPainter& painter, const QRectF& box) const
{
    const QRectF laidOut = box.adjusted(portraitBorder, portraitBorder, -portraitBorder, -portraitBorder);
    const qreal innerRadius = portraitRadius - portraitBorder;

    const qreal ratio = devicepixels::effectiveRatio(painter);
    QPixmap picture = theme.avatar(look, laidOut.size().toSize(), ratio);

    if(picture.isNull())
        return;

    // A BRUSH OVER A ROUNDED RECT, not a clipped blit: Qt's raster engine draws a
    // clip path aliased, and the corner would show it. The brush is mapped so one
    // of its pixels is one device pixel, its corner on a whole one, as the table's
    // textures are - so the picture is not resampled on its way to the screen.
    picture.setDevicePixelRatio(1.0);

    const QPointF corner = devicepixels::snapped(painter, laidOut.topLeft());

    QTransform placed = QTransform::fromTranslate(corner.x(), corner.y());
    placed.scale(1.0 / ratio, 1.0 / ratio);

    QBrush brush(picture);
    brush.setTransform(placed);

    // AND THE RECT IS THE PICTURE'S, not the layout's. The layout's starts and ends
    // a fraction of a pixel off the grid the picture was snapped to, and a brush
    // tiles: that fraction sampled the row past the picture's other edge, and drew
    // a hairline of its coat along the top and its background along the bottom.
    // The frame goes round the same rect, so the two cannot leave a gap - and the
    // portrait moves by under half a device pixel for it.
    const QRectF inner(corner, QSizeF(picture.size()) / ratio);
    const QRectF outer = inner.adjusted(-portraitBorder, -portraitBorder, portraitBorder, portraitBorder);

    painter.fillPath(paths::roundedBand(outer, portraitRadius, inner, innerRadius),
                     theme.table.portraitBorder);

    painter.fillPath(paths::rounded(inner, innerRadius), brush);
}

void SeatItem::paintText(QPainter& painter, const QRectF& column) const
{
    const Theme::TablePalette& palette = theme.table;

    const QFont name = nameFont();
    const QFont pill = pillFont();
    const QFontMetricsF nameMetrics(name);
    const QFontMetricsF pillMetrics(pill);

    const qreal nameHeight = lineHeight(nameMetrics);
    const qreal pillHeight = lineHeight(pillMetrics) + 2.0 * (pillPadY + pillBorder);

    // The two lines as one block, centred in the column as the plaque's flex row
    // centres it.
    const qreal top = column.center().y() - (nameHeight + statsGap + pillHeight) / 2.0;

    // THE NAME LINE: "▸ " in gold-bright when this seat is to move, the name in
    // cream, its score in muted - a shape cue beside the ring, never colour alone
    // (section 18).
    const seatitem::NameLine& line = shown.name;
    const qreal nameBaseline = fonts::baselineIn(nameMetrics, top, nameHeight);

    qreal x = column.left();

    painter.setFont(name);

    if(!line.marker.isEmpty())
    {
        painter.setPen(palette.goldBright);
        painter.drawText(QPointF(x, nameBaseline), line.marker);
        x += nameMetrics.horizontalAdvance(line.marker) + markerGap;
    }

    painter.setPen(palette.cream);
    painter.drawText(QPointF(x, nameBaseline), line.name);
    x += nameMetrics.horizontalAdvance(line.name);

    painter.setPen(palette.muted);
    painter.drawText(QPointF(x, nameBaseline), line.score);

    // THE PILL, as wide as its text and no wider than the column.
    const seatitem::PillView& view = shown.pill;

    QColor colour = palette.cream;
    QColor border = palette.pillBorder;

    switch(view.state)
    {
    case seatitem::Pill::Neutral:  break;
    case seatitem::Pill::OnTarget: colour = border = palette.good; break;
    case seatitem::Pill::Owing:    colour = border = palette.warn; break;
    case seatitem::Pill::Over:     colour = border = palette.bad; break;
    }

    const qreal frame = 2.0 * (pillPadX + pillBorder);
    const QString text = pillMetrics.elidedText(view.text, Qt::ElideRight,
                                                std::max(0.0, column.width() - frame));

    const QRectF box(column.left(), top + nameHeight + statsGap,
                     std::min(column.width(), pillMetrics.horizontalAdvance(text) + frame),
                     pillHeight);

    const qreal half = pillBorder / 2.0;

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(border, pillBorder));
    painter.drawRoundedRect(box.adjusted(half, half, -half, -half),
                            box.height() / 2.0 - half, box.height() / 2.0 - half);

    painter.setFont(pill);
    painter.setPen(colour);
    painter.drawText(QPointF(box.left() + pillBorder + pillPadX,
                             fonts::baselineIn(pillMetrics, box.top() + pillBorder + pillPadY,
                                        lineHeight(pillMetrics))),
                     text);
}
