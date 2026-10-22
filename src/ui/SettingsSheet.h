#ifndef SETTINGS_SHEET_H
#define SETTINGS_SHEET_H

#include "ui/Overlay.h"

QT_BEGIN_NAMESPACE
class QCheckBox;
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
// An Overlay, in section 2.13's look since the redesign's phase 10. Its slider and
// box are Qt's own, drawn by the style from the sheet's dark palette.
class SettingsSheet : public Overlay
{
    Q_OBJECT

public:
    explicit SettingsSheet(QWidget* parent = nullptr);

    // What the sheet opens on, without announcing it as a change.
    void setValues(int botSpeed, bool waitAfterEachRound);

    // Done, so Enter closes, as the other overlays do.
    void focusDefault() override;

signals:
    // A control moved: the slider's position (newgame::SlowestSpeed to
    // FastestSpeed) and the box.
    void settingsChanged(int botSpeed, bool waitAfterEachRound);

    void closeRequested();

private:
    void announce();

    QSlider* speed = nullptr;
    QCheckBox* waitEachRound = nullptr;
    SheetButton* done = nullptr;
};

#endif
