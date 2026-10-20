#include "TextRuns.h"

#include "paint/Fonts.h"

#include <QFontMetricsF>
#include <QPainter>

#include <utility>

namespace
{

bool isRedSuit(QChar c)
{
    return c == QChar(0x2665) || c == QChar(0x2666);
}

bool isBlackSuit(QChar c)
{
    return c == QChar(0x2660) || c == QChar(0x2663);
}

bool isSuit(QChar c)
{
    return isRedSuit(c) || isBlackSuit(c);
}

const QFont& fontOf(const textruns::Run& run, const textruns::Style& style)
{
    return run.emphasis == textruns::Emphasis::Strong ? style.strong : style.plain;
}

// EACH RUN IN PIECES, cut either side of every suit glyph, so a glyph can take its
// own colour. `visit` is handed each piece and the colour it draws in; width() and
// paint() both walk the line through here, so the width measured is the width
// drawn - the pieces' advances summed, not the whole string's, which kerning across
// a cut would make a hair different.
template<typename Visit>
void walk(const textruns::Run& run, const textruns::Style& style, Visit&& visit)
{
    const QColor base = run.emphasis == textruns::Emphasis::Strong ? style.strongColour
                                                                   : style.plainColour;

    qsizetype start = 0;

    for(qsizetype i = 0 ; i < run.text.size() ; i++)
    {
        const QChar c = run.text.at(i);

        if(!isSuit(c))
            continue;

        if(i > start)
            visit(run.text.mid(start, i - start), base);

        visit(QString(c), isRedSuit(c) ? style.redSuit : style.blackSuit);
        start = i + 1;
    }

    if(start < run.text.size())
        visit(run.text.mid(start), base);
}

// In the FONT's units, which are `style.scale` times the line's.
qreal runAdvance(const textruns::Run& run, const textruns::Style& style)
{
    const QFontMetricsF metrics(fontOf(run, style));
    qreal advance = 0.0;

    walk(run, style, [&](const QString& piece, const QColor&)
    {
        advance += metrics.horizontalAdvance(piece);
    });

    return advance;
}

// Appends `run` to `line`, merged into the last run when the two read alike, so a
// wrapped line is the few runs it was built from rather than a run per word.
void append(textruns::Line& line, textruns::Run run)
{
    if(!line.empty() && line.back().emphasis == run.emphasis)
        line.back().text += run.text;
    else
        line.push_back(std::move(run));
}

void trimEnd(textruns::Line& line)
{
    while(!line.empty())
    {
        QString& text = line.back().text;

        while(!text.isEmpty() && text.back() == QLatin1Char(' '))
            text.chop(1);

        if(!text.isEmpty())
            return;

        line.pop_back();
    }
}

}

QString textruns::text(const Line& line)
{
    QString whole;

    for(const Run& run : line)
        whole += run.text;

    return whole;
}

textruns::Style textruns::plainStyle(const QFont& font, qreal pixels, const QColor& colour)
{
    const fonts::Sized sized = fonts::sized(font, pixels);

    return { sized.font, sized.font, colour, colour, colour, colour, sized.scale };
}

textruns::Metrics textruns::metrics(const Style& style)
{
    const QFontMetricsF font(style.plain);

    return { font.ascent() / style.scale, font.descent() / style.scale,
             font.lineSpacing() / style.scale };
}

qreal textruns::baselineIn(const Style& style, qreal top, qreal height)
{
    const Metrics measured = metrics(style);

    return top + (height - (measured.ascent + measured.descent)) / 2.0 + measured.ascent;
}

textruns::Line textruns::emphasised(const QString& sentence, const QString& strong)
{
    const qsizetype at = sentence.indexOf(QStringLiteral("%1"));

    if(at < 0)
        return { Run{ sentence } };

    Line line;

    if(at > 0)
        line.push_back(Run{ sentence.left(at) });

    line.push_back(Run{ strong, Emphasis::Strong });

    if(at + 2 < sentence.size())
        line.push_back(Run{ sentence.mid(at + 2) });

    return line;
}

qreal textruns::width(const Line& line, const Style& style)
{
    qreal total = 0.0;

    for(const Run& run : line)
        total += runAdvance(run, style);

    return total / style.scale;
}

void textruns::paint(QPainter& painter, QPointF origin, const Line& line, const Style& style)
{
    painter.save();

    // In the fonts' own units from here, scaled down to the line's.
    painter.translate(origin);
    painter.scale(1.0 / style.scale, 1.0 / style.scale);

    qreal x = 0.0;

    for(const Run& run : line)
    {
        const QFont& font = fontOf(run, style);
        const QFontMetricsF metrics(font);

        painter.setFont(font);

        walk(run, style, [&](const QString& piece, const QColor& colour)
        {
            painter.setPen(colour);
            painter.drawText(QPointF(x, 0.0), piece);
            x += metrics.horizontalAdvance(piece);
        });
    }

    painter.restore();
}

std::vector<textruns::Line> textruns::wrap(const Line& line, const Style& style, qreal width)
{
    // THE LINE AS WORDS, each with the spaces after it, keeping its run's emphasis.
    // A word can straddle two runs - "♥." is the glyph's run and then a full stop -
    // so a word ends only at a space, wherever the runs change.
    std::vector<Line> words;
    Line word;
    bool inSpaces = false;

    for(const Run& run : line)
    {
        for(const QChar c : run.text)
        {
            const bool space = c == QLatin1Char(' ');

            if(!space && inSpaces)
            {
                words.push_back(std::move(word));
                word.clear();
            }

            inSpaces = space;
            append(word, Run{ QString(c), run.emphasis });
        }
    }

    if(!word.empty())
        words.push_back(std::move(word));

    std::vector<Line> lines;
    Line current;

    for(Line& next : words)
    {
        // FITTED WITHOUT ITS TRAILING SPACES, which hang past the edge in CSS too.
        Line fitted = current;

        for(const Run& run : next)
            append(fitted, run);

        Line trimmed = fitted;
        trimEnd(trimmed);

        if(!current.empty() && textruns::width(trimmed, style) > width)
        {
            trimEnd(current);
            lines.push_back(std::move(current));
            current = std::move(next);
            continue;
        }

        current = std::move(fitted);
    }

    trimEnd(current);

    if(!current.empty())
        lines.push_back(std::move(current));

    return lines;
}
