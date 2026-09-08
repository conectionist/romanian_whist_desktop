#include "BidStrip.h"

#include <QBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPalette>
#include <QPushButton>

#include <cstddef>

bidstrip::Fields bidstrip::fieldsFor(const BetPrompt& prompt)
{
    Fields fields;

    fields.question = QCoreApplication::translate("bidstrip",
                                                  "How many tricks will you win?");

    // The hand's size IS the trick count and so the highest legal bid. Narrowed
    // here rather than carried as a size_t, because it is about to become a
    // number of buttons and a bid, and both are unsigned int - the type
    // IMoveProvider::makeBet() returns.
    fields.highest = static_cast<unsigned int>(prompt.handSize);

    fields.forbidden = prompt.forbiddenBet;

    // NO EXPLANATION WITHOUT A FORBIDDEN BID, and the empty string is what the
    // widget hides the label on. Most bidders have none: the engine engages it
    // for the final bidder only, and drops it once the bids so far already
    // exceed the trick count.
    if(!prompt.forbiddenBet)
        return fields;

    // ENGAGED MEANS YOU BID LAST. Nothing here infers turn order or counts what
    // others have bid - getForbiddenBet() is only ever filled for the seat that
    // closes the bidding, so the presence of the value is the fact, and section
    // 15.1's wording can say so outright.
    //
    // One arg() taking two rather than two chained, the rule SnapshotLog.cpp
    // states: arg() fills the lowest placeholder still standing, including one
    // that arrived inside a string it just substituted.
    fields.explanation =
        QCoreApplication::translate("bidstrip",
                                    "You bid last - %1 would make the bids add up to %2, "
                                    "and somebody has to be wrong.")
            .arg(QString::number(*prompt.forbiddenBet),
                 QString::number(fields.highest));

    return fields;
}

BidStrip::BidStrip(QWidget* parent) : QWidget(parent)
{
    question = new QLabel(this);
    question->setAlignment(Qt::AlignCenter);

    explanation = new QLabel(this);
    explanation->setAlignment(Qt::AlignCenter);

    // WRAPPED, because this sentence is long and the strip is only as wide as
    // its buttons. Left unwrapped it would set the strip's minimum width from a
    // sentence rather than from the bids, which is the widest thing on offer
    // deciding the layout instead of the thing being offered.
    explanation->setWordWrap(true);

    buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(6);

    auto* stack = new QVBoxLayout(this);
    stack->addWidget(question);
    stack->addLayout(buttonRow);
    stack->addWidget(explanation);

    // FOCUS SO THE DIGITS WORK. Without this the strip never receives a key
    // press and section 15.1's keyboard half is silently absent - the buttons
    // still work, so it looks finished.
    setFocusPolicy(Qt::StrongFocus);

    setTheme(theme);
}

void BidStrip::setTheme(const Theme& palette)
{
    theme = palette;

    // Through a palette rather than a stylesheet, the call InfoStrip makes and
    // for the same reason: section 18.1's rule is that colours come from Theme,
    // and a stylesheet would be a second place deciding what colour things are.
    QPalette colours = QWidget::palette();
    colours.setColor(QPalette::Window, theme.seatPlaque);
    colours.setColor(QPalette::WindowText, theme.textPrimary);

    setPalette(colours);

    // OPAQUE, unlike most widgets here, because this one floats over the table -
    // see TableView::setBottomOverlay(). Without it the green shows through and
    // the numbers are unreadable against a trick.
    setAutoFillBackground(true);
}

void BidStrip::setPrompt(const BetPrompt& prompt)
{
    fields = bidstrip::fieldsFor(prompt);
    rebuild();
}

void BidStrip::rebuild()
{
    // DELETED, not hidden. A round of one card must not leave the eight-card
    // round's buttons parked in the layout: hidden widgets still belong to it,
    // and the next prompt would append rather than replace.
    for(QPushButton* button : buttons)
    {
        buttonRow->removeWidget(button);
        delete button;
    }

    buttons.clear();

    question->setText(fields.question);

    explanation->setText(fields.explanation);

    // Hidden rather than emptied, so the strip does not reserve a line for a
    // sentence it is not showing. This widget is an overlay and appears only on
    // your turn, so a height that changes with the prompt costs nothing - which
    // is exactly what is NOT true of InfoStrip, whose height must not move
    // because the table is laid out beneath it.
    explanation->setVisible(!fields.explanation.isEmpty());

    // 0..highest INCLUSIVE. An 8-card round offers nine bids, and bidding zero
    // is a real choice rather than the absence of one.
    for(unsigned int bet = 0 ; bet <= fields.highest ; bet++)
    {
        auto* button = new QPushButton(QString::number(bet), this);

        // THE ONE THE ENGINE WOULD THROW ON, disabled before it is reached
        // rather than rejected after. QtMoveProvider::makeBet() re-prompts on a
        // bad bet rather than passing it to the engine, so the cost of missing
        // this is a strip that reappears rather than a crash - but a button that
        // does nothing when clicked is worse than one that is visibly not for
        // clicking.
        if(fields.forbidden && bet == *fields.forbidden)
            button->setEnabled(false);

        // BY VALUE INTO THE LAMBDA. `bet` is the loop's, and capturing it by
        // reference would leave every button reporting the last one.
        connect(button, &QPushButton::clicked, this, [this, bet]{ emit bidChosen(bet); });

        buttonRow->addWidget(button);
        buttons.push_back(button);
    }
}

void BidStrip::keyPressEvent(QKeyEvent* event)
{
    // UNMODIFIED DIGITS ONLY. A bid is irrevocable, so Ctrl+2 must not place
    // one: that chord belongs to whatever the window binds it to, and Phase E
    // owns the full keyboard map. The keypad's own modifier is masked back in
    // because keypad 2 is the same 2 to the person pressing it.
    if((event->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier)
    {
        QWidget::keyPressEvent(event);
        return;
    }

    const int digit = event->key() - Qt::Key_0;

    if(digit < 0 || static_cast<std::size_t>(digit) >= buttons.size())
    {
        // Not a digit this strip offers. Passed on rather than swallowed, so a
        // key the window binds still reaches it.
        QWidget::keyPressEvent(event);
        return;
    }

    QPushButton* button = buttons[static_cast<std::size_t>(digit)];

    // A DISABLED DIGIT IS IGNORED, which section 15.1 asks for by name. Reading
    // the button rather than re-testing `fields.forbidden` keeps one rule about
    // what is clickable rather than two that can disagree.
    if(!button->isEnabled())
        return;

    // click() rather than emitting directly, so the keyboard and the mouse go
    // through one path and the button visibly depresses.
    button->click();
}
