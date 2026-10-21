#ifndef OVERLAY_H
#define OVERLAY_H

#include "paint/Theme.h"

#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QVBoxLayout;
QT_END_NAMESPACE

// WHAT EVERY SHEET OVER THE PAGE IS: the pause menu, the round sheet, the
// game-over panel, and the menu bar's Rules and Settings. A subclass fills
// panelLayout() and says which button the keyboard starts on; this class does
// the rest, once, where the five used to each carry a copy (PR #35's review,
// finding #11). Phase 10 restyles them here.
//
// A PANEL AND NOT A QDialog, which is section 4.4's rule and which the pause
// menu started out breaking. It was a modal QDialog first, on the reasoning that
// section 4.4's warning is about a dialog up while the GAME THREAD IS PARKED IN
// A PROMPT for an unbounded time - and that a close arriving then would be
// refused rather than swallowed, leaking nothing.
//
// THAT WAS TRUE ABOUT SAFETY AND WRONG ABOUT BEHAVIOUR. Qt drops window-system
// events to a window blocked by a modal dialog, so the title bar's X did NOTHING
// while the pause was up - not a hang, and recoverable by pressing Resume first,
// but a dead close button is a defect whether or not anything leaks. Measured on
// the way to fixing it: a PROGRAMMATIC close() works fine through a modal
// dialog, so it is specifically the window manager's route that is blocked,
// which is the one a person uses.
//
// So a sheet is a child widget covering the page instead. It dims what is
// behind it and swallows the clicks that land on it, so it reads and behaves as
// modal - and the window is never blocked, so closing it runs closeEvent(),
// stop() and cancelPacing() like any other close.
//
// IT COVERS ITS PARENT AT EVERY SIZE, by following the parent's resizes itself.
// The parent is the window's page stack, which changes size with the window and
// also when the log dock is shown or hidden. Sized only on the window's resizes,
// a sheet kept its old size when the log came or went: too tall and off-centre,
// or short of the bottom, leaving the hand uncovered and clickable under a sheet
// that was holding the game (PR #35's review, finding #2).
//
// AND TAB STAYS INSIDE IT, through focusNextInside(), which the window calls
// for the sheet on top. Tab walking out through the menu bar reached the bid
// strip under the dim, and Space then bid while the sheet held the game
// (finding #4).
class Overlay : public QWidget
{
    Q_OBJECT

public:
    // `parent` is the page stack, which this covers from now on.
    explicit Overlay(QWidget* parent);

    // The panel's colours: the words sit on a plaque rather than on the table's
    // green, because Theme has no colour for text on the green - textPrimary is
    // chosen against seatPlaque. D1 found that by rendering a menu title that
    // was dark grey on dark green.
    void setTheme(const Theme& theme);

    // Puts the keyboard on the button this sheet leads with, so Enter takes it.
    // Called by the window after it shows the sheet, rather than from
    // showEvent(): focus taken during a show is easy to lose to whatever is
    // shown next.
    virtual void focusDefault() = 0;

    // Moves the keyboard to the next (or previous) control inside this sheet,
    // in the window's Tab order, wrapping round at either end. From outside the
    // sheet it lands on the first (or last). False when the sheet has nothing to
    // stop on, which leaves the keyboard where it was.
    bool focusNextInside(bool next);

protected:
    // The plaque, centred on the page and no bigger than its contents, and the
    // column a subclass adds its words and buttons to.
    QWidget* panel() const { return plaque; }
    QVBoxLayout* panelLayout() const { return column; }

    const Theme& theme() const { return palette; }

    // A sheet's title: bold, and `scale` times the label's own font - off the
    // widget's font rather than a point size written here, so a large system
    // font or a HiDPI display gets a title that grew with everything else.
    static void styleTitle(QLabel* title, qreal scale);

    // The dim, from Theme (section 18.1), under the panel.
    void paintEvent(QPaintEvent* event) override;

    // SWALLOWED, and not left to the default. QWidget::mousePressEvent() ignores
    // the event, which propagates to the PARENT rather than to the widgets
    // underneath - so the table would not be clicked either way. Accepting it
    // says so on purpose, and stops a press on the dim from reaching the window.
    void mousePressEvent(QMouseEvent* event) override;

    // The parent's resizes, which this follows.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    Theme palette = Theme::light();

    QWidget* plaque = nullptr;
    QVBoxLayout* column = nullptr;
};

#endif
