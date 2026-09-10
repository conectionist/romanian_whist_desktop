#include "RoundSheet.h"

#include "InfoStrip.h"
#include "ScoreboardWidget.h"

#include <QBoxLayout>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>

namespace
{
// The streak the engine pays out on, and the round before it is the one worth
// warning about. docs/RULES.md: +10 on the fifth consecutive exact bid, -10 on
// the fifth consecutive miss.
constexpr unsigned int payoutStreak = 5;
constexpr unsigned int warnAt = payoutStreak - 1;
}

QString roundsheet::titleFor(const RoundResult& result)
{
    // THE CARD COUNT AND THE ROUND TYPE THROUGH infostrip's OWN FUNCTIONS, not
    // second copies of either. The three words are already on screen across the top of the table
    // all round, and two spellings of Forehead is exactly the drift section 7
    // warns about - one of them would be the one lupdate collects.
    //
    // One arg() taking three rather than three chained: a player cannot reach
    // this string, but the rule SnapshotLog.cpp states holds anyway - arg() fills
    // the lowest placeholder still standing, including one that arrived inside a
    // string it just substituted.
    return QCoreApplication::translate("roundsheet", "Round %1 - %2 - %3")
        .arg(QString::number(result.roundNumber),
             infostrip::cardsText(result.trickCount),
             infostrip::roundTypeText(result.roundType));
}

QString roundsheet::arrowFor(const RoundSeatResult& seat)
{
    // NOT A SUM DONE UPSTREAM. The bridge carries the two numbers apart because
    // that is how the engine reports them inside onRoundScored(), and adding
    // them here is what makes the arrow show a before and an after rather than
    // one number twice.
    return QCoreApplication::translate("roundsheet", "%1 \xE2\x86\x92 %2")
        .arg(QString::number(seat.totalScore),
             QString::number(seat.totalScore + seat.roundScore));
}

QString roundsheet::roundScoreFor(const RoundSeatResult& seat)
{
    // THE SIGN IS THE CUE and the colour only makes it quick, which is section
    // 18's rule and the same round ScoreboardWidget takes. A negative number
    // already carries its own minus; a positive one has to be given its plus, or
    // the two columns read as unrelated kinds of number.
    return seat.roundScore > 0 ? QStringLiteral("+%1").arg(seat.roundScore)
                               : QString::number(seat.roundScore);
}

QStringList roundsheet::streakWarnings(const RoundResult& result)
{
    // SILENT WHEN THE NEXT ROUND CANNOT COUNT, and this is the first thing
    // checked rather than the last, because it makes every warning below true or
    // none of them. A one-card round neither advances a streak nor breaks it, so
    // there is no fifth to reach and nothing to warn about.
    //
    // Also false on the final round, where there is no next round at all.
    if(!result.nextRoundCountsForStreaks)
        return {};

    QStringList lines;

    // EVERY SEAT ON FOUR, and the loop runs to the end rather than returning
    // from inside it - see the header for what returning early cost. Table
    // order, which is the order the rows above are drawn in.
    for(const RoundSeatResult& seat : result.seats)
    {
        // EXACTLY four, in either direction. Five has already paid out and three
        // is too early to be worth a line - and the two counters are never both
        // engaged, because a round that advances one resets the other, so the
        // order of these two within a seat decides nothing.
        if(seat.lossStreak == warnAt)
            lines.append(QCoreApplication::translate(
                             "roundsheet", "%1 has missed four in a row - a fifth costs %2.")
                             .arg(QString::fromStdString(seat.name), QString::number(10)));

        if(seat.winStreak == warnAt)
            lines.append(QCoreApplication::translate(
                             "roundsheet", "%1 has hit four in a row - a fifth is worth %2.")
                             .arg(QString::fromStdString(seat.name), QString::number(10)));
    }

    return lines;
}

// ---- the widget ----

RoundSheet::RoundSheet(QWidget* parent) : QWidget(parent)
{
    panel = new QWidget(this);
    panel->setAutoFillBackground(true);

    QVBoxLayout* inside = new QVBoxLayout(panel);
    inside->setContentsMargins(40, 28, 40, 28);

    title = new QLabel(panel);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("roundTitle");

    QFont titleFont = title->font();
    titleFont.setBold(true);
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.2);
    title->setFont(titleFont);

    inside->addWidget(title);
    inside->addSpacing(16);

    // The grid is emptied and rebuilt per round, HEADINGS INCLUDED - see
    // clearRows(), and GameOverPanel, which reached the same arrangement.
    rows = new QWidget(panel);
    rowLayout = new QGridLayout(rows);
    rowLayout->setHorizontalSpacing(24);

    inside->addWidget(rows);
    inside->addSpacing(12);

    warning = new QLabel(panel);
    warning->setAlignment(Qt::AlignCenter);
    warning->setObjectName("streakWarning");
    warning->setWordWrap(true);
    warning->hide();

    inside->addWidget(warning);
    inside->addSpacing(12);

    continueButton = new QPushButton(
        QCoreApplication::translate("roundsheet", "Continue"), panel);
    continueButton->setObjectName("continueRound");
    continueButton->setDefault(true);
    continueButton->setMinimumWidth(180);
    continueButton->setMinimumHeight(32);

    QHBoxLayout* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);
    buttonRow->addWidget(continueButton);
    inside->addLayout(buttonRow);

    QVBoxLayout* stack = new QVBoxLayout(this);
    stack->addStretch(1);

    QHBoxLayout* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(panel);
    row->addStretch(1);

    stack->addLayout(row);
    stack->addStretch(1);

    connect(continueButton, &QPushButton::clicked, this, &RoundSheet::continueRequested);
}

void RoundSheet::setTheme(const Theme& _theme)
{
    theme = _theme;

    QPalette scheme = panel->palette();
    scheme.setColor(QPalette::Window, theme.seatPlaque);
    scheme.setColor(QPalette::WindowText, theme.textPrimary);
    panel->setPalette(scheme);

    update();
}

void RoundSheet::clearRows()
{
    // EVERYTHING, HEADINGS AND ALL, which is why they are not built in the
    // constructor. Keeping a persistent row 0 and removing only the seats below
    // it needs the layout walked by position, and the first version of that read
    // a widget it had just deleted to find its index. GameOverPanel takes the
    // whole grid out and rebuilds; three heading labels per round is nothing
    // beside getting that wrong.
    //
    // delete, NOT deleteLater(), and this is where the sheet parts company with
    // GameOverPanel - which uses the deferred form and is right to.
    //
    // THE DIFFERENCE IS HOW OFTEN THIS RUNS. That panel builds its rows once per
    // GAME; this one rebuilds them up to thirty-two times, and a widget taken
    // out of a layout but not yet destroyed is still a child of `rows` sitting
    // at its last geometry - so it goes on PAINTING until the event loop gets
    // round to the deferred delete. Round two would be drawn over round one.
    //
    // Caught by the test that shows two rounds and counts the labels: it found
    // two "Ana"s, which is exactly that ghost.
    //
    // Safe, because the usual reason for deleteLater() is absent: these are
    // QLabels that emit nothing, and this runs from a queued signal handler
    // rather than from a slot of one of the widgets being removed.
    while(QLayoutItem* item = rowLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    // COUNTED HERE RATHER THAN ASKED OF THE LAYOUT. QGridLayout::rowCount()
    // reports the rows it has ever ALLOCATED and never shrinks when items are
    // taken out, so a second round reading it back would start writing below an
    // empty grid.
    placedRows = 0;
}

void RoundSheet::addHeadings()
{
    // In the scoreboard's own words, through its own functions - the same four
    // columns the panel beside the table carries all round, so a player reads
    // one table rather than two vocabularies, and a translator sees one string
    // each rather than two spellings to keep in step.
    const auto heading = [this](const QString& text, int column, Qt::Alignment align)
    {
        QLabel* label = new QLabel(text, rows);
        label->setAlignment(align);

        QFont bold = label->font();
        bold.setBold(true);
        label->setFont(bold);

        rowLayout->addWidget(label, 0, column);
    };

    heading(QCoreApplication::translate("roundsheet", "Name"), 0, Qt::AlignLeft);
    heading(scoreboard::headingBid(), 1, Qt::AlignRight);
    heading(scoreboard::headingWon(), 2, Qt::AlignRight);
    heading(scoreboard::headingRound(), 3, Qt::AlignRight);
    heading(scoreboard::headingTotal(), 4, Qt::AlignRight);
}

void RoundSheet::addRow(const RoundSeatResult& seat)
{
    const int r = ++placedRows;

    const auto cell = [this, r](const QString& text, int column, Qt::Alignment align)
    {
        QLabel* label = new QLabel(text, rows);
        label->setAlignment(align);
        rowLayout->addWidget(label, r, column);
        return label;
    };

    // The human's row bold, which is what ScoreboardWidget does and for the same
    // reason: a table of four names is read by finding your own first.
    QLabel* name = cell(QString::fromStdString(seat.name), 0, Qt::AlignLeft);

    if(seat.isHuman)
    {
        QFont bold = name->font();
        bold.setBold(true);
        name->setFont(bold);
    }

    cell(seat.hasBet ? QString::number(seat.bet) : QStringLiteral("-"), 1, Qt::AlignRight);
    cell(QString::number(seat.tricksWon), 2, Qt::AlignRight);

    QLabel* round = cell(roundsheet::roundScoreFor(seat), 3, Qt::AlignRight);

    // THE COLOUR REINFORCES THE SIGN rather than replacing it (section 18), and
    // it comes from Theme like every other colour in the client (section 18.1).
    // Zero takes neither, because zero went neither way.
    if(seat.roundScore != 0)
    {
        QPalette scheme = round->palette();
        scheme.setColor(QPalette::WindowText,
                        seat.roundScore > 0 ? theme.scorePositive : theme.scoreNegative);
        round->setPalette(scheme);
    }

    cell(roundsheet::arrowFor(seat), 4, Qt::AlignRight);
}

void RoundSheet::showResult(const RoundResult& result)
{
    clearRows();
    addHeadings();

    title->setText(roundsheet::titleFor(result));

    for(const RoundSeatResult& seat : result.seats)
        addRow(seat);

    // ONE LABEL, NOT ONE PER LINE. The label already wraps and the panel is
    // sized to what it holds below, so joining costs no layout - and at most six
    // seats can be on four at once, which in practice is one and occasionally
    // two.
    const QStringList lines = roundsheet::streakWarnings(result);

    warning->setText(lines.join(QLatin1Char('\n')));
    warning->setVisible(!lines.isEmpty());

    // The panel is rebuilt per round and the seat count can differ between
    // games, so it is sized to what it now holds rather than to what it held.
    panel->adjustSize();
}

void RoundSheet::focusContinue()
{
    continueButton->setFocus(Qt::OtherFocusReason);
}

void RoundSheet::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme.dimmedOverlay);

    QWidget::paintEvent(event);
}

void RoundSheet::mousePressEvent(QMouseEvent* event)
{
    event->accept();
}
