#include "Overlay.h"

#include "paint/DevicePixels.h"
#include "paint/Fonts.h"
#include "paint/Paths.h"

#include <QBoxLayout>
#include <QEvent>
#include <QFocusEvent>
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

// ---- SheetButton ----

SheetButton::SheetButton(const QString& text, Kind kind, QWidget* parent)
    : QPushButton(text, parent), buttonKind(kind), colours(Theme::dark().table)
{
    // Repainted as the cursor comes and goes, which is all the lit look needs.
    setAttribute(Qt::WA_Hover);

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

bool SheetButton::isLit() const
{
    return isEnabled() && (underMouse() || isDown() || (tabbedTo && hasFocus()));
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

void SheetButton::focusInEvent(QFocusEvent* event)
{
    tabbedTo = event->reason() == Qt::TabFocusReason || event->reason() == Qt::BacktabFocusReason;
    update();

    QPushButton::focusInEvent(event);
}

void SheetButton::focusOutEvent(QFocusEvent* event)
{
    tabbedTo = false;
    update();

    QPushButton::focusOutEvent(event);
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

        labels.push_back(label);
        shades.push_back(given.tone);
    }

    rows.push_back(std::move(labels));
    tones.push_back(std::move(shades));

    update();
}

QLabel* SheetTable::cell(int row, int column) const
{
    if(row < 0 || row >= rowCount() || column < 0 || column >= static_cast<int>(columns.size()))
        return nullptr;

    return rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
}

void SheetTable::setColours(const Theme::TablePalette& palette)
{
    colours = palette;

    for(QLabel* heading : headings)
        setTextColour(heading, colours.muted);

    for(std::size_t r = 0 ; r < rows.size() ; r++)
    {
        for(std::size_t c = 0 ; c < rows[r].size() ; c++)
            colour(rows[r][c], tones[r][c]);
    }

    update();
}

QLabel* SheetTable::addLabel(const QString& text, int row, int column, bool heading)
{
    QLabel* label = new QLabel(text, this);

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
    // as a Tab lights it (SheetButton::isLit()).
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
