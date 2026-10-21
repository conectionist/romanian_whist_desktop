#include "SettingsSheet.h"

#include "ui/NewGameDialog.h"

#include <QBoxLayout>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFont>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>

SettingsSheet::SettingsSheet(QWidget* parent) : QWidget(parent)
{
    panel = new QWidget(this);
    panel->setAutoFillBackground(true);

    QVBoxLayout* inside = new QVBoxLayout(panel);
    inside->setContentsMargins(40, 28, 40, 28);

    QLabel* title = new QLabel(tr("Settings"), panel);
    title->setObjectName(QStringLiteral("settingsTitle"));

    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
    titleFont.setBold(true);
    title->setFont(titleFont);

    inside->addWidget(title);

    QLabel* meta = new QLabel(tr("These apply now, and to your next game."), panel);
    inside->addWidget(meta);
    inside->addSpacing(18);

    // THE SETUP DIALOG'S SLIDER, the same five positions under the same words, so
    // a player who has set it in one place recognises it in the other - and the
    // same newgame::pacingFor() turns it into delays.
    speed = new QSlider(Qt::Horizontal, panel);
    speed->setObjectName(QStringLiteral("settingsSpeed"));
    speed->setRange(newgame::SlowestSpeed, newgame::FastestSpeed);
    speed->setPageStep(1);
    speed->setTickPosition(QSlider::TicksBelow);
    speed->setMinimumWidth(240);

    QHBoxLayout* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(tr("Bot speed"), panel));
    speedRow->addSpacing(12);
    speedRow->addWidget(new QLabel(tr("Slow"), panel));
    speedRow->addWidget(speed, 1);
    speedRow->addWidget(new QLabel(tr("Instant"), panel));
    inside->addLayout(speedRow);

    inside->addSpacing(10);

    // The round sheet's wait, which a player may want to turn on mid-game to read
    // a round, or off once they no longer need to. It takes effect when the next
    // round is scored; a sheet already up keeps the wait it opened with.
    waitEachRound = new QCheckBox(tr("Wait for me after each round"), panel);
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

    QVBoxLayout* stack = new QVBoxLayout(this);
    stack->addStretch(1);

    QHBoxLayout* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(panel);
    row->addStretch(1);

    stack->addLayout(row);
    stack->addStretch(1);

    connect(speed, &QSlider::valueChanged, this, &SettingsSheet::announce);
    connect(waitEachRound, &QCheckBox::toggled, this, &SettingsSheet::announce);
    connect(done, &QPushButton::clicked, this, &SettingsSheet::closeRequested);
}

void SettingsSheet::setTheme(const Theme& _theme)
{
    theme = _theme;

    QPalette scheme = panel->palette();
    scheme.setColor(QPalette::Window, theme.seatPlaque);
    scheme.setColor(QPalette::WindowText, theme.textPrimary);
    panel->setPalette(scheme);

    update();
}

void SettingsSheet::setValues(int botSpeed, bool waitAfterEachRound)
{
    // Filling the sheet is not changing a setting.
    const QSignalBlocker quietSpeed(speed);
    const QSignalBlocker quietWait(waitEachRound);

    speed->setValue(botSpeed);
    waitEachRound->setChecked(waitAfterEachRound);
}

void SettingsSheet::focusDone()
{
    done->setFocus(Qt::OtherFocusReason);
}

void SettingsSheet::announce()
{
    emit settingsChanged(speed->value(), waitEachRound->isChecked());
}

void SettingsSheet::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);

    // The same dim as the pause menu's, from Theme (section 18.1).
    painter.fillRect(rect(), theme.dimmedOverlay);

    QWidget::paintEvent(event);
}

void SettingsSheet::mousePressEvent(QMouseEvent* event)
{
    // Swallowed, as PauseOverlay's are: the table under the dim is not live.
    event->accept();
}
