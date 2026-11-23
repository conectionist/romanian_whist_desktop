#ifndef PAUSE_OVERLAY_H
#define PAUSE_OVERLAY_H

#include "ui/Overlay.h"

// What the menu bar's Pause raises, and Esc: Resume, or abandon the game and go back to the
// main menu. An Overlay, not a modal dialog, for the reasons recorded there.
//
// THE PAUSE ITSELF IS NOT THIS CLASS'S. MainWindow keeps this widget's
// visibility and GameController::setPaused() in lockstep - see
// setPauseMenuVisible() - so "the menu is up" and "the game is held" are one
// fact rather than two a caller can set apart.
class PauseOverlay : public Overlay
{
    Q_OBJECT

public:
    explicit PauseOverlay(QWidget* parent = nullptr);

    // Resume, so Enter resumes.
    void focusDefault() override;

signals:
    void resumeRequested();
    void mainMenuRequested();

private:
    SheetButton* resume = nullptr;
    SheetButton* mainMenu = nullptr;
};

#endif
