#ifndef TEXT_RUNS_H
#define TEXT_RUNS_H

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QString>

#include <vector>

class QPainter;

// A LINE OF TEXT IN MORE THAN ONE VOICE, as the mock-up's panels write them: the
// scoreboard's "Bids so far: 3 of 6 tricks" with the 3 in bold cream, the trick
// panel's "Leader: Bogdan · Led ♥" with the name in cream, and every suit glyph in
// its own colour (page.html's .red and .blk) whatever run it is in.
//
// THE WORDS AND THE LOOK ARE KEPT APART. What a line says, and which part of it
// stands out, is decided by the panel's free functions and tested without a font
// or a palette; how each part looks is a Style the panel hands to paint(). The
// table redesign's sheets (phase 10) set their text the same way.
namespace textruns
{

// How a run reads against the rest of its line. The panel says what that looks
// like: bold cream on the scoreboard, cream at weight 500 on the trick panel.
enum class Emphasis
{
    Plain,
    Strong
};

struct Run
{
    QString text;
    Emphasis emphasis = Emphasis::Plain;

    bool operator==(const Run&) const = default;
};

using Line = std::vector<Run>;

// The line as one string: what a test reads, and what it says to anyone who
// cannot see the emphasis.
QString text(const Line& line);

// How a panel sets its runs.
struct Style
{
    QFont plain;
    QFont strong;
    QColor plainColour;
    QColor strongColour;

    // ♥ and ♦ in red, ♠ and ♣ in cream - black on a dark panel would vanish -
    // inside a run of any colour.
    QColor redSuit;
    QColor blackSuit;

    // HOW MANY TIMES THE SIZE THEY ARE DRAWN AT the two fonts are set: 1, unless
    // the size is fractional - see fonts::sized(), whose `scale` this is.
    qreal scale = 1.0;
};

// A style of one colour and one font, at the pixel size `pixels`, which may be
// fractional: what a line with no emphasis and no suit glyph needs.
Style plainStyle(const QFont& font, qreal pixels, const QColor& colour);

// The plain font's measurements, at the size the line is drawn.
struct Metrics
{
    qreal ascent = 0.0;
    qreal descent = 0.0;

    // CSS's line-height: normal.
    qreal lineSpacing = 0.0;
};

Metrics metrics(const Style& style);

// Where the line's baseline goes in a line box of `height` at `top`: CSS's
// half-leading, fonts::baselineIn()'s rule.
qreal baselineIn(const Style& style, qreal top, qreal height);

// How wide paint() draws the line.
qreal width(const Line& line, const Style& style);

// Paints the line with its baseline starting at `origin`.
void paint(QPainter& painter, QPointF origin, const Line& line, const Style& style);

// The line broken at spaces into lines no wider than `width`. A word is never
// split: one wider than the width stands on a line of its own. The spaces a line
// breaks at are dropped, as a browser drops them.
std::vector<Line> wrap(const Line& line, const Style& style, qreal width);

} // namespace textruns

#endif
