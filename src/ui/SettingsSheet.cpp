#include "SettingsSheet.h"

#include "ui/NewGameDialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QCoreApplication>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

SettingsSheet::SettingsSheet(QWidget* parent) : Overlay(parent)
{
    QWidget* panel = this->panel();
    QVBoxLayout* inside = panelLayout();

    QLabel* title = new QLabel(tr("Settings"), panel);
    title->setObjectName(QStringLiteral("settingsTitle"));
    dress(title, sheet::Role::Title);

    inside->addWidget(title);

    QLabel* meta = new QLabel(tr("These apply now, and to your next game."), panel);
    dress(meta, sheet::Role::Meta);

    inside->addSpacing(2);
    inside->addWidget(meta);
    inside->addSpacing(18);

    // THE SETUP DIALOG'S SLIDER, built by the same function, so a player who has
    // set it in one place recognises it in the other - its words set here as the
    // sheet's: "Bot speed" as the mock-up's .label, its two ends as notes.
    const newgame::SpeedRow speedRow = newgame::buildSpeedRow(panel, 12);
    speed = speedRow.slider;
    speed->setObjectName(QStringLiteral("settingsSpeed"));
    speed->setMinimumWidth(240);

    bool first = true;

    for(int i = 0 ; i < speedRow.row->count() ; i++)
    {
        if(QLabel* words = qobject_cast<QLabel*>(speedRow.row->itemAt(i)->widget()))
        {
            dress(words, first ? sheet::Role::Label : sheet::Role::Note);
            words->setWordWrap(false);
            first = false;
        }
    }

    // Its own spacing: a row in the sheet's column takes the column's, which is
    // none.
    speedRow.row->setSpacing(8);

    inside->addLayout(speedRow.row);
    inside->addSpacing(14);

    // The round sheet's wait, which a player may want to turn on mid-game to read
    // a round, or off once they no longer need to. It takes effect when the next
    // round is scored; a sheet already up keeps the wait it opened with.
    waitEachRound = newgame::buildWaitCheckBox(panel);
    waitEachRound->setObjectName(QStringLiteral("settingsWait"));
    waitEachRound->setFont(sheet::fontFor(sheet::Role::Body));
    inside->addWidget(waitEachRound);

    done = new SheetButton(tr("Done"), SheetButton::Kind::Primary, panel);
    done->setObjectName(QStringLiteral("settingsDone"));
    done->setDefault(true);

    addActions({ done });

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
