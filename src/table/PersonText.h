#ifndef PERSON_TEXT_H
#define PERSON_TEXT_H

#include "core/GameSnapshot.h"
#include "core/PlayerProfile.h"

#include <QString>

// SENTENCES THAT AGREE WITH THE PERSON THEY ARE ABOUT.
//
// English says "Leader: Ana" and "Leader: Bogdan" alike, but Romanian's adjective
// agrees with its subject - "Ana e prima", "Bogdan e primul" - and changes person
// for your own seat: "Tu ești primul". So one English source is filed several
// times for the translator, told apart by its disambiguation, and the seat's
// gender (SeatView::gender) and whether it is yours pick which one is shown.
//
// WRITTEN WITH QT_TRANSLATE_NOOP3 at the call site, never built here from a
// source passed in: lupdate files a string only where it sees it spelt out, with
// its context and its disambiguation, so each variant has to be a literal.
//
//     constexpr persontext::Sentence leader{
//         "trickpanel",
//         { QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about a man"),
//           QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about a woman") },
//         { QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about you, a man"),
//           QT_TRANSLATE_NOOP3("trickpanel", "Leader: %1", "about you, a woman") } };
//
// Untranslated, every variant shows its English source, so English reads as it
// always has.
namespace persontext
{
// What QT_TRANSLATE_NOOP3 spells: { source, disambiguation }.
struct Variant
{
    const char* source = nullptr;
    const char* comment = nullptr;
};

// One sentence about one person, as a man and as a woman.
struct Gendered
{
    Variant male;
    Variant female;
};

// One sentence about any seat: said of somebody else, or of you.
struct Sentence
{
    const char* context = nullptr;
    Gendered others;
    Gendered yours;
};

// The variant for this gender, translated.
QString translate(const char* context, const Gendered& variants, Gender gender);

// The variant for this seat: yours if it is the human's, the others' otherwise,
// in the seat's gender. A seat the snapshot does not have is somebody else's, a
// man's - the defaults SeatView itself has.
QString translate(const Sentence& sentence, const GameSnapshot& snapshot, unsigned int seat);

// The human seat's gender, and Male in a game nobody plays.
Gender yourGender(const GameSnapshot& snapshot);
}

#endif
