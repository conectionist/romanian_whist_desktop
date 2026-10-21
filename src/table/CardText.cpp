#include "CardText.h"

#include <QCoreApplication>

using romanian_whist::Card;
using romanian_whist::Rank;
using romanian_whist::Suit;

// No default case in either switch, so a fifth suit or a fourteenth rank is a
// warning rather than a silent blank; the fallback sits after the switch, as
// Card::toString() and snapshotlog have theirs.
QString cardtext::symbol(Suit suit)
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

QString cardtext::suitName(Suit suit)
{
    switch(suit)
    {
        case Suit::Hearts:   return QCoreApplication::translate("cardtext", "Hearts");
        case Suit::Diamonds: return QCoreApplication::translate("cardtext", "Diamonds");
        case Suit::Spades:   return QCoreApplication::translate("cardtext", "Spades");
        case Suit::Clubs:    return QCoreApplication::translate("cardtext", "Clubs");
    }

    return QString();
}

QString cardtext::label(Card card)
{
    QString rank;

    switch(card.rank)
    {
        case Rank::Two:   rank = QStringLiteral("2"); break;
        case Rank::Three: rank = QStringLiteral("3"); break;
        case Rank::Four:  rank = QStringLiteral("4"); break;
        case Rank::Five:  rank = QStringLiteral("5"); break;
        case Rank::Six:   rank = QStringLiteral("6"); break;
        case Rank::Seven: rank = QStringLiteral("7"); break;
        case Rank::Eight: rank = QStringLiteral("8"); break;
        case Rank::Nine:  rank = QStringLiteral("9"); break;
        case Rank::Ten:   rank = QStringLiteral("10"); break;

        // The court cards' letters are a deck's, and a Romanian one's differ.
        case Rank::Jack:  rank = QCoreApplication::translate("cardtext", "J"); break;
        case Rank::Queen: rank = QCoreApplication::translate("cardtext", "Q"); break;
        case Rank::King:  rank = QCoreApplication::translate("cardtext", "K"); break;
        case Rank::Ace:   rank = QCoreApplication::translate("cardtext", "A"); break;
    }

    return rank + symbol(card.suit);
}
