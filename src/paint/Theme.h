#ifndef THEME_H
#define THEME_H

#include <QBrush>
#include <QColor>
#include <QPixmap>
#include <QRectF>
#include <QSize>

#include <array>
#include <memory>

// The first of the two seams of plan section 18.1: THE ONLY PLACE A COLOUR
// EXISTS.
//
// It was the only place a table METRIC existed too, a card height derived from
// the window, until the table redesign's phase 2 replaced that with a fixed table
// space the view scales. Those numbers are the mock-up's now, and they live in
// table/TableSpace.h.
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
//
// EVERY COLOUR LIVES IN THIS BASE, NOT IN Theme BELOW IT, because Theme's caches
// tell a stale picture from a fresh one by comparing the colours it was painted
// from with the colours the theme holds now: all of them, through the defaulted
// operator== at the foot of this struct. A member added here is in that
// comparison with no other edit. The hand-written key this replaced had to be
// told about each new colour, and twice it was not - accent, which paintAvatar()
// reads, and then the three bid colours in B2. Theme.cpp asserts that Theme adds
// nothing beside its cache, so a colour declared there instead does not compile.
struct ThemeColours
{
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

        // THE TABLE ITSELF (section 2.4), added in the redesign's phase 3.
        //
        // The wood's edge: a dark stroke round the rim, a faint warm highlight
        // inside it, and the darkest of the three bead rings, which is the same
        // brown as the stroke.
        QColor woodDark;
        QColor woodHighlight;

        // The bead's other two rings, from the felt outward.
        QColor beadInner;
        QColor beadMiddle;

        // The felt's vignette, light at the centre and darker toward the rim, and
        // the faint gold circle inset on it.
        QColor feltVignetteLight;
        QColor feltVignetteDark;
        QColor feltLine;

        // The shadows: the table's on the room, the rim's into the wood, and two
        // into the felt, one wide and one close to the edge.
        QColor tableShadow;
        QColor woodInnerShadow;
        QColor feltInnerShadowWide;
        QColor feltInnerShadowNear;

        // The logo plaque: its green from the centre out, the gold border and the
        // fine line inside it, the lettering's gold from top to bottom and the
        // shadow under it, and the clubs and ornaments.
        QColor logoCentre;
        QColor logoMiddle;
        QColor logoEdge;
        QColor logoBorder;
        QColor logoInnerLine;
        QColor logoTextTop;
        QColor logoTextMiddle;
        QColor logoTextBottom;
        QColor logoTextShadow;
        QColor logoOrnament;

        // THE NAME PANELS (section 2.5), added in the redesign's phase 4.
        //
        // The active seat's glow, round its gold-bright ring; the Taken/Bid pill's
        // border while it is neutral, which a coloured state replaces with its own.
        QColor activeGlow;
        QColor pillBorder;

        // The portrait's frame, a gold hairline. page.html also gives it a dark
        // inset line, which never shows: an inset shadow is painted under the
        // content, and the portrait's picture fills its whole content box.
        QColor portraitBorder;

        // WHAT EVERY PORTRAIT SHARES: the dark its background fades to, the
        // eyebrows' stroke, and the mouth of a face with no mustache.
        QColor portraitEdge;
        QColor portraitBrow;
        QColor portraitMouth;

        // ONE HEAD-AND-SHOULDERS BUST PER SEAT, table.js PLAYERS[].look: six hand-
        // drawn variants rather than a hue rotation, indexed as Theme::avatar() is.
        //
        // hat, mustache and beard are INVALID where the look has none, which is how
        // the mock-up's optional fields read; a valid colour draws the feature.
        struct PortraitLook
        {
            QColor background;
            QColor skin;
            QColor shade;
            QColor hair;
            QColor coat;
            QColor shirt;
            QColor hat;
            QColor mustache;
            QColor beard;
            bool longHair = false;

            bool operator==(const PortraitLook&) const = default;
        };

        std::array<PortraitLook, 6> portraits;

        // THE RACKS (section 2.6), added in the redesign's phase 5. The card backs'
        // own colours are CardRenderer's, as every card's are; these are the
        // rack's light on them and the rail they stand in.
        //
        // The row's drop shadow, under the cards and the rail together.
        QColor rackShadow;

        // Over each card in the rack: light at the far end, which rises toward
        // the viewer, and dark at the rail; and a dark hairline round its edge.
        QColor rackCardLight;
        QColor rackCardDark;
        QColor rackCardEdge;

        // The rail: its brown, lit along the edge toward the table and darker
        // along the owner's, a dark outline, and the shadow it casts.
        QColor railBase;
        QColor railLight;
        QColor railDark;
        QColor railEdge;
        QColor railShadow;

        // THE PLAYED CARDS (section 2.7), added in the redesign's phase 6. Every
        // card on the felt casts a soft shadow; the winning one casts a deeper
        // one and glows, inside a gold-bright ring.
        QColor playedShadow;
        QColor winningShadow;
        QColor winningGlow;

        // THE CARDS IN YOUR HAND (section 2.8), added in the redesign's phase 7.
        // Each casts a shadow down and to the left; raised under the cursor, it
        // casts a deeper one and glows inside the winner's gold-bright ring.
        QColor heldShadow;
        QColor raisedShadow;
        QColor raisedGlow;

        // The lead rhombus: gold-bright, with a dark hairline round it and a glow.
        QColor leadMarkOutline;
        QColor leadMarkGlow;

        // THE CORNER PANELS (sections 2.9 to 2.11), added in the redesign's phase 8.
        //
        // A button under the cursor: its inset gold line brightened, and a faint
        // gold glow round it.
        QColor panelInnerLineHover;
        QColor buttonGlow;

        // The faint gold rules inside a panel: under the scoreboard's headings,
        // above its trump, and over the trick panel's prompt.
        QColor hudRule;

        // The trump card's shadow on the scoreboard.
        QColor trumpShadow;

        // THE MENU BAR (section 2.12), added in the redesign's phase 9. A vertical
        // gradient over a panelBorder hairline; under the cursor, an item's faint
        // gold background. Beneath the bar, on whatever page is showing, a faint
        // gold glint along its edge and a soft shadow.
        QColor menuTop;
        QColor menuBottom;
        QColor menuHover;
        QColor menuGlint;
        QColor menuShadow;

        // THE BID STRIP (section 2.13), added in the redesign's phase 10. Its bids are
        // the green buttons above; each casts a small shadow, and under the cursor
        // glows inside a gold-bright ring instead. The one bid the last bidder may
        // not make is drawn flat and dark, its number struck through.
        QColor bidShadow;
        QColor bidGlow;
        QColor bidDisabledFill;
        QColor bidDisabledText;
        QColor bidDisabledBorder;

        bool operator==(const TablePalette&) const = default;
    };

    TablePalette table;

    bool operator==(const ThemeColours&) const = default;
};

// The colours above, and what is drawn from them.
struct Theme : ThemeColours
{
    // Qt's own type rather than an enum of ours, because the caller that
    // matters is QGuiApplication::styleHints()->colorScheme() and translating
    // between two spellings of the same three values would be pure friction.
    // Qt::ColorScheme::Unknown is a real value on platforms that do not report
    // one; forScheme() reads it as Light.
    static Theme forScheme(Qt::ColorScheme scheme);
    static Theme light();
    static Theme dark();

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

    // THE ROOM BEHIND THE REDESIGNED TABLE: section 2.2's radial gradient from
    // `table`, dark wood fading to near black, over the whole of the table page's
    // viewport. Same logical-size-and-dpr contract as background() above.
    //
    // A SECOND METHOD RATHER THAN A NEW BODY FOR background(), because that one is
    // also the main menu's and the Card art window's, and the redesign keeps those
    // on today's palette (open question Q1). The table page draws this; nothing
    // else does until a later phase moves it across.
    QPixmap room(QSize view, qreal dpr) const;

    // The room as a brush over `view` in whatever units the caller paints in: what
    // room() rasterizes, for a caller that fills straight from the gradient
    // instead - TableView, while a resize settles, when the size changes every
    // frame and a cached picture of the old size would only be thrown away.
    QBrush roomBrush(const QRectF& view) const;

    // A drawn bust today (table.PortraitLook), real art tomorrow. THE BOX IS
    // WHAT THE SEAT PANEL RESERVES, which is the whole reason this takes a QSize
    // rather than a scale: replacing the art then never reflows the layout,
    // because the layout never asked how big the art wanted to be.
    //
    // `seat` picks the look, modulo the six there are. The table passes a seat's
    // place clockwise from the bottom, so you are always the first.
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
    QPixmap paintRoom(QSize physical) const;
    QPixmap paintAvatar(unsigned int seat, QSize physical) const;

    // The other half: stamping the ratio onto what a painter returned. Static
    // and shared by both, so neither public method can rasterize large and then
    // forget to say so.
    static QPixmap rasterize(QPixmap pixmap, qreal dpr);

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
