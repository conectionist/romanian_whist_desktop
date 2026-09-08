#include "InfoStrip.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QLabel>

#include <algorithm>

using romanian_whist::RoundType;

namespace
{

// How tall the trump card is, in text lines. Enough that the rank in the corner
// is readable, which at one line it is not.
constexpr qreal cardHeightInLines = 4.0;

// Space either side of the strip's contents, also in text lines, so the whole
// strip scales together with the font.
constexpr qreal marginInLines = 0.35;

}

QString infostrip::roundTypeText(RoundType type)
{
    // No default case, so a fourth round type is a warning here rather than a
    // silent "unknown" in front of a player. The fallback sits after the switch,
    // which is what Card::toString() and SnapshotLog both do.
    switch(type)
    {
        case RoundType::Normal:   return QCoreApplication::translate("infostrip", "Normal");
        case RoundType::Forehead: return QCoreApplication::translate("infostrip", "Forehead");
        case RoundType::Hidden:   return QCoreApplication::translate("infostrip", "Hidden");
    }

    return QCoreApplication::translate("infostrip", "Unknown");
}

infostrip::Fields infostrip::fieldsFor(const GameSnapshot& snapshot)
{
    Fields fields;

    // "of" rather than "7/24": this is a sentence a player reads once a round,
    // not a column in a log, and the slash is the log's idiom.
    //
    // One arg() taking both, never chained - the rule SnapshotLog.cpp and
    // MainWindow both carry, because a substituted value containing %2 would
    // otherwise swallow the next substitution. Numbers cannot do that, but the
    // habit is what keeps it from being decided case by case.
    fields.round = QCoreApplication::translate("infostrip", "Round %1 of %2")
                       .arg(snapshot.roundNumber)
                       .arg(snapshot.roundCount);

    // The trick count IS the number of cards dealt to each player - a round of
    // six cards is a round of six tricks - so this reads it rather than adding a
    // field that would have to agree with it.
    //
    // TWO STRINGS RATHER THAN Qt's "%n card(s)", which was tried and is wrong
    // here. Qt takes the plural FORMS from the catalogue, for the source
    // language as much as for any other - so with no translator loaded, which is
    // what an English build is, translate("%n card(s)", n) substitutes the count
    // and leaves the rest alone: "1 card(s)", on screen, in the shipped
    // language. Selecting here spells both forms correctly out of the box and
    // still gives lupdate two strings to collect. Languages whose plural rules
    // need more than two forms are the reason %n exists, and the day one is
    // added this becomes the thing to revisit.
    fields.cards = snapshot.trickCount == 1
                       ? QCoreApplication::translate("infostrip", "1 card")
                       : QCoreApplication::translate("infostrip", "%1 cards")
                             .arg(snapshot.trickCount);

    fields.roundType = roundTypeText(snapshot.roundType);

    fields.hasTrump = snapshot.hasTrump;

    // Copied ONLY under hasTrump. Left at Card's default otherwise, which is a
    // real two of clubs - see the header.
    if(snapshot.hasTrump)
        fields.trump = snapshot.trump;

    return fields;
}

InfoStrip::InfoStrip(QWidget* parent)
    : QWidget(parent)
{
    round = new QLabel(this);
    cardCount = new QLabel(this);
    roundType = new QLabel(this);
    trumpLabel = new QLabel(this);
    trumpCard = new QLabel(this);

    // The slot before anything is in it, so the strip is its full height from
    // the first paint and stays there - through the eight-card round, which has
    // no trump, and through the frames before any snapshot has arrived.
    trumpSlot = cards.sizeForHeight(trumpCardHeight());
    trumpCard->setFixedSize(trumpSlot);

    auto* layout = new QHBoxLayout(this);

    const qreal margin = QFontMetricsF(font()).height() * marginInLines;

    layout->setContentsMargins(qRound(margin * 2.0), qRound(margin),
                               qRound(margin * 2.0), qRound(margin));
    layout->setSpacing(qRound(margin * 3.0));

    layout->addWidget(round);
    layout->addWidget(cardCount);
    layout->addWidget(roundType);
    layout->addStretch();
    layout->addWidget(trumpLabel);
    layout->addWidget(trumpCard);

    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

qreal InfoStrip::trumpCardHeight() const
{
    return QFontMetricsF(font()).height() * cardHeightInLines;
}

void InfoStrip::setTheme(const Theme& palette)
{
    theme = palette;

    // Through a palette rather than a stylesheet: section 18.1's rule is that
    // colours come from Theme, and a stylesheet here would be a second place
    // that decides what colour text is.
    QPalette colours = QWidget::palette();
    colours.setColor(QPalette::Window, theme.seatPlaque);
    colours.setColor(QPalette::WindowText, theme.textPrimary);

    setPalette(colours);
    setAutoFillBackground(true);

    // The card is unchanged by the palette - a card is white in the dark too -
    // but the labels around it are not, so the whole strip is rebuilt.
    refresh();
}

void InfoStrip::setSnapshot(const GameSnapshot& snapshot)
{
    // Only when something in the strip actually moved. render() runs on every
    // one of a game's ~1300 frames and these four fields change once a round,
    // so rebuilding them each time would be work nobody can see - and setText()
    // on a QLabel is a relayout whether the text differs or not.
    if(haveShown
        && shown.roundNumber == snapshot.roundNumber
        && shown.roundCount == snapshot.roundCount
        && shown.trickCount == snapshot.trickCount
        && shown.roundType == snapshot.roundType
        && shown.hasTrump == snapshot.hasTrump
        && shown.trump == snapshot.trump)
    {
        return;
    }

    haveShown = true;

    // Copied field by field rather than by assigning the whole snapshot: this
    // holds what the strip is SHOWING, and a copy of the seats and the table
    // would be a few hundred bytes a round for fields nothing here reads.
    shown.roundNumber = snapshot.roundNumber;
    shown.roundCount = snapshot.roundCount;
    shown.trickCount = snapshot.trickCount;
    shown.roundType = snapshot.roundType;
    shown.hasTrump = snapshot.hasTrump;
    shown.trump = snapshot.trump;

    refresh();
}

void InfoStrip::refresh()
{
    if(!haveShown)
        return;

    const infostrip::Fields fields = infostrip::fieldsFor(shown);

    round->setText(fields.round);
    cardCount->setText(fields.cards);
    roundType->setText(fields.roundType);

    if(!fields.hasTrump)
    {
        // Named rather than left blank. An eight-card round having no trump is
        // a rule of the game and one of the five things this strip exists to
        // say; an empty space says the client forgot.
        trumpLabel->setText(tr("No trump"));

        // EMPTIED, NOT HIDDEN. Hiding it collapsed the strip to the height of a
        // line of text, so entering and leaving the eight-card round jumped the
        // table and the scoreboard beneath it by some fifty pixels - twice in
        // every game. The slot keeps its size and holds nothing, and the words
        // beside it say why it is empty.
        trumpCard->clear();
        return;
    }

    trumpLabel->setText(tr("Trump"));

    // devicePixelRatioF() of the widget, so a strip on a HiDPI screen gets a
    // card rasterized for it. QLabel lays a pixmap out at its device-independent
    // size, so the strip's geometry does not change with the ratio - which is
    // the whole of B1's contract, arriving here for free.
    //
    // trumpSlot rather than a size worked out here: it is the size the label is
    // fixed at, and a pixmap that disagreed with it would be clipped.
    trumpCard->setPixmap(cards.face(fields.trump, trumpSlot, devicePixelRatioF()));
}
