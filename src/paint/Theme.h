#ifndef THEME_H
#define THEME_H

#include <QColor>
#include <QPixmap>
#include <QSize>
#include <QString>

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
// bid/won pill and section 18's +/- score colours - each added in the phase
// whose widget needed it, and each added HERE rather than in that widget, which
// is the rule that matters. Nothing in section 18 is owed to this struct now.
//
// The one member section 18 does NOT name is accentText below, which is not a
// sixth design colour but the accent's second FORM: a colour readable as a mark
// and a colour readable as a word are different colours, and the section spells
// out neither. See the note on it.
//
// THE TABLE PAGE IS DARK IN BOTH SCHEMES - the table redesign's open question Q1
// (docs/ui-redesign/IMPLEMENTATION_PLAN.md), decided by the owner on 2026-10-17.
// The redesign is a dark wood-panelled room and has no light variant to follow
// the system with, so its colours are one set, `table` below, which light() and
// dark() assign from the same function. The members above it keep following the
// scheme for the main menu and the dialogs, and keep drawing today's table until
// the redesign's later phases move each widget across to `table`.
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

    // THE ACCENT AS TEXT, which is not the same colour as the accent as a mark.
    // C4 painted section 17's round-type label in `accent` and the light theme's
    // amber measured 2.0:1 against the plaque behind it - below even the 3:1
    // large-text floor, and dimmer than the textPrimary the label had before,
    // so the line meant to CATCH the eye read worse than the three beside it.
    //
    // A hairline or a pip on a card can be the bright amber, because a mark is
    // read as a shape at whatever contrast it has. A word has to be read as
    // letters. So the two uses are two members: `accent` stays what it was for
    // every mark already drawing it, and text takes the darkened one here.
    //
    // In the dark theme they are one colour, because the amber that had to be
    // lifted to hold against a dark plaque already reads as text against it -
    // hence the assignment rather than a literal in dark(). The split exists
    // for the light theme; the dark one just keeps the same two names honest.
    QColor accentText;

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

    // Section 18's scoreboard numbers: a positive round score green with a `+`,
    // a negative one red with a `-`. Added in B2b, with the scoreboard that
    // reads them, which is what the note above this struct promised.
    //
    // The SIGN is what carries the meaning and the colour is what makes it
    // quick, the same way round as the pill above - section 18 allows no cue
    // that is colour alone, and a round score is a number that already says
    // which way it went.
    QColor scorePositive;
    QColor scoreNegative;

    // The redesign's palette: section 2.2 of docs/ui-redesign/IMPLEMENTATION_PLAN.md,
    // copied from the mock-up's page.html. The same in both schemes, per the note
    // above this struct. Added in the redesign's phase 1, before anything reads
    // it, so the phases that build the new table have one place to take it from.
    //
    // NOTHING BUT QColor, AND THAT IS LOAD-BEARING. ThemeTests counts these
    // members from sizeof() to prove every one of them reaches paletteKey(), so a
    // member of any other type here breaks the count, and that test with it.
    struct TablePalette
    {
        // The room behind the table, a radial gradient from centre to edge.
        QColor roomCentre;
        QColor roomMiddle;
        QColor roomEdge;

        // What lies under the wood and felt textures, where they let it show.
        QColor woodBase;
        QColor feltBase;

        // Every panel: a vertical gradient, a gold hairline border, an inset
        // dark line and then a faint gold one inside it, and a drop shadow.
        QColor panelTop;
        QColor panelBottom;
        QColor panelBorder;
        QColor panelInnerShadow;
        QColor panelInnerLine;
        QColor panelShadow;

        // Gold for marks and borders, gold-bright for whatever is active or
        // winning; cream for text and muted for labels.
        QColor gold;
        QColor goldBright;
        QColor cream;
        QColor muted;

        // The Taken/Bid pill: on target, still owing, over. Its text always
        // carries the numbers, so the colour is never the only cue (section 18).
        QColor good;
        QColor warn;
        QColor bad;

        // Suit glyphs inside text. The black suits draw in cream, because black
        // on a dark panel would vanish - a member all the same, so no caller has
        // to know that.
        QColor redSuit;
        QColor blackSuit;

        // The green buttons: the bids, and the sheets' actions.
        QColor buttonTop;
        QColor buttonBottom;
        QColor buttonBorder;

        bool operator==(const TablePalette&) const = default;
    };

    TablePalette table;

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

    // The redesign's two table textures (tools/generate_table_textures.py), as
    // the source art: the wooden rim 1020x1020 with alpha, the felt 880x880.
    //
    // NOT SCALED HERE, unlike the two above. The table is composed from these,
    // the base colours and a stack of gradients, and it is that composition
    // which gets cached at the view's size and ratio (the redesign's phase 3).
    // These are its inputs, read once and shared by every copy of the theme.
    //
    // A null pixmap means the resource was never embedded, and that is said on
    // stderr rather than left to look like a missing table.
    QPixmap woodTexture() const;
    QPixmap feltTexture() const;

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

    // The one place a texture is read, so the warning on a missing resource
    // cannot be left off one of the two.
    static QPixmap loadTexture(const QString& path);

    // EVERY colour, not the ones today's painters happen to read. Which members
    // a painter depends on is not visible from the cache lookup, and the answer
    // changes: paintAvatar() reads seatPlaque AND accent, and nothing about that
    // is visible from avatar()'s side of the call. A key that has to be updated
    // alongside a painter is a key that will not be.
    //
    // The bid and score colours are in the key while NEITHER of this struct's
    // two painters reads one - they are drawn by the seat plaque and the
    // scoreboard, which have palettes of their own to consult. That is the rule
    // working rather than an oversight, and it is why the rule is "every
    // colour" and not "every colour a painter here reads": the bid colours were
    // added to the struct in B2 and to the key only in B2a, and that gap is the
    // whole argument.
    //
    // All of `table` too, which nothing paints from yet either.
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
