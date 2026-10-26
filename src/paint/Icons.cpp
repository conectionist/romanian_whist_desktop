#include "Icons.h"

#include <QByteArray>
#include <QPainter>
#include <QString>
#include <QSvgRenderer>

namespace
{

// page.html's markup for each, with the colour left as %1 and its opacity as %2:
// the page writes stroke="currentColor", which only CSS can resolve.
QString markupFor(icons::Icon icon)
{
    switch(icon)
    {
    case icons::Icon::RoundHistory:
        return QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
            "stroke='%1' stroke-opacity='%2' stroke-width='1.8'>"
            "<path d='M4 4h16v16H4z'/><path d='M4 9h16M4 14h16M10 4v16'/></svg>");

    case icons::Icon::LastTrick:
        return QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
            "stroke='%1' stroke-opacity='%2' stroke-width='1.8'>"
            "<rect x='3' y='6' width='10' height='14' rx='1.5'/>"
            "<path d='M9 6V4.5A1.5 1.5 0 0 1 10.5 3h8A1.5 1.5 0 0 1 20 4.5v11a1.5 1.5 0 0 1-1.5 1.5H13'/>"
            "</svg>");

    case icons::Icon::NewGame:
        return QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
            "stroke='%1' stroke-opacity='%2' stroke-width='1.8'>"
            "<circle cx='12' cy='12' r='9'/><path d='M12 7v10M7 12h10'/></svg>");

    case icons::Icon::Settings:
        return QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
            "stroke='%1' stroke-opacity='%2' stroke-width='1.8'>"
            "<circle cx='12' cy='12' r='3.2'/>"
            "<path d='M12 2.5v3M12 18.5v3M2.5 12h3M18.5 12h3M5.3 5.3l2.1 2.1M16.6 16.6l2.1 2.1"
            "M5.3 18.7l2.1-2.1M16.6 7.4l2.1-2.1'/></svg>");

    case icons::Icon::Rules:
        return QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
            "stroke='%1' stroke-opacity='%2' stroke-width='1.8'>"
            "<path d='M4 5c3-1.5 5.5-1.5 8 0v15c-2.5-1.5-5-1.5-8 0z'/>"
            "<path d='M20 5c-3-1.5-5.5-1.5-8 0v15c2.5-1.5 5-1.5 8 0z'/></svg>");

    case icons::Icon::Exit:
        return QStringLiteral(
            "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' "
            "stroke='%1' stroke-opacity='%2' stroke-width='1.8'>"
            "<path d='M14 4H5v16h9'/><path d='M10 12h10M16 8l4 4-4 4'/></svg>");
    }

    return QString();
}

}

void icons::paint(QPainter& painter, Icon icon, const QRectF& box, const QColor& colour)
{
    const QString markup = markupFor(icon);

    if(markup.isEmpty() || box.isEmpty())
        return;

    // Parsed on every call, which is only ever while a picture is composed: a
    // button's is composed once per state and ratio and blitted after that.
    QSvgRenderer renderer(markup.arg(colour.name(QColor::HexRgb))
                                .arg(colour.alphaF())
                                .toUtf8());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    renderer.render(&painter, box);
    painter.restore();
}
