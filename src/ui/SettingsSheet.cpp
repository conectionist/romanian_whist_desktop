#include "SettingsSheet.h"

#include "ui/NewGameDialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>

#include <algorithm>
#include <vector>

namespace
{
std::vector<QLabel*> labelsOf(const QHBoxLayout* row)
{
    std::vector<QLabel*> labels;

    for(int i = 0 ; i < row->count() ; i++)
    {
        if(QLabel* words = qobject_cast<QLabel*>(row->itemAt(i)->widget()))
            labels.push_back(words);
    }

    return labels;
}
}

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
    const newgame::SliderRow speedRow = newgame::buildSpeedRow(panel, 12);
    speed = speedRow.slider;
    speed->setObjectName(QStringLiteral("settingsSpeed"));
    speed->setMinimumWidth(240);

    // THE ANIMATIONS' ROW, in the same shape and under it (phase 12): "Card
    // animations", Off, the slider, Slow, with the four stops of motion::Pace. Only
    // the two ends are named, as the speed row's are; the stops between are where
    // the default sits, at the third.
    const newgame::SliderRow animationRow = newgame::buildSliderRow(panel, tr("Card animations"), tr("Off"),
                                                                    tr("Slow"), 0, motion::stopCount - 1, 12);
    animations = animationRow.slider;
    animations->setObjectName(QStringLiteral("settingsAnimations"));
    animations->setMinimumWidth(240);

    for(const QHBoxLayout* row : { speedRow.row, animationRow.row })
    {
        bool first = true;

        for(QLabel* words : labelsOf(row))
        {
            dress(words, first ? sheet::Role::Label : sheet::Role::Note);
            words->setWordWrap(false);
            first = false;
        }
    }

    // THE TWO SLIDERS START AND END TOGETHER: each column of words as wide as the
    // wider of its two, since "Bot speed" and "Card animations" (and "Off" and
    // "Instant") are not the same length. Measured after the words are dressed.
    const std::vector<QLabel*> speedWords = labelsOf(speedRow.row);
    const std::vector<QLabel*> animationWords = labelsOf(animationRow.row);

    for(std::size_t column = 0 ; column < std::min(speedWords.size(), animationWords.size()) ; column++)
    {
        const int width = std::max(speedWords[column]->sizeHint().width(),
                                   animationWords[column]->sizeHint().width());

        speedWords[column]->setMinimumWidth(width);
        animationWords[column]->setMinimumWidth(width);
    }

    // Their own spacing: a row in the sheet's column takes the column's, which is
    // none.
    speedRow.row->setSpacing(8);
    animationRow.row->setSpacing(8);

    inside->addLayout(speedRow.row);
    inside->addSpacing(10);
    inside->addLayout(animationRow.row);
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
    connect(animations, &QSlider::valueChanged, this, &SettingsSheet::announce);
    connect(waitEachRound, &QCheckBox::toggled, this, &SettingsSheet::announce);
    connect(done, &QPushButton::clicked, this, &SettingsSheet::closeRequested);
}

void SettingsSheet::setValues(int botSpeed, bool waitAfterEachRound, motion::Pace pace)
{
    // Filling the sheet is not changing a setting.
    const QSignalBlocker quietSpeed(speed);
    const QSignalBlocker quietAnimations(animations);
    const QSignalBlocker quietWait(waitEachRound);

    speed->setValue(botSpeed);
    animations->setValue(motion::stopOf(pace));
    waitEachRound->setChecked(waitAfterEachRound);
}

void SettingsSheet::focusDefault()
{
    done->setFocus(Qt::OtherFocusReason);
}

void SettingsSheet::announce()
{
    emit settingsChanged(speed->value(), waitEachRound->isChecked(), motion::paceAt(animations->value()));
}
