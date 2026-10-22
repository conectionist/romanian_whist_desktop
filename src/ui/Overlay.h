#ifndef OVERLAY_H
#define OVERLAY_H

#include "paint/Panel.h"
#include "paint/Theme.h"
#include "ui/LitButton.h"

#include <QColor>
#include <QFont>
#include <QSize>
#include <QString>
#include <QWidget>

#include <initializer_list>
#include <vector>

QT_BEGIN_NAMESPACE
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QVBoxLayout;
QT_END_NAMESPACE

// THE SHEETS' LOOK, section 2.13 of docs/ui-redesign/IMPLEMENTATION_PLAN.md: the
// table redesign's phase 10. page.html's .scrim and .sheet - a dim over the page,
// and on it a panel 720 wide, its title in Marcellus 32, a muted line under it,
// tables with small capitals for headings, and its buttons at the bottom right.
//
// IN DEVICE-INDEPENDENT PIXELS, NOT TABLE UNITS, by the owner's call of
// 2026-10-21: the mock-up's numbers read as screen pixels, as the menu bar's are.
// The mock-up's sheets scale with its table, which at the smallest window would
// set their words at six pixels; a sheet is read, so it keeps its size, and in a
// window too small for it it narrows and its body scrolls.
namespace sheet
{

// .sheet: 720 wide, its border included; never closer than 16 to the page's edges.
inline constexpr int width = 720;
inline constexpr int pageMargin = 16;

// .sheet's padding, inside a border of 1.
inline constexpr int border = 1;
inline constexpr int paddingTop = 26;
inline constexpr int paddingX = 30;
inline constexpr int paddingBottom = 24;

// What a full-width sheet has across inside its border and padding: 658.
inline constexpr int contentWidth = width - 2 * (border + paddingX);

// What a line of a sheet is, which says how it is set (page.html):
enum class Role
{
    // h2: Marcellus 32, cream.
    Title,

    // .meta: the muted line under a title, 17.
    Meta,

    // The sheet's own words - the rules - 17, cream.
    Body,

    // .note: 15, muted.
    Note,

    // .label: 13, bold, in spaced capitals, muted.
    Label
};

QFont fontFor(Role role);
QColor colourFor(Role role, const Theme::TablePalette& palette);

// THE TEXT AS RICH TEXT, every ♥ and ♦ in the red suit's colour: what a label that
// can show a suit glyph is given, as textruns colours one on the table. Escaped on
// the way, so nothing else in it is read as markup.
QString withSuits(const QString& text, const Theme::TablePalette& palette);

// A WHOLE NUMBER AS A SHEET'S TABLE WRITES IT: a negative one with section 2.13's
// minus sign, U+2212, which is as wide as a digit in tabular figures - not
// QString::number()'s hyphen, which is narrower, so one column of a sheet would
// otherwise carry two different minus signs (PR #36's review, finding #6). No plus:
// a sign that carries meaning is the caller's to add (roundsheet::roundScoreFor()).
QString number(int value);

} // namespace sheet

// .sheet .actions button: Marcellus 19 on the buttons' green, rounded 8, its
// border the buttons' gold - or, QUIET, with no fill at all. Lit, under the cursor
// or reached by the keyboard (:focus-visible), its border and label are
// gold-bright.
//
// A QPushButton, painted, so a sheet's buttons keep everything a button does - the
// click, Enter and Space, the default - and every test that finds one by name. When
// it is lit is LitButton's rule.
class SheetButton : public LitButton
{
    Q_OBJECT

public:
    enum class Kind
    {
        // The green one: what the sheet is for. Rightmost, as the mock-up's last.
        Primary,

        Quiet
    };

    SheetButton(const QString& text, Kind kind, QWidget* parent = nullptr);

    Kind kind() const { return buttonKind; }
    void setKind(Kind kind);

    void setColours(const Theme::TablePalette& palette);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    Kind buttonKind;
    Theme::TablePalette colours;
};

// .sheet table: headings in small spaced capitals over a faint gold rule, rows of
// 18-pixel figures that line up, each over a fainter rule.
//
// LABELS IN A GRID, so a test reads a cell's text as a player does and the column
// widths are Qt's to work out; the rules are painted under them.
class SheetTable : public QWidget
{
    Q_OBJECT

public:
    struct Column
    {
        QString heading;
        Qt::Alignment alignment = Qt::AlignRight;

        // A heading that may be shortened, with an ellipsis, to fit the table into
        // a width: a player's name over a column of numbers (fitHeadingsTo()).
        bool elide = false;
    };

    // A cell's colour: the text's own, or a score's - green for a gain and red for
    // a loss, which reinforces the sign it carries rather than replacing it
    // (section 18) - or the muted of a line that says there is nothing to show.
    enum class Tone
    {
        Plain,
        Good,
        Bad,
        Muted
    };

    // Given its first few parts - { text, tone, strong } - a cell takes the
    // defaults for the rest. Every part after the text has an initializer of its
    // own, `note` included, which is what keeps -Wmissing-field-initializers quiet
    // at every table.
    struct Cell
    {
        QString text;
        Tone tone = Tone::Plain;

        // Bold: your own row, or a winner's.
        bool strong = false;

        // A SECOND, SMALLER PART AFTER THE TEXT, in .note's muted 15 on the text's
        // baseline: the history's "+6 1/1", where the tone colours the score and
        // the note stays muted whatever it is. Plain text, as the cell's own.
        QString note{};
    };

    explicit SheetTable(QWidget* parent = nullptr);

    // The columns and their headings. Empties the table.
    void setColumns(std::vector<Column> columns);

    void addRow(const std::vector<Cell>& cells);

    // ONE LINE ACROSS EVERY COLUMN, muted, in .note's 15: the mock-up's
    // `colspan` row that says a table has nothing in it yet. Counted as a row,
    // whose one cell is in column 0.
    void addNote(const QString& text);

    // Every row but the headings.
    void clearRows();

    // SHORTENS THE HEADINGS THAT MAY BE, so the table is no wider than `width`:
    // what the other columns need is set aside, and the rest shared out evenly
    // between the elided ones - but a column whose cells are wider than its share
    // keeps its cells' width, and the others share what that leaves. Measured by
    // each heading's own size, as the layout measures it. A shortened heading ends
    // in an ellipsis and keeps its whole text as its tooltip. Run after the rows
    // are in; filling the table again (setColumns(), clearRows()) restores the
    // headings whole. Cells too wide to fit at all still overflow.
    void fitHeadingsTo(int width);

    // The rows added since the last clear, not counting the headings; and one
    // row's cell, 0 being the first row under them. READ by the sheets' tests.
    int rowCount() const { return static_cast<int>(rows.size()); }
    QLabel* cell(int row, int column) const;

    // A cell's note, empty for none; and a column's heading. READ by tests.
    QString note(int row, int column) const;
    QLabel* heading(int column) const;

    void setColours(const Theme::TablePalette& palette);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QLabel* addLabel(const QString& text, int row, int column, bool heading);
    void colour(QLabel* label, Tone tone);

    QGridLayout* grid = nullptr;
    std::vector<Column> columns;
    std::vector<QLabel*> headings;
    std::vector<std::vector<QLabel*>> rows;
    std::vector<std::vector<Tone>> tones;
    Theme::TablePalette colours;
};

// WHAT EVERY SHEET OVER THE PAGE IS: the pause menu, the round sheet, the
// game-over panel, and the menu bar's Rules and Settings. A subclass fills
// panelLayout() and says which button the keyboard starts on; this class does
// the rest, once, where the five used to each carry a copy (PR #35's review,
// finding #11) - and since phase 10 it is also where they look the way section
// 2.13 says.
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

    // The table's palette (section 2.2), which the sheets are drawn in: their
    // words, their buttons, and - through a dark QPalette - the controls Qt's style
    // draws on them, Settings' slider and box.
    void setTheme(const Theme& theme);

    // Puts the keyboard on the button this sheet leads with, so Enter takes it.
    // Called by the window after it shows the sheet, rather than from
    // showEvent(): focus taken during a show is easy to lose to whatever is
    // shown next.
    virtual void focusDefault() = 0;

    // Moves the keyboard to the next (or previous) control inside this sheet,
    // in the window's Tab order, wrapping round at either end, and scrolls it into
    // view. From outside the sheet it lands on the first (or last). False when the
    // sheet has nothing to stop on, which leaves the keyboard where it was.
    bool focusNextInside(bool next);

    // The sheet's own rect - its panel, border included - in this widget.
    QRect sheetRect() const;

protected:
    // The sheet's content, top to bottom, inside its padding; and the widget it
    // is laid on.
    QWidget* panel() const { return plaque; }
    QVBoxLayout* panelLayout() const { return column; }

    const Theme& theme() const { return palette; }

    // Sets `label` as a line of `role`: its font and colour, and wrapping. Kept,
    // so a new theme sets it again. Plain text, unless the label has already been
    // set to rich text: a sheet's lines carry player names, which must read as
    // typed.
    void dress(QLabel* label, sheet::Role role);

    // The sheet's buttons, at the bottom right, 12 apart and 20 below what is
    // above them, in the order given: the primary one last. Returns their row, for
    // a sheet whose primary button changes (GameOverPanel's failure face).
    QHBoxLayout* addActions(std::initializer_list<SheetButton*> buttons);

    // Sizes the sheet to what it now holds, and centres it: called when its
    // content changes. No wider than section 2.13's 720 or the page less its
    // margins, and no taller than the page - past that, its body scrolls.
    void placeSheet();

    // WHAT THE SHEET'S CONTENT HAS ACROSS, inside its border and padding: 658 at
    // full width, less on a narrow page - and less the scroll bar when the body
    // scrolls, since the bar takes its width from the body (PR #37's review,
    // finding #3).
    int contentWidth() const;

    // Called by placeSheet() when contentWidth() has changed: the page narrowed or
    // widened, or the body began or stopped scrolling. A sheet that fits its
    // content to the width - the history's headings - fits it again here.
    virtual void contentWidthChanged() {}

    // Scrolls the sheet's body until `widget`, inside it, is in view: nothing,
    // when the whole sheet fits the page.
    void reveal(QWidget* widget);

    // KEEPS THE READER'S PLACE AS THE BODY GROWS, for a sheet whose rows arrive while
    // it is open (the history): one at the foot is kept there, so the new rows come
    // into view, and one scrolled up stays where they are (PR #37's review, finding
    // #4). Called once, by a sheet that wants it.
    void keepFootInView();

    // The dim, then the panel round the sheet.
    void paintEvent(QPaintEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

    // SWALLOWED, and not left to the default. QWidget::mousePressEvent() ignores
    // the event, which propagates to the PARENT rather than to the widgets
    // underneath - so the table would not be clicked either way. Accepting it
    // says so on purpose, and stops a press on the dim from reaching the window.
    void mousePressEvent(QMouseEvent* event) override;

    // The parent's resizes, which this follows, and the sheet's content changing
    // size.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // The sheet's width, its border included: 720, or the page less its margins.
    int sheetWidth() const;

    Theme palette = Theme::dark();

    QScrollArea* scroll = nullptr;
    QWidget* plaque = nullptr;
    QVBoxLayout* column = nullptr;

    // Whether the body scrolls, as placeSheet() last decided; and the content width
    // it last reported to contentWidthChanged().
    bool bodyScrolls = false;
    int reportedWidth = -1;

    // Whether the body was last left at its foot - or does not scroll at all
    // (keepFootInView()).
    bool atFoot = true;

    // The panel's picture, with its drop shadow: the overlay repaints with every
    // frame of the table under a sheet that does not hold the game, and the two
    // blurred shadows are too dear to paint each time.
    mutable panel::Cache chrome;
};

#endif
