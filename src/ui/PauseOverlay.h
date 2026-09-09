#ifndef PAUSE_OVERLAY_H
#define PAUSE_OVERLAY_H

#include "paint/Theme.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QPushButton;
QT_END_NAMESPACE

// What the prompt bar's Menu button raises, and Esc. Resume, or abandon the game
// and go back to the main menu.
//
// A PANEL AND NOT A QDialog, which is section 4.4's rule and which this started
// out breaking. It was a modal QDialog first, on the reasoning that section
// 4.4's warning is about a dialog up while the GAME THREAD IS PARKED IN A PROMPT
// for an unbounded time - and that a close arriving here would be refused rather
// than swallowed, leaking nothing.
//
// THAT WAS TRUE ABOUT SAFETY AND WRONG ABOUT BEHAVIOUR. Qt drops window-system
// events to a window blocked by a modal dialog, so the title bar's X did
// NOTHING while the pause was up - not a hang, and recoverable by pressing
// Resume first, but a dead close button is a defect whether or not anything
// leaks. Measured on the way to fixing it: a PROGRAMMATIC close() works fine
// through a modal dialog, so it is specifically the window manager's route that
// is blocked, which is the one a person uses.
//
// So this is a child widget filling the window instead. It dims what is behind
// it with Theme::dimmedOverlay and swallows the clicks that land on it, so it
// reads and behaves as modal - and the window is never blocked, so closing it
// runs closeEvent(), stop() and cancelPacing() like any other close. BidStrip
// reached the same shape from the same section for the same reason.
//
// THE PAUSE ITSELF IS NOT THIS CLASS'S. MainWindow keeps this widget's
// visibility and GameController::setPaused() in lockstep - see
// setPauseMenuVisible() - so "the menu is up" and "the game is held" are one
// fact rather than two a caller can set apart.
class PauseOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit PauseOverlay(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // Puts the keyboard on Resume, so Enter resumes. Called by the window when
    // it shows this - rather than from showEvent(), because focus taken during
    // a show is easy to lose to whatever is shown next.
    void focusResume();

signals:
    void resumeRequested();
    void mainMenuRequested();

protected:
    // The dim. Everything behind is still painted; this is drawn over it.
    void paintEvent(QPaintEvent* event) override;

    // SWALLOWED, and not left to the default. QWidget::mousePressEvent() ignores
    // the event, which propagates to the PARENT rather than to the widgets
    // underneath - so the table would not be clicked either way. Accepting it
    // says so on purpose, and stops a press on the dim from reaching the window.
    void mousePressEvent(QMouseEvent* event) override;

private:
    Theme theme = Theme::light();

    // The words and the buttons, on a plaque - the same arrangement MainMenuPage
    // uses and for the reason recorded there: Theme has no colour for text on
    // the table surface, because every word on the table is on a plaque.
    QWidget* panel = nullptr;

    QPushButton* resume = nullptr;
    QPushButton* mainMenu = nullptr;
};

#endif
