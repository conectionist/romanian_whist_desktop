#include "LitButton.h"

#include <QFocusEvent>

LitButton::LitButton(const QString& text, QWidget* parent) : QPushButton(text, parent)
{
    // Repainted as the cursor comes and goes, which is all the lit look needs.
    setAttribute(Qt::WA_Hover);
}

bool LitButton::isLit() const
{
    return isEnabled() && (underMouse() || isDown() || (tabbedTo && hasFocus()));
}

void LitButton::focusInEvent(QFocusEvent* event)
{
    tabbedTo = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    update();

    QPushButton::focusInEvent(event);
}

void LitButton::focusOutEvent(QFocusEvent* event)
{
    tabbedTo = false;
    update();

    QPushButton::focusOutEvent(event);
}
