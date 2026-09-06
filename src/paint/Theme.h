#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QPixmap>
#include <QSize>

#include <memory>

// The first of the two seams of plan section 18.1: THE ONLY PLACE A COLOUR OR A
// TABLE METRIC EXISTS.
//
// The design in section 18 will change - the background, the seat art and the
// card size are all things anyone building this wants to swap once they have
// seen them on screen. What decides whether that is an afternoon or a week is
// how many places construct a colour or a coordinate, and the answer has to be
// "one" from the first widget rather than be retrofitted once there are twelve.
//
// LIGHT AND DARK FROM THE START, which is the half that genuinely cannot be
// retrofitted cheaply: adding a sixth colour later is one line, but adding a
// second MODE later means finding every literal already written. Section 18 also
// fixes what dark mode does and does not touch - the surface changes tone, the
// cards do not, because a card is white in the dark too. That is why nothing
// about a card face or a card back is in this struct.
//
// The palette is the five names section 18.1 declares and no more. Section 14's
// colour-coded bid/won pill and section 18's +/- score colours are real and are
// not here, because they are added when B2 and D2 need them - to THIS struct,
// which is the rule that matters, rather than to the widget that wants them.
struct Theme
{
    // Qt's own type rather than an enum of ours, because the caller that
    // matters is QGuiApplication::styleHints()->colorScheme() and translating
    // between two spellings of the same three values would be pure friction.
    // Qt::ColorScheme::Unknown is a real value on platforms that do not report
    // one; forScheme() reads it as Light.
    static Theme forScheme(Qt::ColorScheme scheme);
    static Theme light();
    static Theme dark();

    QColor tableSurface;
    QColor seatPlaque;
    QColor accent;
    QColor textPrimary;
    QColor dimmedOverlay;

    // Solid, gradient or a photograph - no caller can tell which, which is what
    // makes changing it one edit. It is a gradient today only because there is
    // no art committed to point it at.
    QPixmap background(QSize view) const;

    // Stick figures today, portraits tomorrow. THE BOX IS WHAT THE SEAT PLAQUE
    // RESERVES, which is the whole reason this takes a QSize rather than a
    // scale: replacing the art then never reflows the layout, because the
    // layout never asked how big the art wanted to be.
    QPixmap avatar(unsigned int seat, QSize box) const;

    // One unit. Every other metric on the table derives from it, so new art with
    // a different aspect ratio does not mean re-tuning the layout.
    qreal cardHeightFor(QSize view) const;

private:
    // The painted implementations, each in one place and reached only through
    // the method above it. THAT SEPARATION IS THE POINT: swapping in real art
    // later is a body replacement here, with no signature, no caller and no
    // layout touched. Keeping the cache lookup out of them is what makes that
    // true - an image load is far more expensive than a gradient, so the cache
    // has to already be in the right place when the art arrives.
    QPixmap paintBackground(QSize view) const;
    QPixmap paintAvatar(unsigned int seat, QSize box) const;

    // EVERY colour, not the ones today's painters happen to read. Which members
    // a painter depends on is not visible from the cache lookup, and the answer
    // changes: paintAvatar() reads seatPlaque AND accent, and the bid/won pill
    // and score colours section 18 still owes this struct will read others. A
    // key that has to be updated alongside a painter is a key that will not be.
    quint64 paletteKey() const;

    // SHARED ACROSS COPIES, not per-copy. Theme is a copyable value that widgets
    // will hold by value, and a plain QPixmap member would mean each copy
    // repainting the same background independently. QPixmap is itself
    // refcounted, so this shares the pixels rather than duplicating them.
    //
    // No mutex: this whole struct is GUI-thread-only, because QPixmap is.
    struct Cache;
    std::shared_ptr<Cache> cache;
};

#endif
