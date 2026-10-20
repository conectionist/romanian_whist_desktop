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
};

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
