#include "PauseOverlay.h"

#include <QBoxLayout>
#include <QLabel>

PauseOverlay::PauseOverlay(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    QLabel* title = new QLabel(tr("Paused"), panel);
    title->setObjectName("pauseTitle");
    dress(title, sheet::Role::Title);

    inside->addWidget(title);

    // A WAY BACK INTO THE GAME AND A WAY OUT OF IT, at the bottom right where the
    // mock-up's sheets keep their actions - the way back the green one, on the
    // right, where the eye ends.
    mainMenu = new SheetButton(tr("Main menu"), SheetButton::Kind::Quiet, panel);
    mainMenu->setObjectName("mainMenu");
    mainMenu->setAutoDefault(false);

    resume = new SheetButton(tr("Resume"), SheetButton::Kind::Primary, panel);
    resume->setObjectName("resume");

    // DEFAULT, so Enter resumes - and together with Esc, every quick way out of
    // this panel puts the player back in the game rather than ending it.
    resume->setDefault(true);

    addActions({ mainMenu, resume });

    connect(resume, &QPushButton::clicked, this, &PauseOverlay::resumeRequested);
    connect(mainMenu, &QPushButton::clicked, this, &PauseOverlay::mainMenuRequested);
}

void PauseOverlay::focusDefault()
{
    resume->setFocus(Qt::OtherFocusReason);
}
