#include "Overlay.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFontMetricsF>
#include <QGridLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QScrollArea>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{

// page.html's .sheet h2, .meta, ul, .note and .label.
constexpr int titlePixels = 32;
constexpr int metaPixels = 17;
constexpr int bodyPixels = 17;
constexpr int notePixels = 15;
constexpr int labelPixels = 13;
constexpr qreal labelSpacing = 0.12;

// .sheet .actions: 12 between the buttons and 20 above them. A button is Marcellus
// 19 with padding 10 20 inside a border of 1, rounded 8.
constexpr int actionGap = 12;
constexpr int actionsAbove = 20;
constexpr int buttonPixels = 19;
constexpr int buttonPaddingY = 10;
constexpr int buttonPaddingX = 20;
constexpr qreal buttonBorder = 1.0;
constexpr qreal buttonRadius = 8.0;

// .sheet table: 18 with tabular figures, headings 13 in bold capitals spaced 0.1em;
// a cell padded 7 8 and a heading 6 8, each over a rule 1 deep.
constexpr int cellPixels = 18;
constexpr int headingPixels = 13;
constexpr qreal headingSpacing = 0.1;
constexpr int cellPaddingY = 7;
constexpr int headingPaddingY = 6;
constexpr int cellPaddingX = 8;
constexpr int ruleDepth = 1;

// Where a label keeps its role, so a new theme can colour it again.
constexpr const char* roleProperty = "sheet.role";

QFont buttonFont()
{
    QFont font = fonts::heading();
    font.setPixelSize(buttonPixels);
    return font;
}

QFont headingFont()
{
    QFont font = fonts::body();
    font.setPixelSize(headingPixels);
    font.setWeight(QFont::Bold);
    return fonts::capitals(font, headingSpacing);
}

QFont cellFont(bool strong)
{
    QFont font = fonts::tabular(fonts::body());
    font.setPixelSize(cellPixels);

    if(strong)
        font.setWeight(QFont::Bold);

    return font;
}

void setTextColour(QWidget* widget, const QColor& colour)
{
    QPalette scheme = widget->palette();
    scheme.setColor(QPalette::WindowText, colour);
    scheme.setColor(QPalette::Text, colour);
    widget->setPalette(scheme);
}

QFont noteFont()
{
    QFont font = fonts::tabular(fonts::body());
    font.setPixelSize(notePixels);
    return font;
}

// A TABLE'S CELL: its text and, when it has one, a note after it - .note's smaller
// muted type on the text's own baseline, as page.html's inline <span class="note">
// sits beside the score in the history's "+6 1/1".
//
// A QLabel STILL, so a cell reads as one and its text() is the text alone; and two
// parts painted on one baseline rather than two labels side by side, which a box
// layout would centre each on its own and leave a pixel apart. NOT rich text: a
// cell's words stay plain (PR #36's review, finding #5), and the note's colour is
// its own whatever tone the text takes.
class CellLabel : public QLabel
{
public:
    CellLabel(const QString& text, QWidget* parent) : QLabel(text, parent) {}

    const QString& note() const { return noteText; }

    void setNote(const QString& text)
    {
        noteText = text;
        updateGeometry();
        update();
    }

    void setNoteColour(const QColor& colour)
    {
        noteColour = colour;
        update();
    }

    QSize sizeHint() const override { return widened(QLabel::sizeHint()); }
    QSize minimumSizeHint() const override { return widened(QLabel::minimumSizeHint()); }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        if(noteText.isEmpty())
        {
            QLabel::paintEvent(event);
            return;
        }

        QPainter painter(this);

        const QRectF box = contentsRect();
        const QFont small = noteFont();
        const QFontMetricsF main(font());
        const QFontMetricsF side(small);

        const qreal across = main.horizontalAdvance(text()) + gap() + side.horizontalAdvance(noteText);

        qreal x = box.left();

        if(alignment() & Qt::AlignRight)
            x = box.right() - across;
        else if(alignment() & Qt::AlignHCenter)
            x = box.left() + (box.width() - across) / 2.0;

        // Centred down the cell as the label centres its text, and the note on the
        // same baseline.
        const qreal baseline = fonts::baselineIn(main, box.top(), box.height());

        painter.setFont(font());
        painter.setPen(palette().color(QPalette::WindowText));
        painter.drawText(QPointF(x, baseline), text());

        painter.setFont(small);
        painter.setPen(noteColour);
        painter.drawText(QPointF(x + main.horizontalAdvance(text()) + gap(), baseline), noteText);
    }

private:
    // The space between the two, which the mock-up writes as one: a space of the
    // text's font.
    qreal gap() const { return QFontMetricsF(font()).horizontalAdvance(QLatin1Char(' ')); }

    QSize widened(QSize size) const
    {
        if(noteText.isEmpty())
            return size;

        const qreal extra = gap() + QFontMetricsF(noteFont()).horizontalAdvance(noteText);
        size.rwidth() += static_cast<int>(std::ceil(extra));
        return size;
    }

    QString noteText;
    QColor noteColour;
};

}

namespace
{

// Every layout under `layout`, and every item in them, told to measure again.
void invalidateLayouts(QLayout* layout)
{
    layout->invalidate();

    for(int i = 0 ; i < layout->count() ; i++)
    {
        QLayoutItem* item = layout->itemAt(i);
        item->invalidate();

        if(QLayout* inner = item->layout())
            invalidateLayouts(inner);
    }
}

}

// ---- sheet:: ----

QFont sheet::fontFor(Role role)
{
    switch(role)
    {
    case Role::Title:
    {
        QFont font = fonts::heading();
        font.setPixelSize(titlePixels);
        return font;
    }

    case Role::Meta:
    {
        QFont font = fonts::body();
        font.setPixelSize(metaPixels);
        return font;
    }

    case Role::Body:
    {
        QFont font = fonts::body();
        font.setPixelSize(bodyPixels);
        return font;
    }

    case Role::Note:
    {
        QFont font = fonts::body();
        font.setPixelSize(notePixels);
        return font;
    }

    case Role::Label:
    {
        QFont font = fonts::body();
        font.setPixelSize(labelPixels);
        font.setWeight(QFont::Bold);
        return fonts::capitals(font, labelSpacing);
    }
    }

    return fonts::body();
}

QColor sheet::colourFor(Role role, const Theme::TablePalette& palette)
{
    switch(role)
    {
    case Role::Title:
    case Role::Body:
        return palette.cream;

    case Role::Meta:
    case Role::Note:
    case Role::Label:
        return palette.muted;
    }

    return palette.cream;
}

QString sheet::withSuits(const QString& text, const Theme::TablePalette& palette)
{
    const QString red = palette.redSuit.name(QColor::HexRgb);
    QString rich;

    for(const QChar c : text)
    {
        // ♥ and ♦ in red; ♠ and ♣ in the text's own cream, as textruns has them.
        if(c == QChar(0x2665) || c == QChar(0x2666))
            rich += QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(red, QString(c));
        else
            rich += QString(c).toHtmlEscaped();
    }

    return rich;
}

QString sheet::number(int value)
{
    // Through a long long, so the most negative int has a magnitude to print.
    if(value < 0)
        return QStringLiteral("\u2212%1").arg(-static_cast<long long>(value));

    return QString::number(value);
}

// ---- SheetButton ----

SheetButton::SheetButton(const QString& text, Kind kind, QWidget* parent)
    : LitButton(text, parent), buttonKind(kind), colours(Theme::dark().table)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void SheetButton::setKind(Kind kind)
{
    if(buttonKind == kind)
        return;

    buttonKind = kind;
    update();
}

void SheetButton::setColours(const Theme::TablePalette& palette)
{
    colours = palette;
    update();
}

QSize SheetButton::sizeHint() const
{
    const QFontMetricsF metrics(buttonFont());

    const qreal across = metrics.horizontalAdvance(text()) + 2.0 * (buttonPaddingX + buttonBorder);
    const qreal down = metrics.lineSpacing() + 2.0 * (buttonPaddingY + buttonBorder);

    return QSize(static_cast<int>(std::ceil(across)), static_cast<int>(std::ceil(down)));
}

QSize SheetButton::minimumSizeHint() const
{
    return sizeHint();
}

void SheetButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF box(rect());
    const QRectF padding = box.adjusted(buttonBorder, buttonBorder, -buttonBorder, -buttonBorder);
    const bool lit = isLit();

    // The background over the padding box, under the border, as CSS paints one: the
    // buttons' green from top to bottom, or - quiet - nothing at all.
    if(buttonKind == Kind::Primary)
    {
        QLinearGradient green(padding.topLeft(), padding.bottomLeft());
        green.setColorAt(0.0, colours.buttonTop);
        green.setColorAt(1.0, colours.buttonBottom);
        painter.fillPath(paths::rounded(box, buttonRadius), green);
    }

    QColor border = colours.buttonBorder;
    QColor label = colours.cream;

    if(!isEnabled())
        border = label = colours.muted;
    else if(lit)
        border = label = colours.goldBright;

    painter.fillPath(paths::roundedBand(box, buttonRadius, padding, buttonRadius - buttonBorder), border);

    const QFont font = buttonFont();
    const QFontMetricsF metrics(font);
    const qreal line = metrics.lineSpacing();

    painter.setFont(font);
    painter.setPen(label);
    painter.drawText(QPointF((box.width() - metrics.horizontalAdvance(text())) / 2.0,
                             fonts::baselineIn(metrics, (box.height() - line) / 2.0, line)),
                     text());
}

// ---- SheetTable ----

SheetTable::SheetTable(QWidget* parent) : QWidget(parent), colours(Theme::dark().table)
{
    grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
}

void SheetTable::setColumns(std::vector<Column> given)
{
    columns = std::move(given);

    // Every column shares what the sheet has to spare, as a table 100% wide shares
    // it out in CSS.
    for(int c = 0 ; c < grid->columnCount() ; c++)
        grid->setColumnStretch(c, 0);

    for(std::size_t c = 0 ; c < columns.size() ; c++)
        grid->setColumnStretch(static_cast<int>(c), 1);

    clearRows();
}

void SheetTable::clearRows()
{
    // EVERYTHING, HEADINGS AND ALL, and then the headings again: keeping row 0 and
    // removing the rows under it needs the grid walked by position, and the first
    // version of that, in the round sheet, read a widget it had just deleted to
    // find its index.
    //
    // delete, NOT deleteLater(). The round sheet rebuilds its rows up to thirty-two
    // times a game, and a label taken out of the grid but not yet destroyed is
    // still a child here at its last geometry - so it goes on PAINTING until the
    // event loop gets round to the deferred delete, and round two is drawn over
    // round one. Safe, because these are labels that emit nothing, and nothing
    // that runs this is a slot of one of them.
    while(QLayoutItem* item = grid->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    headings.clear();
    rows.clear();
    tones.clear();

    for(std::size_t c = 0 ; c < columns.size() ; c++)
        headings.push_back(addLabel(columns[c].heading, 0, static_cast<int>(c), true));

    update();
}

void SheetTable::addRow(const std::vector<Cell>& cells)
{
    // COUNTED HERE RATHER THAN ASKED OF THE GRID. QGridLayout::rowCount() reports
    // the rows it has ever ALLOCATED and never shrinks, so a table filled a second
    // time would start writing below an empty one.
    const int row = static_cast<int>(rows.size()) + 1;

    std::vector<QLabel*> labels;
    std::vector<Tone> shades;

    for(std::size_t c = 0 ; c < columns.size() ; c++)
    {
        const Cell given = c < cells.size() ? cells[c] : Cell{};

        QLabel* label = addLabel(given.text, row, static_cast<int>(c), false);
        label->setFont(cellFont(given.strong));
        colour(label, given.tone);

        if(!given.note.isEmpty())
        {
            CellLabel* noted = static_cast<CellLabel*>(label);
            noted->setNote(given.note);
            noted->setNoteColour(colours.muted);
        }

        labels.push_back(label);
        shades.push_back(given.tone);
    }

    rows.push_back(std::move(labels));
    tones.push_back(std::move(shades));

    update();
}

void SheetTable::addNote(const QString& text)
{
    if(columns.empty())
        return;

    const int row = static_cast<int>(rows.size()) + 1;

    // Laid in column 0 and then stretched across them all, so the grid sizes no
    // column by it: a sentence would otherwise widen the first column to its own
    // length.
    QLabel* label = addLabel(text, row, 0, false);
    grid->removeWidget(label);
    grid->addWidget(label, row, 0, 1, std::max<int>(1, static_cast<int>(columns.size())));

    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setFont(noteFont());
    colour(label, Tone::Muted);

    rows.push_back({ label });
    tones.push_back({ Tone::Muted });

    update();
}

void SheetTable::fitHeadingsTo(int width)
{
    // WHAT EACH COLUMN NEEDS AS IT STANDS: its widest label, heading or cell. A
    // note row spans every column and asks for nothing of any one.
    std::vector<int> widest(columns.size(), 0);

    for(std::size_t c = 0 ; c < columns.size() ; c++)
    {
        widest[c] = headings[c]->sizeHint().width();

        for(const std::vector<QLabel*>& row : rows)
        {
            if(row.size() == columns.size())
                widest[c] = std::max(widest[c], row[c]->sizeHint().width());
        }
    }

    int fixed = 0;
    int shared = 0;

    for(std::size_t c = 0 ; c < columns.size() ; c++)
    {
        if(columns[c].elide)
            shared++;
        else
            fixed += widest[c];
    }

    if(shared == 0)
        return;

    const int share = (width - fixed) / shared;
    const QFontMetrics metrics(headingFont());

    for(std::size_t c = 0 ; c < columns.size() ; c++)
    {
        if(!columns[c].elide)
            continue;

        QLabel* label = headings[c];
        const QString whole = columns[c].heading;

        // The heading's own room, less its padding - and never less than its
        // column's cells need, which do not shorten.
        int room = share - 2 * cellPaddingX;

        for(const std::vector<QLabel*>& row : rows)
        {
            if(row.size() == columns.size())
                room = std::max(room, row[c]->sizeHint().width() - 2 * cellPaddingX);
        }

        // MEASURED IN THE HEADING'S OWN FONT, its capitals and spacing included,
        // which QFontMetrics lays out as the label does.
        const QString shown = metrics.elidedText(whole, Qt::ElideRight, std::max(0, room));

        label->setText(shown);
        label->setToolTip(shown == whole ? QString() : whole);
    }
}

QLabel* SheetTable::cell(int row, int column) const
{
    if(row < 0 || row >= rowCount() || column < 0)
        return nullptr;

    const std::vector<QLabel*>& cells = rows[static_cast<std::size_t>(row)];

    if(column >= static_cast<int>(cells.size()))
        return nullptr;

    return cells[static_cast<std::size_t>(column)];
}

QString SheetTable::note(int row, int column) const
{
    const CellLabel* label = dynamic_cast<const CellLabel*>(cell(row, column));
    return label ? label->note() : QString();
}

QLabel* SheetTable::heading(int column) const
{
    if(column < 0 || column >= static_cast<int>(headings.size()))
        return nullptr;

    return headings[static_cast<std::size_t>(column)];
}

void SheetTable::setColours(const Theme::TablePalette& palette)
{
    colours = palette;

    for(QLabel* heading : headings)
        setTextColour(heading, colours.muted);

    for(std::size_t r = 0 ; r < rows.size() ; r++)
    {
        for(std::size_t c = 0 ; c < rows[r].size() ; c++)
        {
            colour(rows[r][c], tones[r][c]);

            if(CellLabel* noted = dynamic_cast<CellLabel*>(rows[r][c]))
                noted->setNoteColour(colours.muted);
        }
    }

    update();
}

QLabel* SheetTable::addLabel(const QString& text, int row, int column, bool heading)
{
    // A heading is a plain label; a cell may carry a note after its text.
    QLabel* label = heading ? new QLabel(text, this) : new CellLabel(text, this);

    // PLAIN, because a cell holds a player's name as they typed it, and Qt's
    // AutoText would read "<b>Dan</b>" as markup (PR #36's review, finding #5). A
    // sheet that wants rich text asks for it on its own label, as the round
    // sheet's line and the rules do.
    label->setTextFormat(Qt::PlainText);

    const Qt::Alignment across = columns[static_cast<std::size_t>(column)].alignment
                               & Qt::AlignHorizontal_Mask;

    label->setAlignment(across | Qt::AlignVCenter);

    // The cell's padding, and the rule under it, which is part of its box.
    const int paddingY = heading ? headingPaddingY : cellPaddingY;
    label->setContentsMargins(cellPaddingX, paddingY, cellPaddingX, paddingY + ruleDepth);

    if(heading)
    {
        label->setFont(headingFont());
        setTextColour(label, colours.muted);
    }

    grid->addWidget(label, row, column);
    return label;
}

void SheetTable::colour(QLabel* label, Tone tone)
{
    switch(tone)
    {
    case Tone::Plain:
        setTextColour(label, colours.cream);
        break;

    case Tone::Good:
        setTextColour(label, colours.good);
        break;

    case Tone::Bad:
        setTextColour(label, colours.bad);
        break;

    case Tone::Muted:
        setTextColour(label, colours.muted);
        break;
    }
}

void SheetTable::paintEvent(QPaintEvent*)
{
    if(columns.empty())
        return;

    QPainter painter(this);

    // A RULE ALONG THE FOOT OF EVERY ROW, across the whole table: brighter under the
    // headings than under the rows.
    const int last = static_cast<int>(rows.size());

    for(int row = 0 ; row <= last ; row++)
    {
        const QRect cell = grid->cellRect(row, 0);

        if(!cell.isValid())
            continue;

        painter.fillRect(QRect(0, cell.bottom() + 1 - ruleDepth, width(), ruleDepth),
                         row == 0 ? colours.sheetHeadRule : colours.sheetRowRule);
    }
}

// ---- Overlay ----

Overlay::Overlay(QWidget* parent) : QWidget(parent)
{
    // THE SHEET'S BODY SCROLLS when the page is too short for it, as the mock-up's
    // .sheet does past its max-height. Transparent, frameless and never a Tab stop
    // itself: the panel round it is this widget's to paint, and the keyboard moves
    // between the sheet's controls, not onto their container.
    scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidgetResizable(true);
    scroll->setFocusPolicy(Qt::NoFocus);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);

    plaque = new QWidget;

    column = new QVBoxLayout(plaque);
    column->setContentsMargins(sheet::border + sheet::paddingX, sheet::border + sheet::paddingTop,
                               sheet::border + sheet::paddingX, sheet::border + sheet::paddingBottom);
    column->setSpacing(0);

    scroll->setWidget(plaque);

    // QScrollArea::setWidget() turns the widget's background on. The panel behind
    // it is painted here, so it goes off again.
    plaque->setAutoFillBackground(false);

    // Its content growing or shrinking resizes the sheet.
    plaque->installEventFilter(this);

    // Hidden or shown, so that it always opens at the page's size.
    if(parent)
    {
        setGeometry(parent->rect());
        parent->installEventFilter(this);
    }

    setTheme(palette);
}

void Overlay::setTheme(const Theme& theme)
{
    palette = theme;

    const Theme::TablePalette& table = palette.table;

    // A DARK PALETTE FOR WHAT Qt's STYLE DRAWS ON THE SHEET - Settings' slider and
    // box, a scroll bar - through the palette rather than a stylesheet: section
    // 18.1's rule is that colours come from Theme, and a stylesheet string is a
    // colour constructed somewhere else.
    QPalette scheme = QWidget::palette();

    // WINDOW IS THE PANEL'S BORDER, not its fill: nothing here fills with it (the
    // plaque is transparent), and Fusion draws a control's outline in it, darkened
    // - from the panel's own near-black, the checkbox's box vanished into the panel.
    for(const QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive })
    {
        scheme.setColor(group, QPalette::Window, table.panelBorder);
        scheme.setColor(group, QPalette::WindowText, table.cream);
        scheme.setColor(group, QPalette::Base, table.panelBottom);
        scheme.setColor(group, QPalette::AlternateBase, table.panelTop);
        scheme.setColor(group, QPalette::Text, table.cream);
        scheme.setColor(group, QPalette::Button, table.panelTop);
        scheme.setColor(group, QPalette::ButtonText, table.cream);
        scheme.setColor(group, QPalette::Highlight, table.gold);
        scheme.setColor(group, QPalette::HighlightedText, table.panelBottom);
        scheme.setColor(group, QPalette::Light, table.panelBorder);
        scheme.setColor(group, QPalette::Midlight, table.panelBorder);
        scheme.setColor(group, QPalette::Mid, table.panelBorder);
        scheme.setColor(group, QPalette::Dark, table.panelInnerShadow);
        scheme.setColor(group, QPalette::Shadow, table.panelInnerShadow);
    }

    scheme.setColor(QPalette::Disabled, QPalette::WindowText, table.muted);
    scheme.setColor(QPalette::Disabled, QPalette::Text, table.muted);
    scheme.setColor(QPalette::Disabled, QPalette::ButtonText, table.muted);

    setPalette(scheme);

    // And everything this draws itself, again in the new colours.
    for(QLabel* label : findChildren<QLabel*>())
    {
        const QVariant role = label->property(roleProperty);

        if(role.isValid())
            setTextColour(label, sheet::colourFor(static_cast<sheet::Role>(role.toInt()), table));
    }

    for(SheetButton* button : findChildren<SheetButton*>())
        button->setColours(table);

    for(SheetTable* table : findChildren<SheetTable*>())
        table->setColours(palette.table);

    update();
}

void Overlay::dress(QLabel* label, sheet::Role role)
{
    label->setProperty(roleProperty, static_cast<int>(role));
    label->setFont(sheet::fontFor(role));
    label->setWordWrap(true);

    // PLAIN TEXT, unless the line has asked for rich text itself - the round
    // sheet's trump line and the rules do, and escape what they put in. A QLabel's
    // default AutoText reads anything with a tag on its first line as markup, and
    // the sheets' lines carry player names: "Ana and <b>Dan</b> tie for first..."
    // drew in bold, and so did a streak warning (PR #36's review, finding #5, which
    // first covered only the tables' cells).
    if(label->textFormat() == Qt::AutoText)
        label->setTextFormat(Qt::PlainText);

    setTextColour(label, sheet::colourFor(role, palette.table));
}

void Overlay::addActions(std::initializer_list<SheetButton*> buttons)
{
    column->addSpacing(actionsAbove);

    QHBoxLayout* actions = new QHBoxLayout;
    actions->setSpacing(actionGap);
    actions->addStretch(1);

    for(SheetButton* button : buttons)
    {
        button->setColours(palette.table);
        actions->addWidget(button);
    }

    column->addLayout(actions);
}

QRect Overlay::sheetRect() const
{
    return scroll->geometry();
}

void Overlay::placeSheet()
{
    const int room = std::max(0, width() - 2 * sheet::pageMargin);
    const int across = std::min(sheet::width, room);

    // EVERY LAYOUT IN THE SHEET FORGETS WHAT IT MEASURED, the nested ones included.
    // A label's new text invalidates the layout it sits in, but a box layout keeps
    // the height it last worked out for a width, and a row inside the column -
    // Rules' list - went on answering with the height it had measured empty: the
    // window lays a sheet out at start-up, and the rules arrive when Rules is first
    // pressed. The sheet opened as a sliver with a scroll bar (found in the running
    // app, phase 10).
    invalidateLayouts(column);

    int wanted = plaque->hasHeightForWidth() ? plaque->heightForWidth(across) : -1;

    if(wanted <= 0)
        wanted = plaque->sizeHint().height();

    const int down = std::min(wanted, std::max(0, height() - 2 * sheet::pageMargin));

    scroll->setGeometry((width() - across) / 2, (height() - down) / 2, across, down);

    update();
}

void Overlay::reveal(QWidget* widget)
{
    // LAID OUT FIRST. A sheet just shown has posted its layout, not run it, so its
    // widgets are still where they were last placed - nowhere, the first time -
    // and there would be nothing to scroll to.
    column->activate();

    scroll->ensureWidgetVisible(widget);
}

bool Overlay::focusNextInside(bool next)
{
    // The controls a Tab can stop on, in the window's own Tab order: the chain
    // is a loop through the whole window, so it comes back round to this.
    QList<QWidget*> stops;

    for(QWidget* widget = nextInFocusChain() ; widget != this ; widget = widget->nextInFocusChain())
    {
        if(isAncestorOf(widget) && widget->isVisible() && widget->isEnabled() &&
           (widget->focusPolicy() & Qt::TabFocus) == Qt::TabFocus)
        {
            stops.append(widget);
        }
    }

    if(stops.isEmpty())
        return false;

    // window()->focusWidget() rather than QApplication::focusWidget(), which is
    // null whenever the window is not the active one.
    const qsizetype at = stops.indexOf(window()->focusWidget());
    const qsizetype count = stops.size();

    qsizetype to = next ? 0 : count - 1;

    if(at >= 0)
        to = (at + (next ? 1 : count - 1)) % count;

    // A TAB THAT LANDS WHERE THE KEYBOARD ALREADY IS - a sheet with one button,
    // whose default it is - still shows it there: Qt sends no focus event to the
    // widget that has it, so it is let go and taken again, and the button lights
    // as a Tab lights it (LitButton::isLit()).
    if(stops[to]->hasFocus())
        stops[to]->clearFocus();

    stops[to]->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);

    // Into view, when the sheet's body scrolls.
    scroll->ensureWidgetVisible(stops[to]);

    return true;
}

void Overlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), palette.table.scrim);

    // THE PANEL ROUND THE SHEET, section 2.2's with its drop shadow, at the pixels it
    // lands on.
    const QRect frame = scroll->geometry();
    const panel::Picture made = chrome.picture(QSizeF(frame.size()), palette.table, panel::State::Plain,
                                               devicePixelRatioF());

    if(!made.pixmap.isNull())
        painter.drawPixmap(devicepixels::snapped(painter, QPointF(frame.topLeft()) + made.offset), made.pixmap);

    QWidget::paintEvent(event);
}

void Overlay::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    placeSheet();
}

void Overlay::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // MEASURED AGAIN NOW THAT IT SHOWS. A sheet the window has kept hidden since it
    // was built has never shown its widgets, and a layout counts a widget that has
    // never been shown as nothing: filled while hidden, the history measured 194
    // tall for thirty rows. Its layout put that right a moment later, but a sheet
    // opened at its foot (reveal()) had already scrolled by the wrong size.
    placeSheet();
}

void Overlay::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}

bool Overlay::eventFilter(QObject* watched, QEvent* event)
{
    if(watched == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());

    // The sheet's words or rows changed, so its height may have.
    if(watched == plaque && event->type() == QEvent::LayoutRequest)
        placeSheet();

    return QWidget::eventFilter(watched, event);
}
