#include "RoundText.h"

#include <QCoreApplication>

using romanian_whist::RoundType;

QString infostrip::cardsText(unsigned int trickCount)
{
    // TWO STRINGS RATHER THAN Qt's "%n card(s)", which was tried and is wrong
    // here. Qt takes the plural FORMS from the catalogue, for the source language
    // as much as for any other - so with no translator loaded, which is what an
    // English build is, translate("%n card(s)", n) substitutes the count and
    // leaves the rest alone: "1 card(s)", on screen, in the shipped language.
    // Selecting here spells both forms correctly out of the box and still gives
    // lupdate two strings to collect. Languages whose plural rules need more than
    // two forms are the reason %n exists, and the day one is added this becomes
    // the thing to revisit.
    //
    // REVISITED IN THE REDESIGN'S PHASE 12, with Romanian: three forms, the third
    // ("20 de cărți") only from 20 up. A hand is never more than 8 cards, so these
    // two strings serve it, as do the tricks and bids counted the same way. What can
    // pass 19 - a final score, a game's rounds, the minutes a game takes - is %n:
    // GameOverPanel.cpp, RulesSheet.cpp and NewGameDialog.cpp.
    return trickCount == 1 ? QCoreApplication::translate("infostrip", "1 card")
                           : QCoreApplication::translate("infostrip", "%1 cards")
                                 .arg(trickCount);
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

bool infostrip::isCalledOut(RoundType type)
{
    // Asked as "not Normal" rather than listed, so a round type added to the
    // engine is called out by default. The two that exist are the two section 17
    // is about, and a new one would be a special round too.
    return type != RoundType::Normal;
}

infostrip::Fields infostrip::fieldsFor(const GameSnapshot& snapshot)
{
    Fields fields;

    // "of" rather than "7/24": this is a sentence a player reads once a round,
    // not a column in a log, and the slash is the log's idiom.
    //
    // One arg() taking both, never chained - the rule SnapshotLog.cpp and
    // MainWindow both carry, because a substituted value containing %2 would
    // otherwise swallow the next substitution.
    fields.round = QCoreApplication::translate("infostrip", "Round %1 of %2")
                       .arg(QString::number(snapshot.roundNumber),
                            QString::number(snapshot.roundCount));

    // The trick count IS the number of cards dealt to each player - a round of
    // six cards is a round of six tricks - so this reads it rather than adding a
    // field that would have to agree with it.
    fields.cards = cardsText(snapshot.trickCount);

    fields.roundType = roundTypeText(snapshot.roundType);

    fields.hasTrump = snapshot.hasTrump;

    // Copied ONLY under hasTrump. Left at Card's default otherwise, which is a
    // real two of clubs - see the header.
    if(snapshot.hasTrump)
        fields.trump = snapshot.trump;

    return fields;
}
