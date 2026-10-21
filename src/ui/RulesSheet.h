#ifndef RULES_SHEET_H
#define RULES_SHEET_H

#include "paint/Theme.h"
#include "ui/NewGameDialog.h"

#include <romanian_whist/Card.h>

#include <QString>
#include <QStringList>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
QT_END_NAMESPACE

// The menu bar's Rules (the redesign's phase 9): the rules at a glance, for the
// game on the table.
//
// THE MOCK-UP'S showRules(), and the engine's docs/RULES.md behind it, fitted to
// what this client deals. The mock-up plays one schedule with no special rounds;
// the client deals either structure, can end on a Forehead and a Hidden round,
// and can make every one-card round a Forehead one - so the sheet says which.
//
// Translated through QCoreApplication::translate() under the context
// "rulessheet", for the reason table/ScoreboardItem.h gives.
namespace rulessheet
{

// The lowest rank in an N-player deck: two ranks per player down from the Ace
// (RULES.md rule 2), so Jack, 9, 7, 5 and 3 for two to six players.
romanian_whist::Rank lowestRank(unsigned int seats);

// "Jack", "9": a rank as the deck line reads it.
QString rankName(romanian_whist::Rank rank);

// One sentence per rule, in the mock-up's order: the deck, the schedule, the
// special rounds (only when this game deals them), trump, bidding, play and
// scoring.
QStringList linesFor(const newgame::Config& config);

} // namespace rulessheet

// The widget. Built as PauseOverlay is - a child of the window's stack, dimming
// what is behind it and swallowing the clicks that land on it - because phase 10
// restyles every sheet together (the owner's call, 2026-10-21).
class RulesSheet : public QWidget
{
    Q_OBJECT

public:
    explicit RulesSheet(QWidget* parent = nullptr);

    void setTheme(const Theme& theme);

    // Fills the sheet for the game `config` deals.
    void showRules(const newgame::Config& config);

    // Puts the keyboard on Close, so Enter closes. Called by the window when it
    // shows this, as the other overlays' are.
    void focusClose();

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    Theme theme = Theme::light();

    QWidget* panel = nullptr;
    QLabel* rules = nullptr;
    QPushButton* closeButton = nullptr;
};

#endif
