#ifndef PROMPT_BAR_H
#define PROMPT_BAR_H

#include "core/GameSnapshot.h"
#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QCoreApplication>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// Section 12's fourth region, and the last of them to be built: "where the game
// talks to you. One line, always in the same place, so you learn where to look."
//
// WHAT IT IS FOR, in C3, is the question a dimmed hand raises. Legality lighting
// says WHICH cards you may play; this says WHY the others are out - "You must
// follow the lead suit" when the hand is restricted, "You lead" when it is not.
// The lighting is the primary cue and this is the secondary one, which is why
// C3 can have both and phase B needed neither.
namespace promptbar
{

// TRANSLATED, like InfoStrip's and BidStrip's words and unlike snapshotlog's -
// this is chrome a player reads, so it must move when a translation is loaded.
// QCoreApplication::translate() with an explicit context at every call site, for
// the reason InfoStrip.h gives at length: Q_DECLARE_TR_FUNCTIONS is class-only
// and a hand-rolled tr() wrapper hides every string from lupdate.

struct Fields
{
    // Empty between turns, which is most of the time. The bar keeps its height
    // and says nothing rather than disappearing - see the widget below.
    QString hint;
};

// The prompt is what says it is your turn at all, and the snapshot is what says
// which suit is being followed. Both are needed: legality lives only on the
// prompt, and the lead suit only on the snapshot.
//
// A RESTRICTED HAND IS ONE WITH AN ILLEGAL CARD IN IT, asked of the entries
// rather than inferred from the lead suit. They usually agree and they come
// apart in the case that matters: a player void in the lead suit may play
// anything, so there is a lead suit and nothing to follow, and a bar driven off
// hasLeadSuit alone would tell them to follow a suit they do not hold.
Fields fieldsFor(const PlayPrompt& prompt, const GameSnapshot& snapshot);

// Nothing to say: between turns, and once the game is over.
Fields idle();

} // namespace promptbar

// The widget. One line, dumb, and the same shape as InfoStrip.
class PromptBar : public QWidget
{
    Q_OBJECT

public:
    explicit PromptBar(QWidget* parent = nullptr);

    void setFields(const promptbar::Fields& fields);
    void setTheme(const Theme& theme);

private:
    // ITS HEIGHT IS RESERVED FOR THE WHOLE GAME, which is B2b's finding applied
    // before it can bite again: a widget whose size depends on what it is saying
    // moves everything laid out above it, and this one is empty for most of a
    // game. It holds an empty string rather than being hidden, so the table above
    // it never shifts.
    Theme theme = Theme::light();

    QLabel* hint = nullptr;
};

#endif
