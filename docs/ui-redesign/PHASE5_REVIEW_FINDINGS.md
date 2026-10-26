# Phase 5 review findings (PR #30)

What an `xhigh` `/code-review` of PR #30 ("racks on the rim") found after the PR had been merged.
Nothing here crashed or broke a test.

**All twelve are fixed.** A second review, of the PR that fixed them, found eleven more:
[#13–#23](#second-review-pr-32), all fixed as well.
- #11 was fixed in phase 6's review round (`bbff23d`).
- The other eleven were fixed on `redesign/phase-5-review-fixes`, one commit each. #3 and #12
  share one.
- The hashes are that branch's. The squash merge keeps them reachable through its pull request.
- **Tests:** #1, #2, #4, #6, #8 and #10 each have a test that was run against the old code and
  failed there. #3 and #12 have unit tests of the new `PictureMemo`. #5 and #7 change only how
  the code reads. #9 is checked by the 1920×1080 screenshots: the five table shots are
  pixel-identical before and after (see #9 for the sixth).

The findings below are as they were reported, checked against `redesign/phase-6-trick` at
`6c4ae69`, with line numbers for that commit. A **Fixed** line at the top of each says what
changed.

**Severity:** 1 = code quality only, 3 = a user can see it, 5 = crash or wrong game.
**Complexity:** 1 = a one- or two-line change, 3 = a new helper or API, 5 = a large refactor.

| # | Finding | Sev. | Cplx. | Status |
|---|---|---|---|---|
| 1 | [The rack's shadow is cut away under the cards](#1-the-racks-shadow-is-cut-away-under-the-cards) | 3 | 3 | Fixed (`9ae4fef`) |
| 2 | [`frameRotation()` can return -180](#2-framerotation-can-return--180) | 2 | 1 | Fixed (`6b74526`) |
| 3 | [A null rack picture is kept as current](#3-a-null-rack-picture-is-kept-as-current) | 2 | 1 | Fixed (`2215dc8`, with #12) |
| 4 | [Forehead faces are drawn upside down](#4-forehead-faces-are-drawn-upside-down) | 4 | 2 | Fixed (`98a7fc3`) |
| 5 | [`rackAngle()` returns 0 for "no rack"](#5-rackangle-returns-0-for-no-rack) | 1 | 2 | Fixed (`8e48d5b`) |
| 6 | [The card-back line's corners use one axis](#6-the-card-back-lines-corners-use-one-axis) | 2 | 1 | Fixed (`b4466bd`) |
| 7 | [A test returns an image its painter is still drawing on](#7-a-test-returns-an-image-its-painter-is-still-drawing-on) | 1 | 1 | Fixed (`e2c8342`) |
| 8 | [`setRotation()` repaints even when the angle is the same](#8-setrotation-repaints-even-when-the-angle-is-the-same) | 2 | 1 | Fixed (`f2e5a87`) |
| 9 | [`setDevicePixelRatio()` copies the cached card art](#9-setdevicepixelratio-copies-the-cached-card-art) | 1 | 2 | Fixed (`8a01473`) |
| 10 | [The rack cache empties too soon](#10-the-rack-cache-empties-too-soon) | 1 | 1 | Fixed (`66a8319`) |
| 11 | [The rack cache is a copy of the panel cache](#11-the-rack-cache-is-a-copy-of-the-panel-cache) | 1 | 4 | Fixed (`bbff23d`) |
| 12 | [Every item copies SeatItem's picture memo](#12-every-item-copies-seatitems-picture-memo) | 2 | 3 | Fixed (`2215dc8`) |

**How they were fixed, in order:** #4 first, because players saw it. Then #9, checked on the
1920×1080 screenshots before the racks changed. Then the quick ones (#2, #6,
#7, #10, #8). Then #1, which changes the racks' edges. Then #5, and #12 last, which also fixed #3.

---

## 1. The rack's shadow is cut away under the cards

`src/table/RackItem.cpp:271`. Severity 3, complexity 3.

**Fixed** in `9ae4fef`: `drawShapeShadow()` takes a `Side`, as `drawCircleShadow()` does, and the
rack's row uses `Side::Beneath`. The rail's own shadow is a CSS box-shadow and stays `Outer`. Test:
a shape over its shadow from beneath meets it in one edge: 0.69 at a half-covered pixel, where the
cut-away shadow gave 0.596.

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

**Fixed** in `6b74526`: the turn is snapped to a quarter turn first and brought into range after.
Test: a hair either side of the top seat's angle both give 180, and every turn all the way round is
in range.

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

**Fixed** in `2215dc8`, with #12: the shared `PictureMemo` never keeps a null picture as current.

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

**Fixed** in `98a7fc3`: `rackitem::artTransform()` lays a face half round, so its top is at the
raised end. Tests: the transform, and a pixel test on `render()`'s own output. In it the top-heavy
seven of hearts has its red at the far end; on the old drawing it failed, 2090 far against 2314
near.

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

**Fixed** in `8e48d5b`: the seat form of `rackAngle()` is removed, and its tests read
`seatlayout::place()->angle`. `Place::angle` is still 0 for your own seat, guarded by the `human`
flag beside it.

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

**Fixed** in `b4466bd`: each radius uses its own axis. Test: at four shapes, nothing is drawn
outside the body's corner grown by a pixel.

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

**Fixed** in `e2c8342`: the painter is ended before the image is returned. No other test helper did
this.

The `drawn` lambda returns its `QImage` while the `QPainter` is still active (no `painter.end()`).
`RackItemTests`' `drawnAt` ends the painter before returning.

**What you see:** nothing on today's compilers. Whether it works depends on NRVO. If the copy is not
elided, the returned image is copied or moved from a device with an active raster engine, the
painter then ends on a moved-from image, and the pixel comparison may run on an image that was never
flushed.

**Fix:** call `painter.end()` before `return`.

## 8. `setRotation()` repaints even when the angle is the same

`src/table/TableScene.cpp:192`. Severity 2, complexity 1.

**Fixed** in `f2e5a87`: the racks, the played cards and the rhombus are turned only when their angle
changes. Its test, "A snapshot that changes nothing repaints nothing", also found
`SeatItem::setSeat()` calling `update()` on every snapshot. That repainted every name panel, and
your hand under yours, and it is fixed in the same commit.

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

**Fixed** in `8a01473`: the rack asks the renderer for its art at a ratio of 1, in the same pixels,
so nothing is detached. Scaling the brush by the art's device-independent size was tried first: Qt
drew backs right but a half-turned face (#4) unturned and tiled. Compared on their RGB channels,
the five table shots are pixel-identical to before. The empty table differs by up to 4 levels in 43
pixels of the logo's lettering (y 511–523). That's the band phase 4 found varies between runs, and
it has no racks in it.

`art.setDevicePixelRatio(1.0)` on a `QPixmap` shared with `CardRenderer`'s cache detaches it, so
every `render()` deep-copies the pixels of each card art it uses.

**What you see:** nothing; it is wasted work. Checked: `cacheKey()` differs after
`setDevicePixelRatio` on a shared copy, so the pixels were copied. An 8-card rack copies eight backs
of about 80 × 117 px on every settle.

**Fix:** take `art.devicePixelRatio()` into account in the brush transform, or draw the art as a
`QImage`, instead of changing the ratio.

## 10. The rack cache empties too soon

`src/table/RackItem.h:135`. Severity 1, complexity 1.

**Fixed** in `66a8319`: `PictureCache` drops only its oldest picture at its cap, for every cache,
and the rack cache holds 16.

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

**Fixed** in `2215dc8`: `src/paint/PictureMemo.h` is the one copy, and all four items use it.
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

# Second review (PR #32)

An `xhigh` `/code-review` of PR #32, the branch that fixed #1–#12, found eleven more. They are
numbered on from #12 so a comment can name one without saying which review it came from. Line
numbers are for `07bce0e`, the PR's head when it was reviewed.

**All eleven are fixed** on `redesign/phase-5-review-fixes`, one commit each.
- **Tests:** #13, #14, #15, #18 and #20's own fix have tests that were run against the old code
  and failed there. #16's new assertions don't compile against the old code: the type is the fix.
  #17, #19, #21, #22 and #23 change only how the code reads or how fast it runs. The existing
  tests cover them.
- **The screenshots:** compared on their RGB channels, the five table shots are pixel-identical
  to `07bce0e`'s. The empty table differs by up to 4 levels in the logo's lettering (y 511–523).
  That's the band phase 4 found varies between runs, so a second run may match there or not.
- **#13 and #14 undid part of #3's fix.** #3 and #12 put the null-picture rule into
  `PictureMemo`, but the cache behind the memo still held a null (#13). The memo also dropped a
  good picture for a null one (#14).

| # | Finding | Sev. | Cplx. | Status |
|---|---|---|---|---|
| 13 | [The picture cache holds a null picture](#13-the-picture-cache-holds-a-null-picture) | 3 | 1 | Fixed (`1845035`) |
| 14 | [A null composition replaces the memo's picture](#14-a-null-composition-replaces-the-memos-picture) | 2 | 2 | Fixed (`e6c508f`) |
| 15 | [The cache drops the oldest, not the least recently used](#15-the-cache-drops-the-oldest-not-the-least-recently-used) | 2 | 1 | Fixed (`dc1dc3a`) |
| 16 | [The human's seat still has a rack angle of 0](#16-the-humans-seat-still-has-a-rack-angle-of-0) | 2 | 3 | Fixed (`4e27f4e`) |
| 17 | [The `setRotation()` guard is copied three times](#17-the-setrotation-guard-is-copied-three-times) | 1 | 1 | Fixed (`829b151`) |
| 18 | [The table top keeps its own memo and repaints on every theme](#18-the-table-top-keeps-its-own-memo-and-repaints-on-every-theme) | 2 | 2 | Fixed (`bdfe4de`) |
| 19 | [A shadow beneath its shape fills a buffer of zeros](#19-a-shadow-beneath-its-shape-fills-a-buffer-of-zeros) | 2 | 2 | Fixed (`79cad7f`) |
| 20 | [A test's lambda writes to a list destroyed before its scene](#20-a-tests-lambda-writes-to-a-list-destroyed-before-its-scene) | 1 | 1 | Fixed (`268b587`) |
| 21 | [The plan names the removed seat form of `rackAngle()`](#21-the-plan-names-the-removed-seat-form-of-rackangle) | 1 | 1 | Fixed (`4c46f3d`) |
| 22 | [`RackItem::compositions()`'s comment doesn't match its count](#22-rackitemcompositionss-comment-doesnt-match-its-count) | 1 | 1 | Fixed (`4fb7431`) |
| 23 | [A rack's art and face flags are parallel vectors](#23-a-racks-art-and-face-flags-are-parallel-vectors) | 1 | 1 | Fixed (`3fb14fb`) |

**How they were fixed, in order:** the picture rules first (#13, #14, #15, then #22, whose comment
depends on them). Then the quick ones (#23, #20, #17, #19). Then #18, and #16 before #21, whose
doc names #16's new type.

## 13. The picture cache holds a null picture

`src/paint/PictureCache.h:52`. Severity 3, complexity 1.

**Fixed** in `1845035`: a null render is returned but not held or counted, so the next request
renders again. Test: "A null picture is never held".

`PictureCache` stored whatever `render()` returned. A rack whose render failed once, for example
a canvas that failed to allocate, was handed that null by the cache on every later paint, so the
memo's "never keep a null" rule could not help. The rack drew nothing until the ratio or palette
changed. The same held for the panels and the card shadows.

## 14. A null composition replaces the memo's picture

`src/paint/PictureMemo.h:40`. Severity 2, complexity 2.

**Fixed** in `e6c508f`: a null leaves the held picture, its ratio and its stale flag alone. A
stale picture is still never handed back. Test: "A null picture leaves the one kept alone".

A paint at a degenerate ratio of 0 replaced SeatItem's composed panel with a null, so the next
paint at the real ratio composed the portrait and text again. Before #12, SeatItem's own memo kept
its picture.

## 15. The cache drops the oldest, not the least recently used

`src/paint/PictureCache.h:50`. Severity 2, complexity 1.

**Fixed** in `dc1dc3a`: a hit moves its entry to the back, so the front, which is dropped, is the
least recently asked for. Test: "A full cache drops the picture least recently asked for".

With #10's fix, a full cache dropped the picture drawn first. A run of one-card Forehead rounds
could then evict the rack backs, which are asked for every frame, before faces that are never seen
again.

## 16. The human's seat still has a rack angle of 0

`src/table/SeatLayout.h:94`. Severity 2, complexity 3.

**Fixed** in `4e27f4e`: `Place::angle` and `rackAngle(QRectF)` are `std::optional<qreal>`, empty
for the human's seat and for an empty panel. `human` stays, and `human == !angle`. Tests: the
`place()` case checks the human's seat has no angle, and "An empty panel has no rack angle".

#5 removed the seat form of `rackAngle()`, but `Place::angle` was still 0 for your own seat, and
`rackAngle()` returned 0 for an empty panel. 0 is the right-hand seat's real angle, so a caller that
forgot to check `human` got a valid-looking rack angle.

## 17. The `setRotation()` guard is copied three times

`src/table/TableScene.cpp:197`, `TrickItem.cpp:128`, `LeadMarkItem.cpp:70`. Severity 1,
complexity 1.

**Fixed** in `829b151`: `src/table/Turn.h`'s `turnTo()` holds the guard and the reason for it, and
all three items turn through it.

#8's fix wrote the same guard into each of the three. The next item turned from a snapshot would
have needed a fourth copy, or brought back the repaint.

## 18. The table top keeps its own memo and repaints on every theme

`src/table/TableTopItem.h:76`. Severity 2, complexity 2.

**Fixed** in `bdfe4de`: `TableTopItem` keeps its picture in a `PictureMemo`, and `setTheme()`
invalidates and repaints only when `theme.table` changes. Test: "Switching between light and dark
repaints nothing".

It was the fifth copy of the memo logic that #12 removed from the other four. And while it kept
its picture across a light/dark flip, its `setTheme()` called `update()` every time, so a flip
repainted the whole felt for nothing.

## 19. A shadow beneath its shape fills a buffer of zeros

`src/paint/Shadows.cpp:219`. Severity 2, complexity 2.

**Fixed** in `79cad7f`: beneath, the hole is an empty vector, read as uncovered everywhere.

For `Side::Beneath`, `drawShapeShadow()` allocated and zero-filled a float buffer the size of the
whole picture, only to multiply every pixel by 1 − 0. That happened on every rack render.

## 20. A test's lambda writes to a list destroyed before its scene

`tests/TableSceneTests.cpp:914`. Severity 1, complexity 1.

**Fixed** in `268b587`: `repainted` is declared before the scene, so it outlives the connection.

The lambda connected to `QGraphicsScene::changed` captured `repainted` by reference, and
`repainted` was declared after the scene, so it was destroyed first. The review suggested a
context object, but the only one to hand is the scene, which would still outlive the list. Moving
the declaration is the fix. #18's new test is written the same way.

## 21. The plan names the removed seat form of `rackAngle()`

`docs/ui-redesign/IMPLEMENTATION_PLAN.md:866`. Severity 1, complexity 1.

**Fixed** in `4c46f3d`: the plan reads a seat's angle from `seatlayout::place()->angle`, and says
it is empty for your own seat.

## 22. `RackItem::compositions()`'s comment doesn't match its count

`src/table/RackItem.h:192`. Severity 1, complexity 1.

**Fixed** in `4fb7431`: the comment says it counts the pictures the rack took, and not a request
that came back null.

It said "how many times this rack has asked for a new picture", but `PictureMemo` counts only
pictures that aren't null. A rack with no cards asks on every paint, and the count stays still.

## 23. A rack's art and face flags are parallel vectors

`src/table/RackItem.cpp:273`. Severity 1, complexity 1.

**Fixed** in `3fb14fb`: one vector of `{ pixmap, face }`.

`arts` and `std::vector<bool> faceUp` had to be kept the same length and order by hand, and a
slip would have laid a face unturned or a back turned.
