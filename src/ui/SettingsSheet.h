#ifndef SETTINGS_SHEET_H
#define SETTINGS_SHEET_H

#include "paint/Theme.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QPushButton;
class QSlider;
QT_END_NAMESPACE

// The menu bar's Settings (the redesign's phase 9, open question Q4): the bot speed
// and whether the round sheet waits for you, the two pacing knobs the setup
// dialog also asks about.
//
// LIVE, by the owner's call of 2026-10-21. Each change is announced at once and
// the window applies it to the game in play (GameController::setPacing()) and to
// the settings the next New Game opens on. The mock-up's sheet does the same with
// its pace; there is nothing to apply and nothing to cancel, so its one button is
// Done.
//
// Built as PauseOverlay is, in today's look, for phase 10 to restyle with the
// others; see RulesSheet.
class SettingsSheet : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsSheet(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // What the sheet opens on, without announcing it as a change.
    void setValues(int botSpeed, bool waitAfterEachRound);

    // Puts the keyboard on Done, so Enter closes, as the other overlays do.
    void focusDone();

signals:
    // A control moved: the slider's position (newgame::SlowestSpeed to
    // FastestSpeed) and the box.
    void settingsChanged(int botSpeed, bool waitAfterEachRound);

    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    void announce();

    Theme theme = Theme::light();

    QWidget* panel = nullptr;
    QSlider* speed = nullptr;
    QCheckBox* waitEachRound = nullptr;
    QPushButton* done = nullptr;
};

#endif
