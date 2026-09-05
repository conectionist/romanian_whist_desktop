#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include "core/GameSnapshot.h"
#include "engine/GameController.h"

#include <QMainWindow>
#include <QString>

#include <chrono>
#include <memory>
#include <vector>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

// Phase A's window: a QTextEdit and nothing else.
//
// That is not a placeholder to apologise for, it is what makes the phase
// diagnostic. With no scene, no card art and no layout in the process, a wrong
// card in the log MUST be a snapshot bug - there is nothing else it could be.
// Phase B draws the table and demotes this widget behind a View menu rather
// than deleting it, because it is the thing to reach for when something looks
// wrong on that table later.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // Closing the window is what ends the game. Sets `closing` first, then
    // stops - see the flag below for why the order matters.
    void closeEvent(QCloseEvent* event) override;

private slots:
    void render(std::shared_ptr<const GameSnapshot> snapshot);

    // NOT connected to close() directly. Closing the window calls stop(),
    // stop() is what produces gameStopped, and close() from here would re-enter
    // closeEvent(). This checks `closing` and forces the close only when the
    // stop came from somewhere else - which in phase A is nowhere, and in phase
    // C is a game abandoned from the far side.
    void onGameStopped();

    void onGameOver(std::vector<romanian_whist::Standing> standings,
                    std::vector<romanian_whist::Standing> winners);

    // The run slot caught something. Neither onGameStopped() nor onGameOver()
    // fires on that path, so this is the only report the window gets.
    void showFailure(QString what);

private:
    void appendLine(const QString& line);

    Ui::MainWindow *ui;

    // Zero until the first snapshot arrives, which is what the log's elapsed
    // column is measured from.
    std::chrono::steady_clock::time_point started;
    bool haveStarted = false;

    // Set by closeEvent() before it stops the game, so onGameStopped() can tell
    // a stop it caused from one it did not.
    bool closing = false;

    // DECLARED LAST, so it is destroyed FIRST: ~GameController joins the game
    // thread, and that has to happen before the widgets its slots write to are
    // gone. ~MainWindow stops it explicitly as well, because the destructor
    // BODY runs before any member is destroyed - deleting `ui` there would
    // otherwise outrun the join.
    GameController controller;
};
#endif // MAIN_WINDOW_H
