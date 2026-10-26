#ifndef RULES_SHEET_H
#define RULES_SHEET_H

#include "ui/NewGameDialog.h"
#include "ui/Overlay.h"

#include <romanian_whist/Card.h>

#include <QString>
#include <QStringList>

QT_BEGIN_NAMESPACE
class QLabel;
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

// The widget, an Overlay like every other sheet, because phase 10 restyles them
// all together (the owner's call, 2026-10-21).
class RulesSheet : public Overlay
{
    Q_OBJECT

public:
    explicit RulesSheet(QWidget* parent = nullptr);

    // Fills the sheet for the game `config` deals.
    void showRules(const newgame::Config& config);

    // Close, so Enter closes.
    void focusDefault() override;

signals:
    void closeRequested();

private:
    QLabel* rules = nullptr;
    SheetButton* closeButton = nullptr;
};

#endif
