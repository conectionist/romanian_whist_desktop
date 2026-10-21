#ifndef HUD_TEXT_H
#define HUD_TEXT_H

#include "paint/TextRuns.h"
#include "paint/Theme.h"

#include <QColor>
#include <QFont>
#include <QString>

// WHAT THE CORNER PANELS' TEXT HAS IN COMMON. The scoreboard (section 2.9) and the
// trick panel (section 2.10) each set a handful of styles, and both write a dash
// where there is nothing to say yet; each once spelled both out for itself.
namespace hudtext
{

// textruns::plainStyle() in the table's suit colours, so a suit glyph anywhere in
// a panel's line is painted in its suit's.
textruns::Style style(const QFont& font, qreal pixels, const QColor& colour,
                      const Theme::TablePalette& palette);

// A dash for a cell with nothing in it yet. Punctuation rather than a word, so not
// a translatable string; an en dash rather than a hyphen because a hyphen beside
// right-aligned digits reads as a minus.
QString dash();

} // namespace hudtext

#endif
