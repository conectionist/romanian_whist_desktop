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
// The palette is the five names section 18.1 declares, plus section 14's
// bid/won pill - added in B2, when the seat plaque that needs it was written,
// and added HERE rather than in that widget, which is the rule that matters.
// Section 18's +/- score colours are still owed and arrive with the scoreboard.
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

    // Section 14's bid/won pill, which is colour-coded because "on target",
    // "still owing tricks" and "over the bid" are three different states and a
    // player should not have to subtract to see which one they are in.
    //
    // NEVER THE ONLY CUE. Section 18's rule is that colour never carries meaning
    // alone, and it does not here: the pill always reads "2 - 1" as well, so the
    // colour is what makes the state quick rather than what makes it knowable.
    QColor bidOnTarget;
    QColor bidOwing;
    QColor bidOver;

    // Solid, gradient or a photograph - no caller can tell which, which is what
    // makes changing it one edit. It is a gradient today only because there is
    // no art committed to point it at.
    //
    // `view` is in LOGICAL pixels and `dpr` is the target device pixel ratio,
    // the same contract CardRenderer::face() states: the returned pixmap is
    // rasterized at view * dpr physical pixels and carries the ratio, so it is
    // crisp on a HiDPI display and is still laid out at `view`. Without it the
    // table is visibly soft beside cards that are not.
    QPixmap background(QSize view, qreal dpr) const;

    // Stick figures today, portraits tomorrow. THE BOX IS WHAT THE SEAT PLAQUE
    // RESERVES, which is the whole reason this takes a QSize rather than a
    // scale: replacing the art then never reflows the layout, because the
    // layout never asked how big the art wanted to be.
    //
    // Same logical-size-and-dpr contract as background() above.
    QPixmap avatar(unsigned int seat, QSize box, qreal dpr) const;

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
    //
    // These take PHYSICAL pixels, as CardRenderer's rasterizeFace() does. Every
    // coordinate in both is a fraction of the box it was handed, so a larger box
    // is the same picture at more pixels and nothing here has to know about a
    // ratio. Attaching the ratio is the caller's half, done above.
    QPixmap paintBackground(QSize physical) const;
    QPixmap paintAvatar(unsigned int seat, QSize physical) const;

    // The other half: stamping the ratio onto what a painter returned. Static
    // and shared by both, so neither public method can rasterize large and then
    // forget to say so.
    static QPixmap rasterize(QPixmap pixmap, qreal dpr);

    // EVERY colour, not the ones today's painters happen to read. Which members
    // a painter depends on is not visible from the cache lookup, and the answer
    // changes: paintAvatar() reads seatPlaque AND accent, and the score colours
    // section 18 still owes this struct will read others. A key that has to be
    // updated alongside a painter is a key that will not be.
    //
    // The bid colours above are IN the key while nothing paints them, which is
    // the rule working rather than an oversight: section 14's pill can start
    // reading bidOwing without an edit here. They were added to the struct in B2
    // and to the key in B2a, and that gap is the whole argument for the rule.
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
