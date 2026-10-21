#include "SettingsSheet.h"

#include "ui/NewGameDialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>

SettingsSheet::SettingsSheet(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    QLabel* title = new QLabel(tr("Settings"), panel);
    title->setObjectName(QStringLiteral("settingsTitle"));
    styleTitle(title, 1.6);

    inside->addWidget(title);

    QLabel* meta = new QLabel(tr("These apply now, and to your next game."), panel);
    inside->addWidget(meta);
    inside->addSpacing(18);

    // THE SETUP DIALOG'S SLIDER, built by the same function, so a player who has
    // set it in one place recognises it in the other.
    const newgame::SpeedRow speedRow = newgame::buildSpeedRow(panel, 12);
    speed = speedRow.slider;
    speed->setObjectName(QStringLiteral("settingsSpeed"));
    speed->setMinimumWidth(240);
    inside->addLayout(speedRow.row);

    inside->addSpacing(10);

    // The round sheet's wait, which a player may want to turn on mid-game to read
    // a round, or off once they no longer need to. It takes effect when the next
    // round is scored; a sheet already up keeps the wait it opened with.
    waitEachRound = newgame::buildWaitCheckBox(panel);
    waitEachRound->setObjectName(QStringLiteral("settingsWait"));
    inside->addWidget(waitEachRound);

    inside->addSpacing(18);

    done = new QPushButton(tr("Done"), panel);
    done->setObjectName(QStringLiteral("settingsDone"));
    done->setDefault(true);
    done->setMinimumWidth(140);
    done->setMinimumHeight(34);

    QHBoxLayout* actions = new QHBoxLayout;
    actions->addStretch(1);
    actions->addWidget(done);
    inside->addLayout(actions);

    connect(speed, &QSlider::valueChanged, this, &SettingsSheet::announce);
    connect(waitEachRound, &QCheckBox::toggled, this, &SettingsSheet::announce);
    connect(done, &QPushButton::clicked, this, &SettingsSheet::closeRequested);
}

void SettingsSheet::setValues(int botSpeed, bool waitAfterEachRound)
{
    // Filling the sheet is not changing a setting.
    const QSignalBlocker quietSpeed(speed);
    const QSignalBlocker quietWait(waitEachRound);

    speed->setValue(botSpeed);
    waitEachRound->setChecked(waitAfterEachRound);
}

void SettingsSheet::focusDefault()
{
    done->setFocus(Qt::OtherFocusReason);
}

void SettingsSheet::announce()
{
    emit settingsChanged(speed->value(), waitEachRound->isChecked());
}
