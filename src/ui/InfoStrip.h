#ifndef INFO_STRIP_H
#define INFO_STRIP_H

#include "core/GameSnapshot.h"
#include "paint/CardRenderer.h"
#include "paint/Theme.h"

#include <QCoreApplication>
#include <QSize>
#include <QString>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QLabel;
QT_END_NAMESPACE

// Section 12's info strip: the five facts that are true all round and change
// only between rounds.
//
// FOUR OF THE FIVE. The fifth is the elapsed clock, and section 19 is a whole
// section on why it must not be here yet: it is the one field in this strip
// that does NOT come from a snapshot, because a clock driven by engine events
// ticks only when the engine publishes - the terminal's stutter, reproduced in a
// client that has no reason to have it. It arrives in phase E as a QTimer.
namespace infostrip
{

// TRANSLATED, which is the opposite of snapshotlog's choice and for the reason
// that file gives: its words are asserted on by tests and read out of saved
// logs, so they must not move when a translation is loaded. These are chrome a
// player reads, so they must.
//
// Spelled as QCoreApplication::translate() with an explicit context at every
// call site, NOT through a tr() of our own. Q_DECLARE_TR_FUNCTIONS is the
// obvious tool and does not work here - it expands with `public:` and
// `private:` inside it, so it is a class-only macro whatever its name suggests.
// A hand-rolled inline tr() wrapper compiles and is worse: lupdate finds
// strings by recognising the CALL, so a wrapper hides every one of them from
// phase F, which is the sweep section 7 says must not have anything to find.

// What the strip draws, from one snapshot. The trump is handed over as a CARD
// rather than as text: the strip renders it through CardRenderer, and this
// struct is the seam between "which card" and "how it is drawn".
struct Fields
{
    QString round;      // "Round 7 of 24"
    QString cards;      // "6 cards"
    QString roundType;  // "Normal", through tr()

    // hasTrump is false in the eight-card round, which has no trump at all -
    // and `trump` is then Card's default, which is a REAL two of clubs.
    // SnapshotLog.cpp carries the same trap; drawing it unguarded is how the
    // strip would claim a trump that does not exist.
    bool hasTrump = false;
    romanian_whist::Card trump{};
};

Fields fieldsFor(const GameSnapshot& snapshot);

// The round type as a PLAYER reads it.
//
// THE SECOND SPELLING, NOT THE THIRD. snapshotlog has fixed ASCII for the
// reason above, and that duplication is deliberate; MainWindow's status bar had
// a third switch of its own, which was one too many. It calls this now, so the
// client has exactly two: one for machines and one for people.
QString roundTypeText(romanian_whist::RoundType type);

// WHETHER THIS ROUND TYPE IS CALLED OUT, which section 17 asks for: Forehead and
// Hidden read "in an accent colour, so the round is identifiable before the
// cards are dealt".
//
// NEVER A COLOUR-ONLY CUE, which is section 18's rule and the reason this is
// safe: the WORD is already the distinguishing information and is already on
// screen, so the colour reinforces it rather than carrying it. A reader who
// cannot tell the two colours apart has lost nothing.
bool isCalledOut(romanian_whist::RoundType type);

} // namespace infostrip

// The widget. Dumb by design: it asks fieldsFor() what to say and draws it.
//
// QLabels rather than a paintEvent, unlike the scoreboard beside it - the strip
// is a row of independent items with no columns to align, so a layout is the
// right tool and it brings eliding and tr() with it. The trump's label holds a
// PIXMAP, and QLabel honours a device-pixel-ratio-stamped one, so B1's contract
// carries through with nothing here having to know about it.
class InfoStrip : public QWidget
{
    Q_OBJECT

public:
    explicit InfoStrip(QWidget* parent = nullptr);

    void setSnapshot(const GameSnapshot& snapshot);
    void setTheme(const Theme& theme);

private:
    void refresh();

    // THE STRIP IS SIZED FROM THE CARD, not the other way round. Section 12's
    // mockup is one line of ASCII, which is a drawing convention and not a
    // height budget: a card scaled into a text line is about 28 pixels tall and
    // its rank is not legible. Derived from the font so it follows a user who
    // has set a larger one.
    qreal trumpCardHeight() const;

    // THE SLOT THE TRUMP SITS IN, RESERVED FOR THE WHOLE GAME. Held rather than
    // recomputed at each use, for two reasons: the fixed size of the label and
    // the size the pixmap is rasterized at must be the same number - a QLabel
    // clips a pixmap larger than itself rather than scaling it - and the strip's
    // height must not depend on whether this round has a trump. The eight-card
    // round has none, and hiding the label took the strip from 77 pixels to 29,
    // moving the table and the scoreboard beneath it twice a game.
    //
    // Derived from the font at construction, which is where the layout's margins
    // come from too - and is the same gap docs/KNOWN_ISSUES.md records: neither
    // follows a font or a screen that changes under a running window.
    QSize trumpSlot;

    Theme theme = Theme::light();

    // Its own, and the cost is one small card: the scene's cache holds
    // table-sized cards and this holds one at strip size, so sharing would save
    // nothing and would couple two widgets that have no other reason to know
    // about each other.
    CardRenderer cards;

    QLabel* round = nullptr;
    QLabel* cardCount = nullptr;
    QLabel* roundType = nullptr;
    QLabel* trumpLabel = nullptr;
    QLabel* trumpCard = nullptr;

    GameSnapshot shown;
    bool haveShown = false;
};

#endif
