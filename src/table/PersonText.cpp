#include "PersonText.h"

#include <QCoreApplication>

QString persontext::translate(const char* context, const Gendered& variants, Gender gender)
{
    const Variant& variant = gender == Gender::Female ? variants.female : variants.male;
    return QCoreApplication::translate(context, variant.source, variant.comment);
}

QString persontext::translate(const Sentence& sentence, const GameSnapshot& snapshot, unsigned int seat)
{
    const bool yours = snapshot.humanSeat >= 0 && static_cast<unsigned int>(snapshot.humanSeat) == seat;
    const Gender gender = seat < snapshot.seats.size() ? snapshot.seats[seat].gender : Gender::Male;

    return translate(sentence.context, yours ? sentence.yours : sentence.others, gender);
}

Gender persontext::yourGender(const GameSnapshot& snapshot)
{
    if(snapshot.humanSeat < 0 || static_cast<std::size_t>(snapshot.humanSeat) >= snapshot.seats.size())
        return Gender::Male;

    return snapshot.seats[static_cast<std::size_t>(snapshot.humanSeat)].gender;
}
