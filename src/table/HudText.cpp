#include "HudText.h"

textruns::Style hudtext::style(const QFont& font, qreal pixels, const QColor& colour,
                               const Theme::TablePalette& palette)
{
    textruns::Style style = textruns::plainStyle(font, pixels, colour);
    style.redSuit = palette.redSuit;
    style.blackSuit = palette.blackSuit;
    return style;
}

QString hudtext::dash()
{
    return QStringLiteral("–");
}
