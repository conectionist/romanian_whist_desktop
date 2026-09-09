#include "PauseOverlay.h"

#include <QBoxLayout>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

PauseOverlay::PauseOverlay(QWidget* parent) : QWidget(parent)
{
    panel = new QWidget(this);
    panel->setAutoFillBackground(true);

    QVBoxLayout* inside = new QVBoxLayout(panel);
    inside->setContentsMargins(40, 28, 40, 28);

    QLabel* title = new QLabel(tr("Paused"), panel);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("pauseTitle");

    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
    titleFont.setBold(true);
    title->setFont(titleFont);

    inside->addWidget(title);
    inside->addSpacing(18);

    resume = new QPushButton(tr("Resume"), panel);
    resume->setObjectName("resume");

    // DEFAULT, so Enter resumes - and together with Esc, every quick way out of
    // this panel puts the player back in the game rather than ending it.
    resume->setDefault(true);

    mainMenu = new QPushButton(tr("Main menu"), panel);
    mainMenu->setObjectName("mainMenu");
    mainMenu->setAutoDefault(false);

    for(QPushButton* button : { resume, mainMenu })
    {
        button->setMinimumWidth(200);
        button->setMinimumHeight(34);
        inside->addWidget(button);
    }

    QVBoxLayout* stack = new QVBoxLayout(this);
    stack->addStretch(1);

    QHBoxLayout* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(panel);
    row->addStretch(1);

    stack->addLayout(row);
    stack->addStretch(1);

    connect(resume, &QPushButton::clicked, this, &PauseOverlay::resumeRequested);
    connect(mainMenu, &QPushButton::clicked, this, &PauseOverlay::mainMenuRequested);
}

void PauseOverlay::setTheme(const Theme& _theme)
{
    theme = _theme;

    QPalette scheme = panel->palette();
    scheme.setColor(QPalette::Window, theme.seatPlaque);
    scheme.setColor(QPalette::WindowText, theme.textPrimary);
    panel->setPalette(scheme);

    update();
}

void PauseOverlay::focusResume()
{
    resume->setFocus(Qt::OtherFocusReason);
}

void PauseOverlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // Theme's own dim rather than a colour written here - section 18.1's rule,
    // and this member existed for exactly this and had no caller until now.
    painter.fillRect(rect(), theme.dimmedOverlay);

    QWidget::paintEvent(event);
}

void PauseOverlay::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}
