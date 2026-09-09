#ifndef PAUSE_DIALOG_H
#define PAUSE_DIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
class QPushButton;
QT_END_NAMESPACE

// What the prompt bar's Menu button opens, and Esc. Resume, or abandon the game
// and go back to the main menu.
//
// A MODAL DIALOG, WHICH SECTION 4.4 WARNS AGAINST - and the warning does not
// apply here, which is worth being explicit about because the same section is
// why BidStrip is a panel.
//
// The hazard there is a dialog up while the GAME THREAD IS PARKED IN A PROMPT
// for an unbounded time: QDialog::exec() spins a nested event loop, a modal
// dialog takes the close button with it, MainWindow::closeEvent never runs,
// stop() is never called and abandon() never wakes the provider. That is exactly
// what C5 tests and what a modal bid dialog would make untestable.
//
// This one is different in the way that matters. A close arriving while it is up
// is REFUSED rather than swallowed - nothing is destroyed and no thread is left
// behind - and every path that does destroy the window (~MainWindow,
// ~GameController) calls stop(), which calls cancelPacing(), which is the one
// thing that can wake the indefinite pace() D1 introduced. So the game is
// reachable from every direction the window can be torn down from.
//
// THE PAUSE ITSELF IS NOT THIS CLASS'S. MainWindow raises and lifts it around
// exec() with an RAII guard, so it is lifted on every exit path including a
// throw. This dialog only says which button was pressed.
class PauseDialog : public QDialog
{
    Q_OBJECT

public:
    // QDialog::exec() returns an int, and these are what this one returns. Not
    // Accepted/Rejected: both buttons are a deliberate choice rather than one
    // being a cancel, and a closed dialog is a Resume - which QDialog::reject()
    // gives for free through Esc and the title bar's close.
    enum Outcome
    {
        Resume = QDialog::Rejected,
        MainMenu = QDialog::Accepted
    };

    explicit PauseDialog(QWidget* parent = nullptr);

private:
    QPushButton* resume = nullptr;
    QPushButton* mainMenu = nullptr;
};

#endif
