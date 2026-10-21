#include "Overlay.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>

Overlay::Overlay(QWidget* parent) : QWidget(parent)
{
    plaque = new QWidget(this);
    plaque->setAutoFillBackground(true);

    column = new QVBoxLayout(plaque);
    column->setContentsMargins(40, 28, 40, 28);

    // The panel centred both ways, and no bigger than its contents.
    QVBoxLayout* stack = new QVBoxLayout(this);
    stack->addStretch(1);

    QHBoxLayout* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(plaque);
    row->addStretch(1);

    stack->addLayout(row);
    stack->addStretch(1);

    // Hidden or shown, so that it always opens at the page's size.
    if(parent)
    {
        setGeometry(parent->rect());
        parent->installEventFilter(this);
    }
}

void Overlay::setTheme(const Theme& theme)
{
    palette = theme;

    // Through the palette rather than a stylesheet: section 18.1's rule is that
    // colours come from Theme, and a stylesheet string is a colour constructed
    // somewhere else.
    QPalette scheme = plaque->palette();
    scheme.setColor(QPalette::Window, palette.seatPlaque);
    scheme.setColor(QPalette::WindowText, palette.textPrimary);
    plaque->setPalette(scheme);

    update();
}

void Overlay::styleTitle(QLabel* title, qreal scale)
{
    QFont font = title->font();
    font.setPointSizeF(font.pointSizeF() * scale);
    font.setBold(true);
    title->setFont(font);
}

void Overlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette.dimmedOverlay);

    QWidget::paintEvent(event);
}

void Overlay::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

bool Overlay::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());

    return QWidget::eventFilter(watched, event);
}
