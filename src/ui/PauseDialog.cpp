#include "PauseDialog.h"

#include <QBoxLayout>
#include <QFont>
#include <QLabel>
#include <QPushButton>

PauseDialog::PauseDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Paused"));
    setModal(true);

    QVBoxLayout* stack = new QVBoxLayout(this);

    QLabel* title = new QLabel(tr("Paused"), this);
    title->setAlignment(Qt::AlignCenter);

    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);

    stack->addWidget(title);
    stack->addSpacing(12);

    resume = new QPushButton(tr("Resume"), this);
    resume->setObjectName("resume");

    // DEFAULT, so Enter resumes. Together with Esc reaching reject() - which is
    // also a Resume - every accidental way out of this dialog puts the player
    // back in the game rather than ending it.
    resume->setDefault(true);

    mainMenu = new QPushButton(tr("Main menu"), this);
    mainMenu->setObjectName("mainMenu");

    for(QPushButton* button : { resume, mainMenu })
    {
        button->setMinimumWidth(200);
        button->setMinimumHeight(32);
        stack->addWidget(button);
    }

    // done() rather than accept()/reject() by name, so the two outcomes are the
    // enumerators the header declares and the call site reads as a choice
    // between them rather than as a dialog being agreed with.
    connect(resume, &QPushButton::clicked, this, [this]{ done(Resume); });
    connect(mainMenu, &QPushButton::clicked, this, [this]{ done(MainMenu); });
}
