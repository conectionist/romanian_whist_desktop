#include "PromptBar.h"

#include <QBoxLayout>
#include <QLabel>
#include <QPalette>

#include <algorithm>

using romanian_whist::Suit;

namespace
{
// THE SYMBOL RATHER THAN THE WORD, which is section 15.2's own spelling: "You
// must follow the ace of spades" is not what a card player says, and a symbol
// needs no translation and no third switch over Suit - CardRenderer already has
// one, and its answers are FILENAMES rather than labels, which is section 7's
// documented exception.
//
// Section 20's accessibility pass is where this is revisited if a symbol turns
// out not to reach a screen reader.
QString symbolFor(Suit suit)
{
    switch(suit)
    {
        case Suit::Hearts:   return QStringLiteral("♥");
        case Suit::Diamonds: return QStringLiteral("♦");
        case Suit::Spades:   return QStringLiteral("♠");
        case Suit::Clubs:    return QStringLiteral("♣");
    }

    return QString();
}
}

promptbar::Fields promptbar::idle()
{
    return Fields{};
}

promptbar::Fields promptbar::fieldsFor(const PlayPrompt& prompt, const GameSnapshot& snapshot)
{
    Fields fields;

    // A BLIND ROUND HAS NOTHING TO EXPLAIN. There is one card, it is face down,
    // and it is forced - so a bar saying "you must follow" would be describing a
    // choice the player does not have.
    if(prompt.blind)
        return fields;

    // RESTRICTED MEANS AN ILLEGAL CARD IN THE HAND, asked of the entries rather
    // than inferred from the lead suit. The two usually agree and come apart in
    // exactly the case that matters: a player VOID in the lead suit may play
    // anything, so hasLeadSuit is true and nothing is restricted - and a bar
    // driven off the snapshot alone would tell them to follow a suit they do not
    // hold.
    const bool restricted = std::any_of(prompt.entries.begin(), prompt.entries.end(),
                                        [](const HandEntry& entry){ return !entry.legal; });

    if(restricted && snapshot.hasLeadSuit)
    {
        fields.hint = QCoreApplication::translate("promptbar", "You must follow %1.")
                          .arg(symbolFor(snapshot.leadSuit));

        return fields;
    }

    // LEADING IS THE OTHER THING WORTH SAYING, and it is not the same as "not
    // restricted": a player void in the lead suit is unrestricted and is not
    // leading. hasLeadSuit is what tells them apart, and it is false only when
    // nothing has been played to this trick.
    if(!snapshot.hasLeadSuit)
        fields.hint = QCoreApplication::translate("promptbar", "You lead.");

    return fields;
}

PromptBar::PromptBar(QWidget* parent) : QWidget(parent)
{
    hint = new QLabel(this);

    auto* row = new QHBoxLayout(this);
    row->addWidget(hint);
    row->addStretch(1);

    // THE HEIGHT IS SETTLED NOW AND NEVER AGAIN, which is B2b's finding applied
    // rather than rediscovered. This bar is empty for most of a game, and a bar
    // that collapsed when it had nothing to say would move the table above it on
    // every turn and back on every play - twice a trick, where the trump label
    // managed it only twice a game.
    //
    // From the font, so it follows a user who has set a larger one.
    setFixedHeight(fontMetrics().height() + row->contentsMargins().top()
                                          + row->contentsMargins().bottom());

    setFields(promptbar::idle());
}

void PromptBar::setFields(const promptbar::Fields& fields)
{
    // Set even when empty, which is the point: an empty string in a bar of fixed
    // height is a blank line where the writing goes, and that is what makes the
    // place worth learning.
    hint->setText(fields.hint);
}

void PromptBar::setTheme(const Theme& palette)
{
    theme = palette;

    // Through the palette rather than a stylesheet, the call InfoStrip and
    // BidStrip both make: section 18.1's rule is that colours come from Theme.
    QPalette colours = QWidget::palette();
    colours.setColor(QPalette::Window, theme.seatPlaque);
    colours.setColor(QPalette::WindowText, theme.textPrimary);

    setPalette(colours);
    setAutoFillBackground(true);
}
