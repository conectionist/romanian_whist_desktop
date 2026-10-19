#include "Panel.h"

#include "paint/DevicePixels.h"
#include "paint/Paths.h"
#include "paint/Shadows.h"

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{

// page.html's .panel and .plaque.active, in the order the rules write them.
constexpr qreal borderWidth = 1.0;
constexpr qreal insetDark = 3.0;
constexpr qreal insetLine = 4.0;
constexpr QPointF shadowOffset(0.0, 12.0);
constexpr qreal shadowBlur = 26.0;
constexpr qreal ringWidth = 3.0;
constexpr qreal glowBlur = 26.0;

}

QMarginsF panel::reach()
{
    // The glow reaches as far as the shadow does, and further than the ring; the
    // shadow alone reaches its offset further down.
    const qreal spread = std::max(shadows::reach(shadowBlur), shadows::reach(glowBlur));

    return QMarginsF(spread, spread, spread, spread + shadowOffset.y());
}

QPainterPath panel::shape(const QRectF& rect)
{
    return paths::rounded(rect, radius);
}

void panel::paint(QPainter& painter, const QRectF& rect, const Theme::TablePalette& palette,
                  bool active)
{
    const QPainterPath outline = shape(rect);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // The outer shadows, the LAST LISTED LOWEST: page.html lists the ring, the
    // glow, then the drop shadow, so they are painted the other way round.
    shadows::drawShapeShadow(painter, outline, shadowOffset, shadowBlur, palette.panelShadow);

    if(active)
    {
        shadows::drawShapeShadow(painter, outline, QPointF(), glowBlur, palette.activeGlow);

        // A SPREAD SHADOW, 0 0 0 3px: the border box grown by 3, its corners
        // grown with it, and outside the box only - so a band.
        painter.fillPath(paths::roundedBand(rect.adjusted(-ringWidth, -ringWidth,
                                                          ringWidth, ringWidth),
                                            radius + ringWidth, rect, radius),
                         palette.goldBright);
    }

    // The background, top to bottom over the padding box, painted under the
    // border as CSS paints one.
    const QRectF padding = rect.adjusted(borderWidth, borderWidth, -borderWidth, -borderWidth);
    const qreal paddingRadius = radius - borderWidth;

    QLinearGradient background(padding.topLeft(), padding.bottomLeft());
    background.setColorAt(0.0, palette.panelTop);
    background.setColorAt(1.0, palette.panelBottom);
    painter.fillPath(outline, background);

    // The inset rings, the last listed lowest again: 4 units of faint gold under 3
    // of near-black, which leaves one fine gold line 3 units inside the border.
    painter.fillPath(paths::roundedBand(padding, paddingRadius,
                                        padding.adjusted(insetLine, insetLine,
                                                         -insetLine, -insetLine),
                                        paddingRadius - insetLine),
                     palette.panelInnerLine);

    painter.fillPath(paths::roundedBand(padding, paddingRadius,
                                        padding.adjusted(insetDark, insetDark,
                                                         -insetDark, -insetDark),
                                        paddingRadius - insetDark),
                     palette.panelInnerShadow);

    painter.fillPath(paths::roundedBand(rect, radius, padding, paddingRadius), palette.panelBorder);

    painter.restore();
}

panel::Picture panel::render(QSizeF size, const Theme::TablePalette& palette, bool active,
                             qreal ratio)
{
    if(size.isEmpty() || ratio <= 0.0)
        return {};

    // The reach in WHOLE PIXELS at this ratio, so the panel starts on a pixel of
    // the picture rather than between two.
    const QMarginsF margins = reach();
    const auto whole = [ratio](qreal units) { return std::ceil(units * ratio) / ratio; };

    const QMarginsF snapped(whole(margins.left()), whole(margins.top()),
                            whole(margins.right()), whole(margins.bottom()));

    const QSizeF logical(size.width() + snapped.left() + snapped.right(),
                         size.height() + snapped.top() + snapped.bottom());

    QPixmap pixmap(QSize(static_cast<int>(std::ceil(logical.width() * ratio)),
                         static_cast<int>(std::ceil(logical.height() * ratio))));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    paint(painter, QRectF(QPointF(snapped.left(), snapped.top()), size), palette, active);
    painter.end();

    return { pixmap, QPointF(-snapped.left(), -snapped.top()) };
}

panel::Picture panel::Cache::picture(QSizeF size, const Theme::TablePalette& wanted, bool active,
                                     qreal ratio)
{
    if(size.isEmpty() || ratio <= 0.0)
        return {};

    return pictures.picture({ size, active }, wanted, ratio,
                            [&] { return render(size, wanted, active, ratio); });
}
