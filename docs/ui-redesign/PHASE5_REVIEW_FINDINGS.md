# Phase 5 review findings (PR #30)

What an `xhigh` `/code-review` of PR #30 ("racks on the rim") found after the PR had been merged.
Nothing here crashes or breaks a test.

**Revised after phase 6's review round:** every finding was checked against
`redesign/phase-6-trick` at `6c4ae69`, and the line numbers are for that commit.
- One finding is fixed: #11, by `bbff23d`.
- Phase 6 added new places for #2, #3, #8 and #12. Each finding names them.
- The rest are still open and still deferred to a later phase.

**Severity:** 1 = code quality only, 3 = a user can see it, 5 = crash or wrong game.
**Complexity:** 1 = a one- or two-line change, 3 = a new helper or API, 5 = a large refactor.

| # | Finding | Sev. | Cplx. | Status |
|---|---|---|---|---|
| 1 | [The rack's shadow is cut away under the cards](#1-the-racks-shadow-is-cut-away-under-the-cards) | 3 | 3 | Open |
| 2 | [`frameRotation()` can return -180](#2-framerotation-can-return--180) | 2 | 1 | Open, wider since phase 6 |
| 3 | [A null rack picture is kept as current](#3-a-null-rack-picture-is-kept-as-current) | 2 | 1 | Open, wider since phase 6 |
| 4 | [Forehead faces are drawn upside down](#4-forehead-faces-are-drawn-upside-down) | 4 | 2 | Open |
| 5 | [`rackAngle()` returns 0 for "no rack"](#5-rackangle-returns-0-for-no-rack) | 1 | 2 | Open |
| 6 | [The card-back line's corners use one axis](#6-the-card-back-lines-corners-use-one-axis) | 2 | 1 | Open |
| 7 | [A test returns an image its painter is still drawing on](#7-a-test-returns-an-image-its-painter-is-still-drawing-on) | 1 | 1 | Open |
| 8 | [`setRotation()` repaints even when the angle is the same](#8-setrotation-repaints-even-when-the-angle-is-the-same) | 2 | 1 | Open, wider since phase 6 |
| 9 | [`setDevicePixelRatio()` copies the cached card art](#9-setdevicepixelratio-copies-the-cached-card-art) | 1 | 2 | Open |
| 10 | [The rack cache empties too soon](#10-the-rack-cache-empties-too-soon) | 1 | 1 | Open |
| 11 | [The rack cache is a copy of the panel cache](#11-the-rack-cache-is-a-copy-of-the-panel-cache) | 1 | 4 | **Fixed** (`bbff23d`) |
| 12 | [Every item copies SeatItem's picture memo](#12-every-item-copies-seatitems-picture-memo) | 2 | 3 | Open, wider since phase 6 |

**Suggested order:**
- **#4 first,** because players see it. Then #1.
- **#2, #3, #6, #7, #8 and #10** take a few minutes each.
- **#12 is now the one refactor left.** #11, its other half, is done. It covers four items now, and
  doing it also fixes #3.

---

## 1. The rack's shadow is cut away under the cards

`src/table/RackItem.cpp:271`. Severity 3, complexity 3.

The rack's drop shadow, the CSS `drop-shadow` under the whole row, is drawn with
`drawShapeShadow`, which cuts the shadow away wherever the cards and rail are. They are opaque and
painted on top, so each anti-aliased edge pixel gets the shadow times (1-c)² instead of (1-c).

**What you see:** at an edge pixel with coverage c = 0.5 and shadow alpha about 0.5, the result is
c + s(1-c)² = 0.625, where CSS gives c + s(1-c) = 0.75. That leaves a faint light seam round every
rack card, with the wood showing through. CSS composites the shadow beneath the image and does not
cut it away.

**Fix:** `Shadows.h` added `Side::Beneath` to fix this same seam on the table rim, but shapes have
no `Beneath` option. Add one, or draw the rack's shadow on its own layer under the rack.

**Not the played cards.** Phase 6's card shadow is also drawn with `drawShapeShadow`, but there it
copies CSS: `.tcard` uses a `box-shadow`, which CSS does clip to outside the card. The winner's
gold ring had the matching seam against the art, and `5c9be6b` fixed that by filling the ring
through under the card.

## 2. `frameRotation()` can return -180

`src/table/RackItem.cpp:169`. Severity 2, complexity 1.

`frameRotation()` brings the angle into (-180, 180] and only then snaps it to an exact quarter turn.
An angle just above -180 therefore snaps to -180. That is outside the function's documented range,
and it takes the path that `QTransform::rotate()` does not handle exactly, which the comment at line
175 itself warns about.

**What you see:** checked: `frameRotation(-pi/2 + 1e-15)` returns -180.0, while
`frameRotation(-pi/2)` returns 180.0. `QTransform::rotate(-180)` goes through sin/cos
(sin = -1.2e-16) instead of the exact 180 path, so an item rotated this way is no longer
pixel-exact.

**Wider since phase 6:** the played cards and the lead rhombus are turned by it too
(`TrickItem.cpp:22`, `:42`), as well as the racks (`TableScene.cpp:192`). Today's tables give
exactly -pi/2 for the top seat, which is why phase 6's "exact quarter turns" test passes.

**Fix:** snap first and normalize after, or map a snapped -180 to 180.

## 3. A null rack picture is kept as current

`src/table/RackItem.cpp:393`. Severity 2, complexity 1.

`RackItem::picture()` stores whatever the shared cache returns and clears `stale`, even when that
picture is null for a rack that has cards. `SeatItem::picture()` (`SeatItem.cpp:268`) deliberately
stays stale in that case, "so the next real paint composes rather than finding a null picture
marked as current".

**What you see:** if `render()` returns a null pixmap while `count > 0` (for example a `QPixmap`
allocation failing at an extreme size or ratio), the rack keeps the null picture as current for that
ratio. `paint()` then draws nothing, and the opponent's rack is missing until `setRack()` changes or
the ratio does.

**Wider since phase 6:** `CardItem::shadow()` (`CardItem.cpp:167`) has the same pattern: a null
shadow is kept as current, so a played card would lose its shadow until it changes. Its inputs are
checked first, so only a failed allocation could cause it. `LeadMarkItem::paint()` does guard,
because it recomposes whenever its picture is null.

**Fix:** copy SeatItem's guard into both. Fixing #12 fixes this as well.

## 4. Forehead faces are drawn upside down

`src/table/RackItem.cpp:105`. Severity 4, complexity 2.

A Forehead face is mapped with the card's top edge at the rail and its bottom at the raised far end.
A card standing in a rack and facing the table would have its top at the raised edge. Card backs are
symmetric, which hides this, but faces are not.

**What you see:** in a 4-player Forehead round, the top opponent's face-up card (say a 7 of hearts)
is rotated 180 degrees on screen. On the side seats it is turned sideways the wrong way. In Forehead
the whole point is reading the other players' cards.

**Fix:** flip the face's flat rect, or its brush, by 180 degrees in the frame, so the face reads
upright from the human's side of the table.

## 5. `rackAngle()` returns 0 for "no rack"

`src/table/SeatLayout.h:77`. Severity 1, complexity 2.

`rackAngle(seatCount, humanSeat, seat)` returns 0.0 for a seat with no panel. 0 radians is also a
real rack angle (the right-hand seat), so a caller can't tell "no rack here" from a real answer.

**What you see:** nothing. Since phase 6's review, the app reaches seat geometry through
`seatlayout::place()` (`SeatLayout.cpp:131`), which returns no `Place` for a seat with no panel.
The seat overload of `rackAngle()` is used only by tests.

`Place::angle` has the same shape, though: it is 0.0 for the human's seat, which has no rack. The
`human` flag next to it says so, and every caller checks the flag first.

**Fix:** remove the seat overload of `rackAngle()` and point the tests at `place()`. Optionally,
make `Place::angle` a `std::optional<qreal>`, so the human's seat cannot be read as the right-hand
rack.

## 6. The card-back line's corners use one axis

`src/paint/CardRenderer.cpp:395`. Severity 2, complexity 1.

The new dark line round the card back uses `7*pixelsPerUnit` (the horizontal scale) for both corner
radii. The cream body underneath was drawn with `7*sx` by `7*sy`, because the painter scales each
axis on its own. The two corners match only when the aspect is exactly 100:145.

**What you see:** at `back(QSize(60,60))` or `back(QSize(41,90))`, sizes `CardRendererTests` asks
for, the line has a 4.2 px circular corner round a 4.2 × 2.9 px elliptical cream corner. The line
cuts across the corner or leaves a cream sliver outside it. Real card sizes are close to the
standard aspect, so on the table this barely shows.

**Fix:** use `7*sx` and `7*sy` for the line's two radii.

## 7. A test returns an image its painter is still drawing on

`tests/SeatItemTests.cpp:111`. Severity 1, complexity 1.

The `drawn` lambda returns its `QImage` while the `QPainter` is still active (no `painter.end()`).
`RackItemTests`' `drawnAt` ends the painter before returning.

**What you see:** nothing on today's compilers. Whether it works depends on NRVO. If the copy is not
elided, the returned image is copied or moved from a device with an active raster engine, the
painter then ends on a moved-from image, and the pixel comparison may run on an image that was never
flushed.

**Fix:** call `painter.end()` before `return`.

## 8. `setRotation()` repaints even when the angle is the same

`src/table/TableScene.cpp:192`. Severity 2, complexity 1.

`relayout()` calls `rack->setRotation()` for every visible rack on every snapshot and theme change.
`QGraphicsItem::setRotation` calls `prepareGeometryChange()` before its equality check, so even an
unchanged angle redoes the rack's place in the scene index and repaints its whole area.

**What you see:** nothing; it is wasted work. Checked with a standalone Qt 6.11.2 program:
`setRotation(90)` on an item already at 90 caused one more paint and one `changed()` signal, while
`setPos()` with the same point caused none. With about 1300 snapshots a game and up to 5 racks,
every snapshot repaints the rack areas, and the table top, shadows and panels under them, for
nothing.

**Wider since phase 6:** both phase 6 items do the same, every snapshot:
- `TrickItem::setTrick()` turns every played card (`TrickItem.cpp:126`), and a card's area now
  includes its shadow;
- `LeadMarkItem::setMark()` turns the rhombus (`LeadMarkItem.cpp:69`).

**Fix:** `if(item->rotation() != r) item->setRotation(r);` in all three places, or one small helper
they share.

## 9. `setDevicePixelRatio()` copies the cached card art

`src/table/RackItem.cpp:295`. Severity 1, complexity 2.

`art.setDevicePixelRatio(1.0)` on a `QPixmap` shared with `CardRenderer`'s cache detaches it, so
every `render()` deep-copies the pixels of each card art it uses.

**What you see:** nothing; it is wasted work. Checked: `cacheKey()` differs after
`setDevicePixelRatio` on a shared copy, so the pixels were copied. An 8-card rack copies eight backs
of about 80 × 117 px on every settle.

**Fix:** take `art.devicePixelRatio()` into account in the brush transform, or draw the art as a
`QImage`, instead of changing the ratio.

## 10. The rack cache empties too soon

`src/table/RackItem.h:135`. Severity 1, complexity 1.

The shared rack cache throws everything away when it reaches 12 entries, a size chosen for "a
round's worth and a Forehead card or two". A 6-player Forehead round adds five different face
pictures. Since `bbff23d` the limit is `PictureCache`'s `Most` parameter, and clearing at the limit
is that template's policy, shared with the panels and the card shadows.

**What you see:** nothing; it is wasted work. After an 8-card round the cache holds pictures for 8
down to 1 backs. In a 6-player Forehead round the fifth face reaches 12 entries and clears every
saved back picture, so later rounds rebuild pictures the cache already had. The images are never
wrong.

**Fix:** raise the rack's `Most` (about 8 backs + 5 faces + headroom). Making `PictureCache` evict
one entry instead of clearing them all would fix this for every cache at once.

## 11. The rack cache is a copy of the panel cache

**Fixed in phase 6's review round, by `bbff23d`.** `src/paint/PictureCache.h` is the one shared
template, `PictureCache<Key, Style, Most>`, and `devicepixels::Canvas` is the one picture type.
`panel::Cache`, `rackitem::Cache` and phase 6's `cardshadow::Cache` are thin wrappers round it.

What it was: `rackitem::Cache` and `rackitem::Picture` were almost line for line `panel::Cache` and
`panel::Picture`: the same rule of one ratio and one palette, linear search, clearing at the size
limit, a render counter, and a `{pixmap, offset}` struct. Only the lookup key differed, so a fix to
one copy could silently miss the other.

## 12. Every item copies SeatItem's picture memo

`src/table/RackItem.h:200`, `src/table/CardItem.h:106`, `src/table/LeadMarkItem.h:47`.
Severity 2, complexity 3. (Was "RackItem copies SeatItem's picture memo".)

Each item that draws a cached picture keeps its own memo: the picture, the ratio it was made at, a
stale flag, and a `picture(ratio)`-style function that recomposes when stale or when the ratio
changes. `SeatItem` had it first (`SeatItem.h:210`).

**Wider since phase 6:** there are now four copies:
- `SeatItem`'s, the first;
- `RackItem`'s: `composed`, `composedRatio`, `stale`, `composedCount`;
- `CardItem`'s, added for the card shadow: `shadowPicture`, `shadowRatio`, `shadowStale`;
- `LeadMarkItem`'s: `picture`, `offset`, `pictureRatio`, `rendered`.

#11's fix shared the caches behind them, not these memos.

**Why it matters:** the copies have already drifted apart.
- The rack's copy and the card's lost SeatItem's null-picture guard, which is #3.
- The rhombus's copy has the guard, but no stale flag: it relies on `setMark()` zeroing its ratio
  when the palette changes.

**Fix:** one small helper that holds the cached picture, the ratio it was made at and the stale
flag, used by all four items.
