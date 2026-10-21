#include "PauseOverlay.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPushButton>

PauseOverlay::PauseOverlay(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    QLabel* title = new QLabel(tr("Paused"), panel);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("pauseTitle");
    styleTitle(title, 1.6);

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

    connect(resume, &QPushButton::clicked, this, &PauseOverlay::resumeRequested);
    connect(mainMenu, &QPushButton::clicked, this, &PauseOverlay::mainMenuRequested);
}

void PauseOverlay::focusDefault()
{
    resume->setFocus(Qt::OtherFocusReason);
}
