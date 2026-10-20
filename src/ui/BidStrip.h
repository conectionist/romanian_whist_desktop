#ifndef BID_STRIP_H
#define BID_STRIP_H

#include "engine/Prompts.h"
#include "paint/Theme.h"

#include <QCoreApplication>
#include <QString>
#include <QWidget>

#include <optional>
#include <vector>

QT_BEGIN_NAMESPACE
class QHBoxLayout;
class QLabel;
class QPushButton;
QT_END_NAMESPACE

// Section 15.1's strip: the question, one button per legal bid, and the reason
// one of them is disabled.
//
// A PANEL AND NOT A QDialog, which is a shutdown decision rather than a taste
// one and is the whole reason this type exists in this shape. QDialog::exec()
// spins a nested event loop for exactly as long as the game thread is parked in
// makeBet(), which is unbounded - and a modal dialog takes the close button with
// it, so MainWindow::closeEvent never runs, stop() is never called, and
// abandon() never wakes the provider. That is precisely the path C5 exists to
// test, and with a modal dialog the test cannot be performed at all. See section
// 4.4.
namespace bidstrip
{

// TRANSLATED, the same choice the scoreboard makes and for the reason spelled out
// at length in table/ScoreboardItem.h: these are words a player reads, so they must move when a
// translation is loaded - unlike snapshotlog's fixed ASCII, which tests assert
// on.
//
// QCoreApplication::translate() with an explicit context at every call site, NOT
// a tr() of our own. Q_DECLARE_TR_FUNCTIONS expands with `public:` and
// `private:` inside it, so it is class-only whatever its name suggests, and a
// hand-rolled inline wrapper hides every string from lupdate - which is the
// sweep section 7 says must have nothing left to find.

// What the strip says, from one prompt. No widget here, which is what makes it
// the part worth a test - the split SnapshotLog.h argues for and the scoreboard
// follows.
struct Fields
{
    QString question;

    // Buttons are 0..highest INCLUSIVE, so an 8-card round has nine of them.
    // BetContext carries no trick count of its own; the hand's size IS the
    // number of tricks and so the highest legal bid.
    unsigned int highest = 0;

    // Exactly one value or none. The engine fills it from getForbiddenBet() for
    // the final bidder only, and leaves it empty once the bids so far already
    // exceed the trick count. THE CLIENT ASKS; IT DOES NOT REIMPLEMENT THE
    // ARITHMETIC.
    std::optional<unsigned int> forbidden;

    // Empty unless `forbidden` is engaged. Explains the disabled button rather
    // than leaving the player to work out why one number is greyed.
    QString explanation;
};

Fields fieldsFor(const BetPrompt& prompt);

} // namespace bidstrip

// The widget. Dumb by design: it asks fieldsFor() what to say and builds buttons
// for it.
//
// IT KNOWS NOTHING ABOUT PROVIDERS OR PROMPT IDS. It emits the bid that was
// chosen and MainWindow does the rest - which is what keeps the generation gate
// (section 4.2) in one place instead of two.
class BidStrip : public QWidget
{
    Q_OBJECT

public:
    explicit BidStrip(QWidget* parent = nullptr);

    void setPrompt(const BetPrompt& prompt);
    void setTheme(const Theme& theme);

signals:
    // THE BID, not the button's position. They are equal today because the
    // buttons run 0..highest with none missing, and writing the index would
    // still be wrong: it is the same class of mistake as submitting a display
    // slot where the engine wants a hand position, and the play side has a whole
    // type (PlayPrompt::handIndex) built to prevent it.
    void bidChosen(unsigned int bet);

protected:
    // Section 15.1's keyboard: the digit keys bid directly, and a disabled digit
    // is ignored. Phase E owns the full keyboard map; this is the one binding
    // that section names as part of bidding itself.
    void keyPressEvent(QKeyEvent* event) override;

private:
    // REBUILT PER PROMPT rather than created once and hidden. handSize changes
    // every round - the schedule runs 1 card up to 8 and back down - so a fixed
    // bank of buttons would be wrong in all but one round, and hiding the spares
    // leaves a strip whose width lies about what is on offer.
    void rebuild();

    Theme theme = Theme::light();

    bidstrip::Fields fields;

    QLabel* question = nullptr;
    QLabel* explanation = nullptr;
    QHBoxLayout* buttonRow = nullptr;

    // Parallel to the bids they carry: buttons[i] is the bid i. Held so
    // keyPressEvent can find one by value without walking the layout, and so a
    // rebuild can delete exactly what it created.
    std::vector<QPushButton*> buttons;
};

#endif
