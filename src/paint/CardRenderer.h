#ifndef CARD_RENDERER_H
#define CARD_RENDERER_H

#include <romanian_whist/Card.h>

#include <QCache>
#include <QImage>
#include <QPixmap>
#include <QSize>

#include <cstddef>
#include <memory>
#include <unordered_map>

class QSvgRenderer;

// The second of the two seams of plan section 18.1: THE ONLY PLACE THAT KNOWS
// CARD ART IS A FILE.
//
// Section 7's two switches over rank and suit live in the .cpp and nowhere else,
// and no widget ever builds ":/cards/..." itself. Card::toString() is NOT the
// filename: the files spell both out (jack_of_hearts.svg, 10_of_clubs.svg) and
// toString() abbreviates to "JH" and "10C", which is the compact form the
// terminal client wants.
//
// THE SVG SET, NOT THE PNGs. Both are committed and both are embedded, but
// CMakeLists.txt's deploy step drops the imageformats plugins, so
// QPixmap(":/cards/x.svg") comes back null in an installed build while
// QSvgRenderer - a linked library rather than a plugin - keeps working. That
// file's comments are the authority on it. Plan section 7 reads the other way,
// as an option to hold in reserve; section 18.1 is the one that took the
// decision and gave that reason, and commit b5de977 is it landing.
//
// AND THAT FAILURE IS INVISIBLE WHILE DEVELOPING, which is the part none of the
// documents say. The qsvg plugin ships in a normal Qt install, so a widget
// loading a card through QPixmap works perfectly on the machine it was written
// on and produces blank cards only once deployed. No test can catch it - the
// suite would have to assert that an installed plugin is absent - so this class
// being the only caller is the entire defence. CardRendererTests.cpp records
// the run that established it.
//
// GUI THREAD ONLY. QPixmap cannot be constructed, copied or destroyed off it.
// Nothing on the game thread may reach for this object - not GameBridge, not a
// snapshot, not a pacing wait - and the fact that a snapshot carries Cards
// rather than anything drawable is what keeps that from being tempting.
class CardRenderer
{
public:
    // Whether the card is one the player may act on. The illegal case is
    // darkened and dulled, as the table redesign's mock-up does it - and
    // explicitly NOT grey-on-white, which section 18 rejects as unreadable at
    // card size. It stays opaque: section 18's 45% opacity let the card beneath
    // show through an overlapping hand (amended in the redesign's phase 7).
    //
    // BAKED INTO THE CACHED PIXMAP rather than applied by the caller, so B2's
    // CardItem::paint() stays a blit and a dimmed card costs nothing to draw
    // once it has been drawn once. It is also why the cache is keyed on state.
    enum class State
    {
        Normal,
        Dimmed
    };

    CardRenderer();
    ~CardRenderer();

    // `size` is in LOGICAL pixels and `dpr` is the target device pixel ratio.
    // The returned pixmap is rasterized at size * dpr physical pixels and
    // carries the ratio, so it is crisp on a HiDPI display and is still laid out
    // as `size`.
    QPixmap face(romanian_whist::Card card, QSize size, qreal dpr,
                 State state = State::Normal) const;

    // A back is not a special case in three widgets. It is drawn in a Forehead
    // round, in a Hidden round and in every opponent's rack, and a back that is
    // drawn in three places is a back that gets changed in two.
    QPixmap back(QSize size, qreal dpr) const;

    // The art's aspect ratio, and the only place it is known. Theme owns the
    // card HEIGHT because section 18.1 makes every table metric derive from it;
    // turning that height into the size to ask for is this class's half, so that
    // art with a different aspect changes one function rather than the layout.
    QSize sizeForHeight(qreal height) const;

    // How many pictures this has painted rather than found in its cache: faces
    // and backs both. READ-ONLY and for tests, which are how a window being resized
    // is shown to leave the cards alone until it settles (TableViewTests).
    std::size_t rasterizations() const { return misses; }

private:
    // Both produce a QImage rather than a QPixmap, and both take PHYSICAL
    // pixels. The device pixel ratio is stamped on in one place, at the end of
    // the one path that leaves this class, so it cannot be forgotten on a
    // branch - and a QImage is what the dimming pass needs to work on anyway.
    //
    // paintBack() is painted rather than loaded because there is no back in
    // resources/ - the committed art is 52 faces and nothing else. Swapping in a
    // back.svg later is this body and a line in cards.qrc, with back() itself
    // untouched.
    QImage paintBack(QSize physical) const;

    QImage rasterizeFace(romanian_whist::Card card, QSize physical) const;

    // Stamps the ratio, stores under the key, and hands back a copy.
    QPixmap finish(const QImage& image, quint64 key, qreal dpr) const;

    // EACH CARD'S ART, PARSED ONCE. Since the table redesign's phase 2 a resize
    // changes the ratio every card is asked for at, so a window being dragged
    // misses the pixmap cache below on every frame for every card on the table -
    // and building a QSvgRenderer reads and parses the file each time, which was
    // most of the cost of a miss. Keyed by indexOf() rather than an array of 52,
    // for the reason backBit gives in the .cpp.
    //
    // Unbounded, because it cannot grow past the deck: one entry per card art.
    // A file that fails to parse leaves its entry empty, so it is tried again,
    // and fails again, on every call rather than once.
    mutable std::unordered_map<unsigned int, std::unique_ptr<QSvgRenderer>> art;

    // MUTABLE AND BOUNDED. Mutable because caching is not observable through
    // this class's const interface, and bounded because the size in the key is
    // the window's: a window being dragged to resize asks for a different size
    // every frame, and an unbounded map would grow without limit for as long as
    // the drag lasted. QCache evicts the least recently used instead.
    mutable QCache<quint64, QPixmap> cache;

    // See rasterizations().
    mutable std::size_t misses = 0;
};

#endif
