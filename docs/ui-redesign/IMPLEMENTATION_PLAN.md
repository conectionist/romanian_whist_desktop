# Table redesign: implementation plan

This plan turns the approved HTML mock-up into the Qt client. It is written for an agent (or a
person) picking the work up cold: it says what the target looks like, where every number comes
from, which classes change, in what order, and how to tell each step is done.

**Read first, in this order**

1. This file, top to bottom, once.
2. Open `mockup/romanian_whist_table.html` in a browser and play a few tricks. Switch the player
   count in the menu bar (3, 4, 5, 6). It is the target. Where this document and the mock-up
   disagree, **the mock-up wins** on looks; this document wins on how it maps onto the client.
3. `screenshots/`: the mock-up at 1920×1080, for side-by-side comparison when the client cannot
   be run next to a browser.
4. `docs/QT_CLIENT_PLAN_proposal1.md` sections 12 to 18. They describe the client as it is and
   carry rules this redesign keeps: section 18's "colour is never the only cue", section 4.0's
   snapshot/prompt split, section 4.4's "panels, not `QDialog`s".

**What is in `docs/ui-redesign/`**

| Path | What it is |
|---|---|
| `IMPLEMENTATION_PLAN.md` | This file |
| `mockup/romanian_whist_table.html` | The approved mock-up. Self-contained, works offline (fonts load from Google Fonts when online and fall back otherwise) |
| `mockup/source/page.html`, `mockup/source/table.js` | The mock-up's readable source: markup + CSS, and the script. **Every number in section 2 comes from these two files** |
| `mockup/source/build_mockup.py` | Rebuilds the HTML from the sources plus the client's own card art and textures |
| `mockup/source/capture_hands.py` | Photographs the mock-up's hand with 1, 4 and 8 cards in headless Chrome, at the moment `tests/ScreenshotTool.cpp`'s hand shots draw (added in phase 7) |
| `screenshots/*.jpg` | The mock-up at 1920×1080 with 3, 4, 5 and 6 players, bidding, and a close-up of racks and played cards; and since phase 7 `hand-1-card.jpg`, `hand-4-cards.jpg`, `hand-8-cards.jpg` |
| `reference/inspiration.webp` | The image the owner started from. Mood only; the mock-up supersedes it wherever they differ |

Also added outside this folder, by the same commit:

| Path | What it is |
|---|---|
| `tools/generate_table_textures.py` | Deterministic generator for the two table textures (numpy + Pillow) |
| `resources/images/table/table_wood.png` | 1020×1020 RGBA, the wooden rim. Generated |
| `resources/images/table/table_felt.png` | 880×880 RGB, the felt. Generated |

The textures are committed but **not yet registered in any `.qrc`**. That is phase 1's first step.

---

## 0. Ground rules

- **The engine does not change.** Nothing in `libs/RomanianWhistEngine` is touched. Nothing in
  `src/engine/` should need to change either, apart from possibly exposing data that already exists
  (see phase 11).
- **The snapshot/prompt plumbing does not change.** `GameSnapshot`, `BetPrompt`, `PlayPrompt`, the
  `promptId` round trip and every shutdown path in `MainWindow` stay as they are. This is a
  repaint, not a rewrite of the game flow.
- **Work on the designated feature branch**, one commit per phase (or per coherent sub-step), with
  the build green and the test suite passing at every commit. Never push to `master`.
- **Match the house style**: Allman braces, `for(... ; ... ; ...)` spacing as in the existing code,
  C++20 (`contains`, `starts_with`, `std::numbers`), explanatory comments on *why* in the density
  the surrounding code already has, `tr()` on every user-visible string.
- **Tests are Catch2** under `tests/`. When a test asserts behaviour this plan deliberately removes
  (for example the adaptive card height), rewrite or delete it in the same commit and say why in the
  commit message. Never skip or disable a test to get green.
- **Verify the way `docs/LOCAL_VERIFICATION.md` says**, and check the test COUNT, not only the colour.
- If a decision in section 7 ("open questions") blocks you, use the stated default and note it in the
  commit message; do not stall.

---

## 1. Target in one paragraph

A dark wood-panelled room seen from directly above. A round wooden table (true circle, even rim,
generated wood-grain texture) with a green felt top, a thin gold circle inset on the felt and the
"Romanian Whist" logo plaque in the middle. Every player's name panel sits **outside** the table:
the human at the bottom, the rest round the circle clockwise. Each opponent's face-down hand stands
in a small wooden rack **on the rim**, leaning back towards its owner so only the backs show. Played
cards land on the felt directly in front of each owner's rack, turned to line up with it; a gold
rhombus on the gold circle marks the trick's leader. The human's hand fans face-up on the bottom of
the rim. Fixed panels are pinned to the window corners: scoreboard (with a stacked trump section)
top-left, current-trick panel top-right, "Round history" and "Last trick" buttons bottom-left. A
styled menu bar runs across the top.

---

## 2. Design specification

### 2.1 Coordinate system ("table space")

The mock-up lays everything out in a fixed logical space and scales it uniformly to the window.
**Port that model; it is what lets every number below be copied verbatim.**

- **Stage**: 1536 × 1024 table-space units. The table centre is **(768, 512)**.
- **Fit box**: the stage is scaled to fit a larger box, so the panels outside the table always have
  room. Box = **x from −172 to 1708 (1880 wide), y from −150 to 1174 (1324 tall)**.
  `scale = min(viewportWidth / 1880, viewportHeight / 1324)`, then the stage is centred horizontally
  on its own 1536 width and vertically on the 1324 box. Equivalently: fit `QRectF(-172, -150, 1880,
  1324)` into the viewport with `Qt::KeepAspectRatio`, centred.
  (`table.js` `fit()`; the 1880 was widened from 1820 so the 20%-enlarged scoreboard clears the
  top-left seat in narrow windows.)
- Elements may sit at negative coordinates or beyond 1536: the top seat's panel is at y = −140, the
  human's at y = 1046, left-hand panels reach x = −34.
- **Window-pinned elements** (scoreboard, trick panel, bottom-left buttons) are NOT at fixed table
  coordinates: they are pinned to the window's corners, 8 device-independent pixels in from each
  edge (and 8 px below the menu bar), but drawn at the table's scale. In the mock-up they are
  stage children whose stage coordinates are recomputed on every resize from the viewport corners
  (`table.js` `fit()`: `top = (bar + 8 − y) / scale`, `left = (8 − x) / scale`, etc.).

### 2.2 Palette

From `page.html` `:root` and the rules that use it. The look is **dark only** (see open question Q1).

| Token | Value | Used for |
|---|---|---|
| room | radial gradient, ellipse at (50%, 42%): `#3b2414` 0% → `#1c110a` 58% → `#0b0704` 100% | Whole window behind the table |
| wood base | `#5a331a` | Under the wood texture |
| felt base | `#1d5a3a` | Under the felt texture |
| panel gradient | top `rgba(40,29,19,0.95)` → bottom `rgba(19,13,8,0.95)` | Every panel |
| panel border | `#806137`, 1 px | Every panel |
| panel inner lines | 3 px `rgba(18,12,7,0.9)`, then 4 px `rgba(201,164,92,0.3)` (inset) | Every panel |
| panel shadow | 0 12 26 `rgba(0,0,0,0.55)` | Every panel |
| gold | `#c9a45c` | Icons, borders, rhombus outline |
| gold-bright | `#f2d38c` | Active ring, hover, prompt text, winning glow, lead rhombus |
| cream | `#f1e6cc` | Primary text |
| muted | `#bba783` | Secondary text, labels |
| good | `#86c98f` | Taken/Bid on target |
| warn | `#e6b95c` | Taken/Bid still owing |
| bad | `#e5806d` | Taken/Bid over |
| red suit | `#e0524a` | ♥ ♦ glyphs in text |
| black suit | cream | ♠ ♣ glyphs in text |
| green button | gradient `#2a6142` → `#17402a`, border `#a3824a` | Bid buttons, modal actions |

### 2.3 Typography

| Role | Family | Fallback | Where |
|---|---|---|---|
| Logo | Cinzel Decorative 700 | Cinzel, Georgia, serif | Centre plaque only |
| Headings | Marcellus 400 | Cinzel, Georgia, serif | Names, panel titles, buttons, menu items, suit names |
| Body | Alegreya Sans 400/500/700 | Gill Sans, Segoe UI, sans-serif | Everything else; labels are 700, uppercase, letter-spaced |

All three are SIL Open Font License (Google Fonts). Bundle the TTFs under `resources/fonts/` with
their `OFL.txt`, register them in a `.qrc`, and load them once at startup with
`QFontDatabase::addApplicationFont`. If fetching them is impossible from the build environment, keep
the code path and the fallbacks, and leave a clear note; do not block the rest of the work on fonts.

All sizes below are in **table-space units** (they scale with the table), not points.

### 2.4 Table

All centred on (768, 512). `page.html` `.wood`, `.felt`, `.felt-line`, `.logo`.

| Layer (bottom to top) | Geometry | Paint |
|---|---|---|
| Table shadow | circle r = 510 | blurred 70, `rgba(0,0,0,0.75)`, no offset (light is straight above) |
| Wood | circle r = 510 | `table_wood.png` scaled to 1020×1020 over `#5a331a`; inset stroke 2 `#2a160a`, then 4 `rgba(255,210,150,0.12)`; inner shadow blur 22 `rgba(0,0,0,0.5)` |
| Bead | three rings just outside the felt | r 440→449 `#3b210f`, 449→451 `#7a4c27`, 451→453 `#2a160a` |
| Felt | circle r = 440 | `table_felt.png` scaled to 880×880 over `#1d5a3a`, then a radial vignette: `rgba(255,255,255,0.07)` at 0%, transparent at 45%, `rgba(0,0,0,0.38)` at 100%; inner shadows blur 60 `rgba(0,0,0,0.55)` and blur 18 `rgba(0,0,0,0.45)`. **The vignette's 100% is at r ≈ 622, not at the felt's edge**: it is `radial-gradient(circle, …)`, whose default size is farthest-corner of the 880 box, so the edge at 440 shows the dark stop at about 0.18 |
| Gold circle | circle r = **416**, 1.5 wide | `rgba(201,164,92,0.2)`. **r 416 is its outer edge**: the mock-up draws it as a CSS border inside a box inset 24, so the line runs 414.5–416, centred at 415.25 |
| Logo plaque | 243 × 128 centred (270×142 scaled 0.9) | see below |

**Logo plaque** (`.logo`, scaled 0.9 about its centre): radial gradient at (50%, 40%) `#2a6142` 0% →
`#17402a` 70% → `#0f2c1d` 100%, sized farthest-corner over the padding box; border 2 `#b8924a`, radius
14; inner line inset 6, 1 px `rgba(232,200,130,0.55)`, radius 9. **It is `class="panel logo"`, so it
also has `.panel`'s box-shadows:** a drop shadow 0 12 26 `rgba(0,0,0,0.55)`, and inset rings of 3 px
`rgba(18,12,7,0.9)` over 4 px `rgba(201,164,92,0.3)`, which leave a 1 px gold line 3 px inside the
border. Text **"Romanian" / "Whist", in mixed case** (Cinzel Decorative draws a lowercase letter as a
small capital, so uppercase would look different) on two lines, Cinzel Decorative 700, 37 px, line
height 1.0, letter-spacing 0.01em, filled with one vertical gradient over both lines `#fbe7b2` 0% →
`#e2b866` 48% → `#a77a35` 100%, and a 2 px drop shadow `rgba(0,0,0,0.55)`. A ♣ in `#d9b46a`, 22 px,
16 px in from each side of the padding box, vertically centred. **Two different ornaments**, 120×12,
stroke 1.2 in `#d9b46a`: above the text two straight strokes with a diamond between and a dot either
side of it; below, the same strokes as waves and the diamond, no dots (the inline SVGs in
`page.html`). Draw it under the played cards.

### 2.5 Seats

`table.js` "geometry" section.

**Seat order and angles.** Seats go clockwise from the human at the bottom: seat *s* (counted from
the human) sits at angle `90° + s·360°/n`, with y pointing down (so 90° is straight down). This is
section 14's rule; the existing `seatlayout::seatCentres()` already encodes the order.

**Name-panel positions** (top-left corner in table space; width 280, height 118). Hand-tuned per
player count so every panel sits just outside the wooden rim (circle r ≈ 522) and clears the corner
panels:

| n | seat 1 | seat 2 | seat 3 | seat 4 | seat 5 |
|---|---|---|---|---|---|
| 2 | (628, −140) | | | | |
| 3 | (96, 49) | (1160, 49) | | | |
| 4 | (−34, 453) | (628, −140) | (1290, 453) | | |
| 5 | (−24, 611) | (96, 49) | (1160, 49) | (1280, 611) | |
| 6 | (1, 700) | (96, 49) | (628, −140) | (1160, 49) | (1255, 700) |

The 2-player row is not in the mock-up (it stops at 3): the opponent takes the top position, which
follows directly from the 4- and 6-player rows. **Seat 0 (the human)**: left 618, top 1046, width
300; centre (768, 1105).

**Spectated game** (no human seat, `humanSeat < 0`): section 14 lays seat 0 out as though it were
the human. Do the same: seat 0's panel goes in the human's spot (618, 1046) but it is drawn as an
opponent: a rack on the rim at angle 90° (bottom), no hand.

**Name panel** (`.plaque`): panel style; padding 8 (left, top, bottom) and 16 (right); 12 gap.
- Portrait 88×100, radius 7, border 1 `#a3824a`, slight sepia. Use `Theme::avatar(seat, box, dpr)`:
  replace its painter with a head-and-shoulders bust (see `portrait()` in `table.js` for shapes
  and per-seat colours; it is six hand-drawn variants indexed by seat, which matches avatar()'s
  existing per-seat contract).
- Line 1: name, Marcellus 25, cream, followed by the total score in brackets in muted:
  `Bogdan (44)`. No "AI" tag (see Q5).
- Line 2: a pill, Alegreya Sans 19, padding 2/12, fully rounded, 1 px border
  `rgba(201,164,92,0.4)`. Text **`Taken/Bid: w/b`** (tricks taken first, then the bid). States:
  before bidding "No bid yet", while this seat is bidding "Bidding…", neutral colours; once play has
  started the pill is green (good) when taken == bid, amber (warn) while taken < bid, red (bad) when
  taken > bid. Same three states `SeatItem::Pill` already models; the text carries the numbers so
  colour is never the only cue (section 18).
- Active seat (whoever is being asked to move): a 3 px gold-bright ring round the panel plus a soft
  gold glow (blur 26, `rgba(242,211,140,0.5)`), and a "▸" before the name. Not shown while a full
  trick is waiting to be cleared.
- When a bot bids, a small gold "Bids 2" bubble floats over its panel for ~1.6 s (`.bubble`).
  Optional; see phase 12.

### 2.6 Racks (opponents' face-down hands)

`table.js` `buildSeats()` and `renderFans()`; `page.html` `.fan`, `.fan .back`, `.fan .ledge`.
Screenshot: `screenshots/detail-racks-and-played-cards.jpg`.

**Where.** For each non-human seat, let `a` be the angle from the table centre to the centre of the
seat's name panel. The rack's origin is on the rim at radius **452**: `(768 + 452·cos a, 512 +
452·sin a)`. If that point falls inside the seat's own panel rectangle grown by 10 on every side (only
the top seat, whose panel overlaps the rim), step `a` clockwise by 1° until it no longer does, then
add a further 9° so the row's width clears the panel corner. Remember the final `a` per seat
(`rackAngle[s]`); the played card and the lead rhombus use it. *(Phase 5: with the table of
section 2.5 the slide never fires. Every rim point is already clear of its panel grown by 10; the
top seat's panel ends at y −22 and its rim point is at y 60. The client implements the rule
anyway, and `seatlayout::place()` is where to read the result.)*

**Frame.** Rotate the rack's local frame by `φ = atan2(−cos a, sin a)` so local +y points
**outwards**, towards the owner, and local −y points at the table centre.

**Cards.** One back per card left in the hand, 62 × 90, side by side along local x, 26 apart,
centred on the origin; card *i* is also rotated in-plane by `(i − mid)·(−2°)`. Each card's top edge
sits at local y = 4 and the card hangs towards +y. It is then **tilted 50° out of the table about
its top edge** (CSS `rotateX(50deg)`, `transform-origin: 50% 0`), so its far end rises towards the
viewer and leans back over the owner's side, and seen through a perspective camera at distance 300
centred on local (0, 40). Net effect: each back is a trapezoid, narrow at the rack and wider at the
far end. Shading: the back gets a gradient that is light `rgba(255,240,210,0.16)` at the far end and
darkens to `rgba(0,0,0,0.3)` at the rack, transparent from 45% of the way between, plus an inset
1 px `rgba(0,0,0,0.25)` line and a radius of 4. The whole row (cards and rail) casts one drop shadow
(0, 5, blur 5, `rgba(0,0,0,0.55)`) in the rack's frame, so it falls towards the owner.

**Qt implementation of the tilt.** `QTransform` supports projective transforms, so each card is a
plain pixmap drawn through a quad-to-quad mapping. **Project last, as CSS does** (corrected in
phase 5; this paragraph used to project first). Each `.back` is `translateX(t) rotate(θ)
rotateX(50°)` about its top-centre (0, 4), and the `.fan`'s perspective projects the result. So for
each corner `(u, v)` of the flat card (`u = ±31`, `v` 0 or 90, measured from that top-centre):
1. Tilt it: `y = v·cos 50°`, `z = v·sin 50°`.
2. Turn it in the plane by θ.
3. Add `t` to x and 4 to y.
4. Only then project it, with `k = 300 / (300 − z)`: `X = x·k`, `Y = 40 + (Y − 40)·k`.

Projecting first gives the outer cards' far ends too little spread.
`QTransform::quadToQuad(cardRect, projectedQuad, t)` then maps the flat card onto its quad, and the
item's rotation `φ` turns the whole rack into place. No OpenGL needed.

**Rack rail.** A wooden bar lying flat on the rim on the table side of the cards: width `(n−1)·26 + 78`,
height 16, local y from −9 to +7, radius 3. It's filled with `#6b3d1f` plus a gradient (light
`rgba(255,220,170,0.35)` at the top edge fading out by 30%, `rgba(0,0,0,0.45)` at the bottom), a
1 px dark outline `rgba(30,14,4,0.6)` and its own drop shadow (0, 2, blur 3, `rgba(0,0,0,0.6)`).
- **No wood texture, whatever `page.html` says.** It centres `table_wood.png` on the rail at
  900×900, and that texture is the rim, a ring that's transparent in the middle, so only the base
  colour shows (found in phase 5).
- **It's raised `translateZ(8px)`,** so the perspective magnifies it by 300/292 about (0, 40):
  y −9..7 becomes about −10.3..6.1, and it gets slightly wider.

**Draw it above the cards** so it covers their bottom edges. An empty hand draws neither cards
nor rail.

**Forehead rounds.** Section 17: in a Forehead round the human sees every other seat's single card.
Today `SeatItem` shows that card face-up on the plaque. In the new layout show it **face-up in the
rack** instead (same geometry, the face instead of the back). Hidden rounds: backs, as normal. The
rule for *whether* a face is visible stays exactly where it is (`SeatView::hand` non-empty, per
section 17's table); only where it is drawn moves. Keep the test "Your own seat shows no face in a
Forehead round" true.

**Card back art.** `CardRenderer::back()` is currently a placeholder painter. Replace its body with
the mock-up's back (`backSvg()` in `table.js`): cream border `#f2e8d2`, burgundy `#7a1c1c` field,
a 45°-rotated 9-unit lattice of faint gold `#d2ac63` lines with small `#a8392b` dots, a gold
`#d8b56a` inner frame, and a central burgundy oval with a gold diamond. Keep `back()`'s signature and
cache behaviour; it is already a single seam for exactly this.

### 2.7 Played cards and the lead rhombus

`table.js` `trickPos()` and `renderTrick()`.

- **Size**: with n ≤ 4, 141.6 × 205.2; with n ≥ 5, 124.8 × 181.2.
- **Opponent's card**: centre on the line from the table centre at angle `rackAngle[s]`, at radius
  `r = 440 − 60 − h/2` (60 inside the felt's edge). Rotation `a − 90°` (degrees), so the card lines
  up with its owner's rack. No random tilt.
- **Human's card**: centred at x = 768, bottom edge 60 above the top of the hand (hand top = 850),
  i.e. top-left (768 − w/2, 850 − 60 − h), unrotated.
- **Every played card casts a shadow** (`.tcard .inner`): radius 7, `0 6px 14px rgba(0,0,0,0.45)`,
  in the card's own turned frame, so the top seat's falls up the screen, as the racks' do.
  *(Added in phase 6; this list used to give the winner's shadow only.)*
- **Winning card** (`PlayedCardView::isWinning`, **once at least two cards are down**: the engine
  ranks a lone first card as winning, and table.js draws no highlight for it): lifted 8 along its
  own "up", which points at the table centre for every seat, with a 2 px gold-bright ring round the
  card's edge (radius 9), a gold glow (blur 26, `rgba(242,211,140,0.55)`) and a deeper shadow
  (`0 16px 24px rgba(0,0,0,0.5)`) in place of the plain one. All three are CSS box-shadows on the
  element that lifts, so they lift with it, and none of them is drawn under the card itself. The
  existing rule that the highlight comes from the engine, never recomputed, stays.
- **Lead rhombus**: replaces the old pip under the first card. During the play phase, mark the seat
  that leads the current trick. Opponent: at radius **416** (on the gold circle) at `rackAngle[s]`.
  Human: at (768, 820), between their card and their hand. Shape: an 11×11 square rotated 45°,
  then stretched 1.45× along the radial direction (`rotate(a − 90°) · scaleY(1.45) · rotate(45°)`),
  gold-bright fill, 1 px `rgba(60,40,10,0.5)` outline, glow blur 10 `rgba(242,211,140,0.8)`. The
  outline and the glow are box-shadows of the square, so the stretch stretches them too.
  When: only while the engine's phase is `Playing` (`GameSnapshot::phase`, from
  `GameEngine::getPhase()`), as table.js draws it in its play phase only. That runs from
  `onBettingComplete`, before anyone is asked for a card, until the round's last trick is won.
  Which seat: the first card's owner once a card is down, including while a full trick waits to be
  cleared; before that, `GameSnapshot::leadSeat`, the engine's leader for the trick about to
  start. *Not* worked out from the bids and the seat on turn: during the last bid's frame every
  bid is in and the last bidder is still on turn, and once the round is scored its last trick
  is still on the table. (Corrected in phase 6's review; phase 6 first read `betsPlaced` and
  `activeSeat`.)

### 2.8 The human's hand

`table.js` `renderHand()`; `.hcard`.

- Card **154.9 × 225** (the same size as the trump card on the scoreboard). The client's art makes
  it 155 × 225 (`CardRenderer::sizeForHeight()`), and the formulas below use the card's real width.
- n cards: `step = min(84, (720 − 154.9)/(n − 1))`, total width `(n − 1)·step + 154.9`, centred on
  x = 768. Card *i*: left `768 − total/2 + i·step`, top `850 + (i − mid)²·1.6`, rotated
  `(i − mid)·1.9°` about the point (w/2, 1.3·h) in the card's own frame. Later cards on top.
- **Every card casts a shadow** (`.hcard .face`): `-3px 4px 10px rgba(0,0,0,0.45)`, radius 8, in
  the card's own turned frame. *(Added in phase 7; this list used to give none.)*
- Hover on a legal card lifts it 30 along its own up. It then casts a 2 px gold-bright ring, a glow
  `0 0 22px rgba(242,211,140,0.7)` and a shadow `0 14px 22px rgba(0,0,0,0.5)` in place of the plain
  one (`.hcard.playable:hover .face`). **The card stays hovered over its resting place as well as
  where it has risen to**: the browser hovers the button, which does not move, and its face, which
  does. Hovering only the risen card makes a card flicker when the cursor is near its bottom edge.
  The next card still wins where it overlaps the risen part, since it is drawn on top. *(Phase 7.)*
  - **A card under a cursor that hasn't moved rises too,** as soon as the hand becomes playable or
    is redrawn: the browser applies `:hover` again after every `render()`, with no mouse move.
    *(Phase 7's review.)*
  - **In the strip a raised card rose out of, a click plays the raised card**, though the card
    beneath (or the felt) now shows there. The mock-up does the same, since the browser still hits
    the button. Matching the drawing there would bring the flicker back. *(Phase 7's review.)*
- Illegal cards are dimmed: brightness 0.55, then saturation 0.55, opaque, as the mock-up does.
  **Decided by the owner, 2026-10-20** (phase 7): the client's old 45% opacity let the card beneath
  show through a hand that overlaps by half. Section 18 of `QT_CLIENT_PLAN_proposal1.md` carries the
  amendment. Order, legality, the snapshot vs prompt source, blind backs and keyboard selection are
  all `HandItem`'s existing behaviour and do not change.
- The human's panel (z above the hand) covers the bottom of the hand. That is intended.

### 2.9 Scoreboard (top-left)

`page.html` `.score`, `table.js` `renderScore()`. Merges today's `InfoStrip` into the scoreboard.
Screenshot: any `table-*.jpg`.

Panel, width 236, padding 12/14, **drawn at 1.2× the table scale**, pinned 8 from the window's left
edge and 8 below the menu bar, growing right and down. Content, top to bottom:

1. "Round 11 of 30", Marcellus 22, centred, one size for the whole line.
2. "6 cards dealt · Normal", 15, muted, centred. "1 card dealt" in the singular. Forehead and Hidden
   are named here (section 17 asks for them to be called out; use the accent colour for those two).
3. The bid tension line, 14.5, muted, centred, the number in bold cream:
   "Bids so far: **3** of 6 tricks" while bidding; then "Bids total **7** for 6 tricks: over by 1" or
   "… under by 1". `betsPlaced` and `betSum` are already on the snapshot.
4. A table, rows in **bid order** (`SeatView::bidOrder`): header PLAYER / BID / TAKEN / PTS in
   12 px bold uppercase muted letter-spaced 0.1em with a faint rule under it; rows 17 px, numbers
   right-aligned in tabular figures; "–" for a bid not yet made and for "taken" during bidding.
   The row of whoever is to move is gold-bright with a "▸".
5. A faint divider, then the **trump section stacked and centred**: "TRUMP" (19.8, bold uppercase,
   letter-spaced 0.24em, muted); the trump card at **154.9 table units wide** (same as a hand card,
   radius 10, shadow); the suit name, Marcellus 19.8, e.g. "♣ Clubs" (♥/♦ glyph in red suit
   colour). In an 8-card round: "TRUMP" then "None this round", no card.

The round-score and streak columns of today's scoreboard are not in the mock-up (see Q3).

### 2.10 Current-trick panel (top-right)

`page.html` `.trickinfo`, `table.js` `renderTrickInfo()`. Replaces the prompt bar's hint line.

Panel, width 264, padding 12/16, body 17 px line-height 1.45, **drawn at 1.2× the table scale**,
pinned 8 from the window's right edge and 8 below the menu bar, growing left and down.

- Play phase: title "Trick 3 of 6" (Marcellus 21); "Leader: **Bogdan** · Led ♥"; "Winning:
  **Costel, K♥**" (or "–"); then, above a faint rule, the prompt in gold-bright.
- Bidding: title "Bidding"; "First to bid: **You**"; prompt "Your bid. How many tricks will you
  take?" or "Ana is thinking…".
- Between rounds: "Round over" / "Scores are in."
- Prompt texts during play: "Your turn. You lead." / "Your turn. You must follow ♥." /
  "Your turn. No ♥, so you must trump." / "Your turn. Any card." / "Ana is thinking…" /
  "Ana wins the trick." / "You win the trick.". Reuse `promptbar::fieldsFor()` for the "must follow"
  logic (it is already tested) and extend it; do not reimplement legality.

### 2.11 Bottom-left buttons

Two panel buttons, height 76, padding 0/22, Marcellus 21, 24 px gold icon then the label: **Round
history** and **Last trick**, 14 apart, pinned 8 from the window's left and bottom edges, at the
table scale (1.0). Hover: brighter inner line and a gold glow. What they open: phase 11.

### 2.12 Menu bar

`page.html` `.menubar`. 46 px tall (this one is in device-independent pixels, not table units), a
vertical gradient `#2e1c10` → `#1a1009`, a 1 px `#806137` bottom border and a soft shadow. Items are
Marcellus 17 with an 18 px gold line icon, 12 px horizontal padding, rounded hover background
`rgba(201,164,92,0.13)`. Left: **New Game**, **Settings**, **Rules**. Far right: **Exit**. The mock-up
also has a **Players 3–6** switch: that is a mock-up control only, do not port it (player count is
chosen in `NewGameDialog`).

### 2.13 Bid strip and modal sheets

- **Bid strip** (`.bidstrip`): panel at table (428, 486), width 680, padding 16/22/18, centred text.
  Title "How many tricks will you take?" Marcellus 25. One button per legal bid 0…handSize, 58×62,
  radius 8, green-button style, Marcellus 28. The forbidden bid is disabled visibly: background
  `#2a211a`, text `#6d604d`, border `#4d3d28`, struck through. Note under it 16 px muted: "You bid
  last. 3 would make the bids add up to 6, and somebody has to be wrong." or "2 bid so far,
  totalling 3 of 6 tricks.". Keep `BidStrip`'s behaviour, keyboard digits and tests; only its look
  and position change. Screenshot: `screenshots/bidding.jpg`.
- **Modal sheets** (round summary, history, last trick, rules, settings, standings): scrim
  `rgba(8,5,3,0.62)`, a centred panel 720 wide, padding 26/30/24, title Marcellus 32, meta line 17
  muted, tables 18 px tabular with 13 px uppercase muted headers, positive scores good-green with
  "+", negative bad-red with "−". Actions bottom-right: Marcellus 19, padding 10/20, radius 8, green
  button style; a "quiet" variant with a transparent background. `RoundSheet`, `GameOverPanel` and
  `PauseOverlay` keep their mechanics (they are panels, not dialogs, per section 4.4) and take this
  look.

---

## 3. How the mock-up maps onto the client

| Mock-up | Client today | Change |
|---|---|---|
| Fixed 1536×1024 stage scaled to a 1880×1324 box | `TableView::resizeEvent` sets the scene rect to the viewport size; `TableScene::cardHeightFor()` binary-searches a card height that fits | Fixed table space + a view transform (phase 2). The adaptive card-height search goes |
| Room, wood, felt, bead, gold circle, logo | `TableScene::drawBackground` blits `Theme::background()` | Room gradient stays in `drawBackground`; the table becomes items or a cached layer (phase 3) |
| Seat panels outside the table | `SeatItem` on an ellipse inside the table (`seatlayout`) | New per-n position table, new panel look (phase 4) |
| Racks on the rim | Card count drawn as a small stack on the plaque | New `RackItem` (phase 5) |
| Played cards in front of racks, lead rhombus | `TrickItem` on its own ring, pip under the first card | New positions, rotation, rhombus (phase 6) |
| Hand on the bottom rim | `HandItem` in a reserved strip at the bottom | New geometry, same behaviour (phase 7) |
| Scoreboard with stacked trump | `InfoStrip` (top) + `ScoreboardWidget` (right column) | One panel, pinned top-left (phase 8) |
| Current-trick panel | `PromptBar` (bottom) | Pinned top-right panel (phase 8) |
| Round history / Last trick buttons | Nothing | New (phases 8 and 11) |
| Menu bar | `QMenuBar` with a View menu; main-menu page and pause menu | Styled bar, new top-level actions (phase 9) |
| Bid strip in the felt | `BidStrip` overlay above `TableScene::overlayBottom()` | Restyle, reposition (phase 10) |

**Recommended structure for the pinned panels.** Keep them as `QWidget`s (so their existing pure
functions and tests survive) but host them **in the scene through `QGraphicsProxyWidget`**, so they
scale with the table exactly as in the mock-up. After every resize, compute each panel's scene
position from the viewport's corners with `mapToScene()` and the panel's own scale (1.2 for the
scoreboard and the trick panel, 1.0 for the buttons). Proxy widgets render text as vectors under
the view transform, so they stay crisp. If proxies cause problems (focus, HiDPI), the fallback is
plain child widgets of the viewport with every size multiplied by the view's scale; it is more code.
Whichever you choose, choose it once for all pinned panels.

---

## 4. Phases

Each phase lists files, steps, tests, and how to know it is done. Commit at the end of each one.

### Phase 0: baseline ✅ COMPLETE

1. Install Qt 6.5 or newer with the Widgets, Svg and LinguistTools modules (system packages such as
   `qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools`, or
   `aqtinstall`). `docs/LOCAL_VERIFICATION.md` has the exact preset commands.
2. Build and run the full test suite on the untouched branch. **Record the test count.**
3. For screenshots without a display: `QT_QPA_PLATFORM=offscreen` and `QWidget::grab()` on the
   main window. Write a tiny throwaway helper (do not commit it unless it is useful) that starts a
   seeded game with 4 and 6 players and saves PNGs, to compare against `screenshots/`.

Done when: the baseline builds and passes, and you can produce a screenshot of the current client.

**Outcome** (PR #25):

- **The baseline is 259 tests**, passing on the `default`, `release` and `clang` presets with
  Qt 6.11.2. Later phases compare their count against this one.
- **The screenshot helper was worth keeping**, so it is committed as a hidden Catch2 case,
  `tests/ScreenshotTool.cpp`. Run it with
  `WHIST_SHOTS_DIR=<dir> ./build/tests/desktop_tests "[.screenshot]"`. It saves
  `table-3-players.png` to `table-6-players.png` and `bidding.png` from a seeded game with every
  setting pinned, and checks which moment each picture shows, so pictures taken before and after
  a phase are comparable.

### Phase 1: assets and theme ✅ COMPLETE

Files: `CMakeLists.txt`, `tests/CMakeLists.txt`, new `resources/table.qrc`, new
`resources/fonts.qrc` + `resources/fonts/`, `src/paint/Theme.h/.cpp`, `src/main.cpp`,
`tests/ThemeTests.cpp`.

1. Register `resources/images/table/table_wood.png` and `table_felt.png` in a new `table.qrc`
   (prefix `/table`). **Read the long comment in `CMakeLists.txt` about the three silent ways to
   embed nothing**, and use the same `qt_add_resources(VAR file.qrc)` + `target_sources` form, for
   the app AND, separately, for `desktop_tests` (see the comment in `tests/CMakeLists.txt`). PNG
   needs no imageformats plugin, which the deploy step strips.
2. Fonts: add the three families (section 2.3) with `OFL.txt`, a `fonts.qrc`, and register them at
   startup in `main.cpp` before any widget is created.
3. Theme: add the palette tokens from section 2.2 as members (the struct's own comment says every
   colour lives there and nowhere else). Add `QPixmap woodTexture() / feltTexture()` accessors that
   load once from the resource and share the result like the other caches. Update `paletteKey()`
   (the comment above it explains why every colour must be in it). *Done, and then replaced in
   the phase's review: `paletteKey()` no longer exists. The caches compare a copy of
   `ThemeColours` instead; see the outcome below.*
4. Decide Q1 (light/dark). Default: the table page uses the new dark palette in both schemes; keep
   `Theme::light()` / `dark()` alive for the main-menu page and dialogs. Write the decision into
   the comment at the top of `Theme.h`.
5. Tests: both textures load from the resource at their expected sizes; every new colour is in
   `paletteKey()` (now: recolouring invalidates both caches, as above); fonts are registered
   (`QFontDatabase::families()` contains them).

Done when: the app still looks as it did, but the resources, fonts and tokens exist and are tested.

**Outcome:**

- **The count is 267 tests** (259 + 8), passing on the `default`, `release` and `clang` presets
  with Qt 6.11.2 and building without a warning. The screenshot tool's five pictures are
  pixel-identical to phase 0's, so the client really does look as it did.
- **Q1 and Q2 were decided by the owner** on 2026-10-17, both as the defaults (see section 7).
- **What later phases take from here:**
  - **Colours:** `Theme::table`, a `Theme::TablePalette` holding section 2.2's tokens under
    camelCase names (`panelTop`, `goldBright`, `good`/`warn`/`bad`, `redSuit`, `buttonTop`, …).
    `light()` and `dark()` fill it from one function, so the table cannot drift between schemes.
    The old members stay until their last reader moves across. **Every colour goes in
    `ThemeColours`** (which `TablePalette` is part of), never on `Theme` itself: the caches
    compare `ThemeColours` with its defaulted `==`, so a member added there needs no other edit,
    and `Theme.cpp` static_asserts that `Theme` adds nothing beside its cache.
  - **Textures:** `Theme::woodTexture()` and `Theme::feltTexture()` return the source art at its
    native size. Phase 3 composes and caches the table at view size × dpr.
  - **Fonts:** `src/paint/Fonts.h`. `fonts::registerBundled()` is called by `main.cpp` and by
    `tests/TestApp.cpp`, as `registerMetaTypes()` is, and `fonts::logo()` / `heading()` /
    `body()` return section 2.3's fallback chains as `QFont`s, with the CSS generic family as a
    style hint (Qt does not read `serif` or `sans-serif` as names). Copy one and set the size and
    weight on the copy, rather than writing the family names again.
- **Alegreya Sans Medium has two family names**: the typographic "Alegreya Sans" and the legacy
  "Alegreya Sans Medium". Qt files it as weight 500 of "Alegreya Sans" on Linux and on the
  Windows CI runner, which `FontsTests.cpp` asserts.
- **Pitfall: the bundled fonts changed the Windows test suite's default font.** The offscreen
  platform's default family is "Helvetica". Linux and macOS substitute a real face for it.
  Windows has no substitution there, so Qt picks the best match and breaks ties alphabetically,
  and "Alegreya Sans" sorts in front of every system face. The first CI run failed
  `TableViewTests`' squeezed-strip case because the narrower face stopped wrapping.
  `tests/TestApp.cpp` now pins the Windows offscreen default to Segoe UI, the system UI font the
  shipped app gets from the windows platform plugin, and leaves any other platform alone. A
  `FontsTests` case asserts that the default is none of the bundled faces, so a recurrence fails
  by name.
- **Pitfall:** Qt's `.qrc` dependency scan is a regex, not an XML parser. Writing the entry tag's
  name in angle brackets inside a comment produces a Makefile that fails with "missing separator".
  `resources/fonts.qrc` says so where it happened.
- **Pitfall:** `QPixmap(fileName)` files every load in the process-wide `QPixmapCache` (10 MB by
  default, shared with QStyle and QIcon), so two loads of one file share a `cacheKey` even with no
  cache of our own. Theme reads its textures through `QImageReader` and `QPixmap::fromImage()`
  instead, which keeps ~7 MB of textures out of that budget. Load any other large art the same
  way.

### Phase 2: table space and the view transform ✅ COMPLETE

Files: `src/table/TableView.*`, `src/table/TableScene.*`, `src/paint/Theme.*`, tests
`TableViewTests.cpp`, `TableSceneTests.cpp`, `ThemeTests.cpp`.

1. `TableScene`: a fixed scene rect `QRectF(-172, -150, 1880, 1324)`; constants for the stage
   (1536×1024) and the table centre (768, 512) in one header (for example a new
   `src/table/TableSpace.h`, a namespace of `constexpr`s) so no other file hard-codes them.
2. `TableView::resizeEvent`: stop resizing the scene; instead `fitInView(sceneRect(),
   Qt::KeepAspectRatio)` (or set the transform by hand to match section 2.1 exactly), no scroll
   bars, centred. The room gradient (`drawBackground`) must cover the whole viewport, not just the
   scene rect: draw it in viewport coordinates.
3. Remove `cardHeightFor()`, `fitsOnTable()`, `trickRingRect()`, `seatRingRect()`, `handRect()`,
   `tableRect()` and `Theme::cardHeightFor()` once nothing uses them, with their tests. Card sizes now
   come from section 2 (hand/trump 154.9×225; played 141.6×205.2 or 124.8×181.2; rack 62×90).
4. Keep `overlayBand()` working for `BidStrip` until phase 10 replaces it, or move the bid strip now
   if that is simpler; whichever you do, keep "the bid strip's band survives" true in spirit.

Done when: resizing the window scales everything uniformly, with no layout jumps, and the tests that
remain pass.

**Outcome:**

- **The count is 279 tests**, passing on the `default`, `release` and `clang` presets with Qt 6.11.2
  and building without a warning. The phase started from 268, not phase 1's 267: phase 1's review
  added a case after its outcome was written. Six cases were retired (the two `Theme::cardHeightFor()`
  cases, and the four `TableSceneTests` cases that asserted `handRect()` / `tableRect()`) and 17 added.
  **The review of PR #27 brought it to 290.**
- **Three decisions by the owner, 2026-10-17:**
  - **Interim seats:** today's plaques on one fixed ellipse,
    `seatlayout::seatCentres(n, humanSeat, ringRect(centre, 662, 593))`, at a plaque unit of 150. That
    is 275×119, close to section 2.5's 280×118. The three constants are in `TableScene.cpp`'s anonymous
    namespace, and **phase 4 deletes them** when it brings in section 2.5's position table. The
    diagonal plaques reach about 25 units onto the wooden rim until then, never the felt.
  - **The room now:** `Theme::room()` paints section 2.2's gradient for the table page.
    `Theme::background()` stays green for the main menu and the Card art window.
  - **A minimum table scale of 0.30**, decided in PR #27's review. `tablespace::minimumViewport()`
    (596×416) is `TableView`'s minimum size hint, and the layouts carry it up to the window, which
    stops at 832×580 with the scoreboard. At the floor, hand cards are about 68 px and plaque names
    about 9 px.
- **What later phases take from here:**
  - **Coordinates:** `src/table/TableSpace.h`, a namespace of the section-2 numbers (`centre`,
    `fitBox`, `feltRadius`, `handTop`, the hand's card height, the view margins, `minimumScale`), plus
    `fitScale()`, `minimumViewport()` and `playedCardHeight()`. Phase 5 added the racks'
    `rackRadius` and `rackCardHeight`. No other file writes those numbers. The scene rect is `fitBox` and never
    changes. `TableScene::relayout()` is private and runs on a snapshot or a theme change only; a
    resize changes `TableView`'s transform and nothing else, which `TableViewTests` asserts item by
    item.
  - **Placement:** the hand is at section 2.8's height and card size, still today's flat fan
    (phase 7). Played cards use section 2.7's sizes, on one ring 60 inside the felt along each
    seat's nominal angle, with yours above your hand. Phase 6 turns them towards the racks.
  - **Stacking:** hand at z 1, plaques at z 2, so your plaque covers the bottom of your hand.
    Phase 3 sets out the rest of section 3's z ladder. **Drawn on top is not on top for the mouse:**
    a `QGraphicsItem` that does not take a press passes it to the item beneath. So `SeatItem` takes
    presses and hover over its drawn `shape()`, and clicking your own name never plays the card under
    it. Phase 4's panel, and anything else a later phase lays over the hand, must do the same.
  - **The bid strip's floor:** `TableScene::overlayBottom()` is the table-space y above your hand.
    `TableView` maps it to the viewport, and the strip rises from there, as far as the viewport's
    top if it has to.
- **The B1 crispness contract is `devicepixels::effectiveRatio(painter)`** (`src/paint/DevicePixels.h`).
  Under a scaled view, a pixmap rasterized at its table-space size would be resampled on its way to the
  screen. The function returns the paint device's ratio times
  `QStyleOptionGraphicsItem::levelOfDetailFromTransform()`, so a pixmap is rasterized at the size it
  lands. It reads the ratio from the painter's *device*, not from a `widget` argument, so a scene
  rendered into a HiDPI image gets that image's ratio. The ratio is below 1 on a small window, which Qt
  draws correctly. **Every pixmap a later phase blits in the scene must call it:** phase 3's cached
  table, phase 4's `Theme::avatar()` portraits, phase 5's rack cards. Tests render a card at half
  scale, and into a ratio-2 image, and compare pixel for pixel.
- **Pixmap caches key the ratio on `devicepixels::ratioKey()`, in ten-thousandths.** The ratio is
  continuous now, and the hundredths key the caches used to share served a resized window another
  size's pixmap (127 px for 125), resampled after all. `CardRenderer` and `Theme` both use it.
- **The room is the view's background.** `TableView::drawBackground()` draws it in viewport pixels
  over the whole viewport, letterbox included, through the scene's `currentTheme()`. The scene used to
  draw it, and had to work out the viewport's logical size from the paint device by type. That is
  harder than it looks, because `QPaintDevice` has no `deviceIndependentSize()`: a widget reports
  logical pixels, while an image or a pixmap reports physical ones beside its ratio. It also took the
  whole device for the viewport, which is wrong for a render into part of an image.
- **Pitfall: `fitInView()` adds a 2-pixel margin of its own, and `Qt::AlignCenter` centres in whole
  pixels.** The scale is set by hand. QGraphicsView's centring halves the viewport with integer
  division and truncates its scroll offset, which measured almost a pixel off at 1920×1080, and it
  centres in the whole viewport where fit() centres in what the 16 / 10 / 8 margins leave.
  `TableView::fitTable()` therefore puts fit()'s offset into the transform and sets the view's scene
  rect to the viewport itself, aligned top-left, so the view has nothing to align or scroll. The
  centring test is exact to 1e-6.
- **Pitfall: a resize re-rasterizes every card on the table.** The ratio changes with the scale, so
  dragging the window edge misses the pixmap cache for every card on every frame. `CardRenderer` keeps
  each card's parsed `QSvgRenderer`, which took a 20-card frame from 135 ms to 75 ms per resize
  step. The rest is the court cards' art: J, Q and K take 8–11 ms each to render at about 124 px,
  against 0.1–0.6 ms for a pip card. A real frame still costs about 20–30 ms per step. **Phase 3 takes
  this on (its task 4):** defer re-rasterizing until a resize settles. Quantizing the scale would undo
  the `ratioKey()` fix.
- **Pitfall: listing a file in `tests/CMakeLists.txt` before the file exists.** The automatic
  re-configure fails on the missing source and leaves a test target that still links, with no test
  sources in it, so `desktop_tests` reports **0 test cases**. A fresh `cmake --preset` fixes it.
  This is the count check of `docs/LOCAL_VERIFICATION.md` again.
- **Removed as dead once their callers went:** `TrickItem::cardFootprint()` and
  `HandItem::liftFraction()`. Both existed only so the old layout could reserve room before any card
  existed.

### Phase 3: the table itself ✅ COMPLETE

Files: new `src/table/TableTopItem.h/.cpp` (wood, bead, felt, gold circle), new
`src/table/LogoItem.h/.cpp` (or one item for both), `TableScene.cpp`.

1. Paint section 2.4 in table space. Cache the composed table as a pixmap at the current device
   pixel ratio × view scale (the same logical-size-and-dpr contract `Theme::background()` already
   states), and invalidate the cache on resize and theme change.
2. Z order, bottom to top: room (background) < table < logo < racks < played cards < lead rhombus <
   hand < human's panel < other panels < pinned HUD < bid strip < modal sheets.
3. Compare with `screenshots/table-4-players.jpg` ignoring seats and cards.
4. **Settle a resize before re-rasterizing**, carried over from phase 2's review (PR #27). Every
   pixmap in the scene is rasterized at the device ratio × the view's scale, so each step of a window
   drag misses every cache. Phase 2 measured 20–30 ms per step from the court cards alone, and the
   composed table from task 1 adds to that. While a resize is in progress, draw each pixmap item (the
   cards, the composed table) from what it already holds, scaled to its rect, and re-rasterize at the
   exact ratio about 150 ms after the last resize event. Build it as one mechanism in `TableView`
   (for example a settling flag the items read, plus a single-shot timer that repaints the scene), not
   a copy per item. Do not quantize the ratio instead: that brings back the soft, misplaced cards
   `devicepixels::ratioKey()` fixed.

Done when: an empty table at 1920×1080 matches the mock-up's table, rim and logo, and dragging the
window edge with a full table on it stays smooth, with each frame of the drag under about 8 ms.

**Outcome:**

- **The count is 309 tests** (290 + 19, none retired), passing on the `default`, `release` and
  `clang` presets with Qt 6.11.2 and building without a warning. **The review of PR #28 brought it
  to 322**, still with none retired.
- **The empty table matches the mock-up.** The screenshot tool's new `table-empty.png` is a bare
  `TableView` at 1920×1034: the mock-up's viewport less its 46 px menu bar, so both tables are drawn at
  one scale in one place.
  - The rim's outer edge lands on the same pixel radius as the mock-up's in each of 8 directions.
  - Averaged over 52 angles, the luminance across the rim (felt, bead, wood, shadow) is within ±3.5
    levels of the mock-up's.
  - The wood grain correlates with the mock-up's at 0.87, within a pixel.
  - The logo's lettering, ornaments and lines are in place.
  - What still differs: the ♣ comes from a fallback font, as it does in the browser. On Linux it is
    about 20% larger and 3 units lower than in the mock-up's picture.
- **Resizing is smooth.** `tests/ResizeTiming.cpp` (`[.timing]`, hidden) drags a full six-player table
  on the release build, offscreen, at 1600×900.
  - A drag frame now costs 7.3–8.2 ms at the median over six runs, and 8–15 ms at worst. Every step
    used to cost about 80 ms, because it re-rasterized every card and the table.
  - About 5–6 ms of the frame is painting, measured with the window held at one size: the room about
    2.5, the cards about 1.2, the plaques up to 1.2, and the table under 1. The rest is the window
    itself being resized on each step.
  - The one frame when a resize settles costs about 75 ms, most of it the court cards' SVG art.
  - After the review of #28 a drag's first step is drawn sharp too, so a one-off resize is never
    soft. That step costs 65–68 ms, once per drag. The frames after it stay at 7.2–7.5 ms median,
    8.1–8.3 ms worst, over four runs.
- **What later phases take from here:**
  - **The stacking order:** `tablespace::z` in `TableSpace.h`, with gaps of ten (`table`, `racks`,
    `playedCards`, `leadMark`, `hand`, `humanPanel`, `panels`, `hud`). Phase 5's racks and phase 6's
    rhombus take their rungs from it.
  - **Shadows:** `src/paint/Shadows.h`, CSS `box-shadow` with CSS's blur numbers.
    - `drawCircleShadow()` is analytic, from the error function. `Side::Beneath` is the outer shadow
      for an opaque circle drawn over it, filled as a disc so the two meet in one anti-aliased edge.
    - `drawShapeShadow()` blurs any path with three box passes and cuts the shape out by its own
      anti-aliased coverage, laid on the device's pixels so the cut-out lines up with the shape.
      Phases 4 and 8 use it for the panels' 0 12 26 shadow, phase 6 for the gold glows.
    - `shadows::reach(blur)` is how far a shadow reaches; size anything that holds one by it.
  - **Gradients:** `gradients::cssRadial()` is CSS's farthest-corner `radial-gradient()`. It draws
    the room, the felt's vignette and the logo.
  - **The colours:** section 2.4's colours are in `Theme::table`, from `woodDark` to `logoOrnament`
    (44 tokens in all).
  - **The table:** `TableTopItem`, a cached `tabletop::render(theme, ratio)` holding the table and
    its logo in one picture.
  - **Settling a resize:**
    - A resize's first step is drawn sharp, at full cost, so a one-off resize (maximize, a dock)
      is never soft. A second resize within 150 ms is a drag: `TableView` freezes a scale the drag
      has already painted at, and sets it on its viewport with `devicepixels::setFreeze()`.
    - The freeze is a factor, the frozen scale over the live one. `devicepixels::effectiveRatio()`
      applies it to any painter on that viewport, so caches hit and Qt scales what they hold.
      A scene rendered into an image paints on the image and gets its exact ratio.
    - It starts on a change of size, not of scale, so a wide window dragged sideways (same scale)
      still spares the room.
    - `TableView` ends the freeze 150 ms after the last resize event, or at once through
      `finishResize()`.
    - **Phase 4's portraits and phase 5's rack cards have nothing to pass:** asking
      `effectiveRatio()`, as every pixmap does, is enough.
  - **Read-only counters for tests:** `CardRenderer::rasterizations()`, `TableTopItem::renders()`,
    `TableScene::cardRenderer()` / `tableTop()`, `TableView::isSettling()`.
- **Where the build departed from this phase's plan, and why:**
  - **The room is filled straight from its gradient while a resize settles**, through
    `Theme::roomBrush()`, which `paintRoom()` also uses. The plan had the last room stretched.
    Measured in the real frame, a stretched room cost 8.9 ms a frame, painting a new room each step
    10.6, and the gradient fill 7.4. A micro-benchmark of the room on its own had said the opposite.
  - **The letter spacing is `QFont::AbsoluteSpacing` 0.37.** CSS's 0.01em is a fixed amount after
    every letter, which `PercentageSpacing` is not.
  - **The logo is part of the table's picture**, not a `LogoItem`, because nothing in the ladder comes
    between them.
- **Pitfall: Qt's raster engine draws clip paths aliased.** A textured disc clipped to a circle has
  a jagged rim. Everything on the table is an anti-aliased fill instead: the textures are brushes, and
  the bead is stacked discs under the felt.
- **Pitfall: a texture brush half a pixel off the grid blurs every texel.** The disc's corner falls
  at 80.6 px at a ratio of 0.767, so each texture's corner is snapped to a whole pixel with
  `devicepixels::snapped()`. Before the snap, the grain's edge energy was 16.6 against the mock-up's
  21.3; after it, 20.8. A shape's shadow is snapped the same way.
- **Pitfall: CSS's paint order.** Outer shadows go under the background. Inset shadows go over it,
  with the last one listed lowest. The border goes over the inset shadows. And `transparent` in a CSS
  gradient blends premultiplied, which Qt's default `ColorInterpolation` also does, so one
  transparent stop ports as it is (`GradientsTests.cpp`). Don't switch to `ComponentInterpolation`:
  that one passes through grey.
- **Pitfall: a Catch2 test name with a comma cannot be run by its name.** The comma separates
  filters, so `"While a resize settles, no card…"` matches nothing: Catch2 prints "No tests ran"
  and exits 2. Leave commas out of new test names; for an old one, use a wildcard,
  `"While a resize settles*"`.

### Phase 4: seats and name panels ✅ COMPLETE

Files: `src/table/SeatLayout.*`, `src/table/SeatItem.*`, `src/paint/Theme.cpp` (avatar painter),
tests `SeatLayoutTests.cpp`, `SeatItemTests.cpp`.

1. `seatlayout`: add a function returning the panel rectangle for (seatCount, humanSeat, seat) from
   section 2.5's table (2 to 6 players, plus the human's and the spectated seat 0's spot). Keep the
   clockwise-from-human mapping that `seatCentres()` encodes; the existing tests for order still
   apply.
2. `SeatItem`: new size (280×118, human 300×118), panel style, portrait box 88×100, two lines (name
   with points in brackets; the Taken/Bid pill), active ring and "▸". Move the face-up Forehead card
   out of the plaque (it goes to the rack in phase 5); until phase 5 lands, keep it working where it is.
3. `Theme::paintAvatar()`: the head-and-shoulders bust per seat (shapes and colours in `table.js`
   `PLAYERS` and `portrait()`); keep the existing cache and box contract.
4. Tests: the layout table for every n, with "no panel overlaps the table circle (r 522) or another
   panel" asserted geometrically; pill text and colour state for no bid / bidding / on target /
   owing / over; the long-name elision test still holds at the new width.

Done when: 3, 4, 5 and 6 players match `screenshots/table-N-players.jpg` for panel placement and look.

**Outcome:**

- **The count is 340 tests** (322 + 18), passing on the `default`, `release` and `clang` presets
  with Qt 6.11.2 and building without a warning.
  - Nine `SeatItemTests` cases were retired or rewritten. They asserted the old plaque's layout:
    a text column narrowed by the Forehead card, and the cards-left stack.
  - The avatar's accent case now recolours a portrait colour instead, since the portraits no
    longer read `accent`.
- **Two decisions by the owner, 2026-10-18:**
  - **No " (you)".** Your panel reads "Player (23)", as the mock-up's reads "You (17)". The
    bottom-centre place and the wider panel already mark it as yours.
  - **The cards-left stack is gone.** Phase 5's racks show the count.
- **What later phases take from here:**
  - **Where the panels go:** `seatlayout::panelRect(seatCount, humanSeat, seat)`, section 2.5's
    table in `SeatLayout.cpp`. Panel sizes are in `TableSpace.h` (`panelSize`, `bottomPanel`).
    `seatlayout::stepsFromBottom()` is the clockwise order, shared with `seatCentres()`.
    Phase 5's rack angle starts from each panel's centre.
  - **The panel style:** `src/paint/Panel.h`.
    - `panel::paint()` draws section 2.2's panel and the active ring and glow, in CSS's paint
      order. `panel::reach()` is how far it paints past its rect.
    - `panel::render()` caches it as a picture whose panel edges fall on whole pixels.
    - Phases 8 and 10 should build their panels on it.
  - **Rounded shapes:** `paths::rounded()` and `paths::roundedBand()` moved to `Paths.h` from
    `TableTopItem.cpp`. The logo draws exactly as before; `table-empty.png` is pixel-identical.
  - **Portraits:** `Theme::avatar()` draws table.js `portrait()` from six
    `TablePalette::PortraitLook`s, with page.html's sepia and contrast filter applied as a pixel
    pass. `TableScene` gives each seat the look of its place clockwise from the bottom, so the
    bottom seat wears the mock-up's first face.
  - **The pill:** `seatitem::pillFor()`. It is neutral until every seat has bid
    (`betsPlaced < seats.size()`), as the mock-up is. It reads "Bidding…" for the seat being
    asked, and is coloured good, warn or bad once play starts.
  - **The Forehead card** stands in the portrait's place until phase 5 moves it to the rack. The
    text column no longer changes for it.
- **Each panel is one cached picture.** `SeatItem::picture()` composes the chrome, the portrait and
  the text, and composes them again only when what the panel shows or the ratio changes.
  - Painted directly, the text, the portrait and the chrome each cost about half a millisecond a
    frame over six panels. That took a drag frame from 7.4 to 8.9 ms at the median.
  - Composed, a drag frame is back at phase 3's numbers: 7.4–7.6 ms median and 8.3–8.9 ms worst,
    over three runs.
  - The first step of a drag and the frame it settles now cost about 76 and 83 ms (phase 3: 65–68
    and 75), because six panels are composed again there.
  - `SeatItem::compositions()` counts compositions, for tests. Neither a settling drag nor a
    snapshot that changes nothing on a panel composes it again.
- **Section 4's "r 522" was too strict.** The five-player bottom panels' square corners are 521.5
  from the centre; only their rounded corners clear 522. `SeatLayoutTests` asserts instead what
  the mock-up's numbers give: every panel at least 10 outside the wooden rim (520).
- **PR #29's review fixes** take the count to 344 tests.
  - **The minimum scale is 0.36**, up from phase 2's 0.30: the owner's decision, 2026-10-18. The
    panels' Marcellus 25 drew names at 7.5 px at 0.30, and draws them at 9 px at 0.36.
    `minimumViewport()` is 709×495, and the window stops at 945×659. A `TableSpaceTests` case holds
    `seatitem::nameSize` and `minimumScale` together.
  - **The chrome is shared:** `panel::Cache`, owned by `TableScene` beside the card renderer and
    holding one ratio and palette at a time. Measured back to back, the first drag step fell from
    about 94 to 79 ms and the settle frame from about 106 to 88. Phases 8 and 10 should draw their
    panels through it too.
  - **`seatitem::Shown`** is what a panel's picture shows. The item paints from it and recomposes
    only when it changes, so anything later added to the panel must go through it.
  - **The portrait's frame** goes through the same sepia and contrast filter as its picture, as CSS
    filters the whole element. The portrait's rounded rect is the snapped picture's own, which
    removed a hairline of the picture's far edge along its top and bottom.
  - **`fonts::baselineIn()`** is CSS's half-leading baseline, shared by the logo and the panels.
- **Pitfall: the logo's lettering is not quite deterministic.** In about one run in four, a band of
  the logo (y 511–523 in `table-empty.png`) differs from the others by up to 4 levels. The base
  branch does the same, so it predates this phase. Compare screenshots with a tolerance there, or
  run twice.

### Phase 5: racks ✅ COMPLETE

Files: new `src/table/RackItem.h/.cpp`, `src/paint/CardRenderer.cpp` (`back()`), `TableScene.cpp`,
new `tests/RackItemTests.cpp`.

1. Implement section 2.6: rack angle (with the slide-off-the-panel rule), frame, projected card quads,
   rail drawn on top, shadow.
2. Expose the final rack angle per seat (the scene needs it for phase 6).
3. Forehead: the single card face-up in the rack when `SeatView::hand` is non-empty.
4. New card back art in `CardRenderer::back()`.
5. Tests: one card per `cardsLeft`; no cards and no rail at zero; the rack origin is outside its own
   seat's panel; the projected quad's far edge is wider than its near edge; the face shows only when
   the seat's hand is visible.

Done when: `screenshots/detail-racks-and-played-cards.jpg` is matched for the racks.

**Outcome:**

- **The count is 362 tests** (344 + 18), passing on the `default`, `release` and `clang` presets
  with Qt 6.11.2 and building without a warning. The card back added one case (345). The racks
  retired six `SeatItemTests` cases that placed the Forehead card on the panel. They moved "Your
  own seat shows no face in a Forehead round" to `TableSceneTests`, and added 23 cases: 19
  `RackItemTests`, one `SeatItemTests` case, two scene cases and one resize case.
  **The review of PR #30 brought it to 364**: two more `RackItemTests`, for the exact quarter
  turns and the pixel snap.
- **Section 2.6 was corrected in five places**, now written into it above:
  - the projection order;
  - the rail's colour, which is flat because the texture is transparent there;
  - the rail's `translateZ(8px)`;
  - the cards' 45% gradient stop and their inset line;
  - the slide rule, which never fires with section 2.5's panels.
- **The card back** is `backSvg()`'s, painted by `CardRenderer::paintBack()` in the mock-up's
  100×145 units. The lattice is a texture brush, not a clip. The Hidden-round hand and the Card art
  window show it too. It adds one thing `backSvg()` doesn't have: a faint dark hairline round the
  edge, like the one the face art draws, so the cream border doesn't fade into a light background.
- **A rack is drawn on a whole device pixel**: `RackItem::paint()` snaps the picture's corner, and
  `rackitem::frameRotation()` gives quarter turns exactly. So four players' racks are pixel-exact.
- **What later phases take from here:**
  - **The rack angle:** `seatlayout::place(seatCount, humanSeat, seat)->angle`, in radians with
    y down. It's a `std::optional`, empty for your own seat, which has no rack. It's 90° for the
    bottom seat, which is what a spectated seat 0 uses. `seatlayout::places()` gives every seat's
    at once, and `seatlayout::rackAngle(panel)` gives a panel's.
    `seatlayout::rackOrigin(angle)` is the point on the rim at `tablespace::rackRadius` (452).
    **Phase 6 turns the played cards and places the rhombus by this angle**:
    `TableScene::trickPositions()` still uses the nominal seat angles.
  - **The rack's geometry:** `rackitem::cardQuad()`, `cardTransform()`, `railRect()`,
    `extent()` and `frameRotation()`, all in the rack's own frame (+y towards the owner).
  - **What a rack shows:** `rackitem::shownFor()`. A non-empty `SeatView::hand` shows those cards
    face up; otherwise it shows `cardsLeft` backs. The Forehead rule stays exactly as section 17
    has it, and the panel draws no card at all now.
  - **Its colours:** `TablePalette`'s `rackShadow`, `rackCard*` and `rail*` tokens.
- **Each rack is one picture, shared.** `rackitem::Cache`, owned by `TableScene` like
  `panel::Cache`, holds one picture per thing shown, at one ratio and palette at a time.
  - The pictures are drawn in the rack's frame and turned by the item, so five racks of five
    cards are one picture.
  - Composed per rack, the first step of a drag and its settle frame each cost about 8 ms more.
- **Resizing**, release build, `[.timing]`, measured back to back on one machine against phase 4:

  | | Phase 4 | Phase 5 |
  |---|---|---|
  | Drag frame, median | 6.7–7.1 ms | 7.3–7.6 ms |
  | Drag frame, worst | 7.2–7.9 ms | 8.3–8.6 ms |
  | First step | 67–69 ms | 70–73 ms |
  | Settle frame | 73–76 ms | 76–79 ms |

  The racks cost about 0.5 ms a drag frame, which is the blending of their pictures turned to
  their angles.
- **Pitfall: a picture turned in advance is not cheaper to draw.** Keeping each rack's picture
  pre-rotated, so the blit had no rotation, was tried and measured slower. A diagonal rack's turned
  picture has larger transparent corners, and blending that area is the cost, not the filtering.
  What helped was trimming the picture's margins to where the shadow actually falls: it's offset
  5 towards the owner, so the table side needs only 3.5.
- **Pitfall: the outer cards' near corners poke past the rail.** They're turned up to 7°, so in an
  8-card rack they stand about 1.7 past its edge, as in the mock-up. A test that the rail covers a
  card's near edge checks the edge's midpoint.

### Phase 6: played cards and the lead rhombus ✅ COMPLETE

Files: `src/table/TrickItem.*`, `TableScene.cpp`, `tests/TrickItemTests.cpp`.

1. Positions, sizes and rotations from section 2.7; the human's card above the hand.
2. Winning highlight from `PlayedCardView::isWinning` only.
3. Replace the pip with the rhombus; work out the leading seat as section 2.7 says.
4. Keep "a trick entry the ring cannot place hides its card" and "an unplaceable lead draws no pip"
   true under their new meaning.

Done when: the played cards and rhombus match the mock-up for 4 and 6 players.

**Outcome:**

- **The count is 384 tests**: 377 at the phase's first commit, and 7 more from its review
  round. 364 − 6 + 19 + 7:
  - **Six `SeatLayoutTests` cases retired** with `seatlayout::seatCentres()` and `ringRect()`,
    which lost their last caller here (the owner's decision, 2026-10-19): "Seat centres are
    fractions of the rect…", "No two seats share a centre" (now covered by "No two panels
    overlap"), "Degenerate requests…" (by "A table the panels have no row for", and its
    out-of-range human by the spectator case), and the three "ring rect" cases.
  - **Five rewritten against `panelRect()`**, keeping their intent: section 14's rows, the human
    at bottom-centre, clockwise order, the spectator layout, and the old "ring's order" case,
    which became "The panels keep section 14's rows whoever is at the bottom".
  - **19 added:** 16 in `TrickItemTests` (its two defensive cases are kept under their new
    meaning), a scene case for the rhombus, a drag-settle case for the shadows, and a
    `GameBridgeTests` case that runs a real four-seat game.
  - **7 more from the review round:**
    - the rhombus marks nobody once the round's play is over;
    - a played card's shadow lies a whole number of pixels from the card's corner;
    - the winner's ring has no inner edge for the felt to show through;
    - `devicepixels::canvas()`'s anchoring;
    - `seatlayout::place()` and `humanSeated()`;
    - a settling drag doesn't draw the rhombus again.

    "The rhombus is long along the radius" became "…drawn long along the radius": it now
    measures what `LeadMarkItem` paints.
- **Section 2.7 was corrected in three places**, now written into it above:
  - every played card casts a shadow, not only the winner;
  - the winner is highlighted only from the second card (the owner's decision, 2026-10-19:
    as the mock-up);
  - the rhombus shows only in the engine's `Playing` phase. Phase 6 first guessed that phase
    from `betsPlaced` and `activeSeat`. That marked the last bidder during their final bid's
    frame, hid the mark when play began, and left it under the round sheet. The review round
    put `GameSnapshot::phase` on the snapshot instead.
- **What later phases take from here:**
  - **Where a seat is:** `seatlayout::place()` gives a seat's panel, whether it is the
    human's, and its rack's angle. `TableScene::relayout()` works out every seat's once
    (`seatlayout::places()`), and the racks, the trick and the rhombus all read it.
    `seatlayout::humanSeated()` is the one "is somebody at the table" rule.
  - **Where a played card goes:** `trickitem::cardPlacement()`, and the rhombus
    `trickitem::leadMarkPlacement()`, both from a `Place`. The turn is
    `rackitem::frameRotation()`, so four players' cards are exact quarter turns.
    `trickitem::leadSeat()` is which seat is marked.
  - **The engine's phase is on the snapshot:** `GameSnapshot::phase`. Phase 8's trick panel
    can read it rather than guessing the phase.
  - **The card shadow:** `src/paint/CardShadow.h`. `cardshadow::render()` draws `.tcard`'s
    box-shadows (plain, or winning: shadow, glow and ring) as one picture, laid a whole number
    of pixels from the card's own corner. The winner's ring is filled through under the card.
    It takes `cardshadow::Colours`, its four colours, not the whole palette.
    **Phase 7 can give the hand its shadow through the same cache.**
  - **One picture cache:** `src/paint/PictureCache.h`. The panels, racks and card shadows all
    keep their pictures in it, and `devicepixels::canvas()` is the one place a picture's corner
    is put on a whole pixel. The per-item memo in front of each cache is still copied four
    times; see `PHASE5_REVIEW_FINDINGS.md` #12.
  - **`CardItem::setPlayed()`** replaces `setWinning()`. Only played cards cast a shadow, so the
    hand is untouched. `CardItem::cardSize()` is public: a played card's `boundingRect()`
    reaches past the card.
  - **The rhombus is `LeadMarkItem`**, at `tablespace::z::leadMark`. Its picture is drawn
    before the 1.45 stretch, at 1.45 times the ratio, as CSS blurs the box before transforming
    it.
  - **New table-space constants:** `tablespace::playedInset` (60) and `leadMarkHumanY` (820).
- **Resizing**, release build, `[.timing]`, one run per column, against phase 5's recorded runs
  (not measured back to back):

  | | Phase 5 | Phase 6, first run | Phase 6, after review |
  |---|---|---|---|
  | Drag frame, median | 7.3–7.6 ms | 8.0 ms | 8.0 ms |
  | Drag frame, worst | 8.3–8.6 ms | 8.7 ms | 9.1 ms |
  | First step | 70–73 ms | 70 ms | 75 ms |
  | Settle frame | 76–79 ms | 81 ms | 83 ms |

  - **The shadows cost about half a millisecond a drag frame:** five pictures blended at their
    turns, as the racks cost.
  - **The settle frame draws them again** at the new ratio.
  - **The first run's timed table had no winner and no rhombus.** `fullTable()` gained both in
    the review round. The refreshed run, at `6c4ae69`, times the winner's three-layer picture
    and the rhombus too.
    - The drag frame's median doesn't move.
    - The first step and the settle frame, where those pictures are drawn afresh, cost about
      2–5 ms more. That is within one run's noise of each other, so read it as "a little", not as
      a measurement.
- **Pitfall: `src/paint/` does not include `src/table/`.** The card shadow's corner radius (7)
  therefore lives in `CardShadow.cpp`, not in `TableSpace.h`.

### Phase 7: the human's hand ✅ COMPLETE

Files: `src/table/HandItem.*`, `src/core/HandLayout.*` if the geometry lives there,
`tests/HandItemTests.cpp`, `tests/HandLayoutTests.cpp`.

1. Section 2.8's geometry and hover lift. Everything about sources, legality and slots stays.
2. Tests: centring, overlap, and "the slot under an overlap is the upper card" still pass with the
   new numbers (rewrite expected values where they encode the old ones).

Done when: the hand matches the mock-up with 1, 4 and 8 cards, and clicking still plays the right card.

**Outcome:**

- **The count is 418 tests**: 411 at the phase's first commits, and 7 more from its review round
  (401 + 17, none retired). All pass on the `default`, `release` and `clang` presets with Qt 6.11.2,
  building without a warning. The phase started from 401: phase 6's 384 plus the phase 5 review
  fixes (#32).
  - One `CardRendererTests` case for the dimming, four `HandItemTests` cases for the hand's shadow
    looks, and five for the fan.
  - **The review round's seven:** six in `HandItemTests` (a card's own rect, written out by hand; a
    card wider than the fan stacks the hand; the next card wins over a raised card's risen face; and
    three for a still cursor) and one in `TableSceneTests` (a still cursor, through the scene's own
    hover).
  - **Rewritten, keeping their intent:**
    - "The fan draws one card per slot and centres them" is now "…where cardPlacement() puts it",
      plus a mirror case for 1 to 8 cards.
    - "The cards overlap rather than sitting end to end" is now "The fan steps 84 apart until eight
      cards close it to 720".
    - "The slot under an overlap is the upper card" takes its point in the card's own frame.
    - "A dimmed card is not the same image as a lit one" lost its alpha assertion, which encoded the
      45% opacity.
    - "A seated table holds the hand where section 2.8 puts it" checks every card.
    - The two plaque cases read `CardItem::isRaised()`.
    - The shadow drag case is now "While a drag settles no card's shadow is drawn again".
  - The `HandItemTests` cases use the table's card height. At their old 100, section 2.8's fixed 84
    step leaves gaps between the cards.
  - **Checked by mutation**, each case against the defect it guards:
    - testing the raised card only where it has risen fails the flicker case;
    - dropping the risen part from the hit test fails the risen-hit case;
    - testing the risen part before the other cards fails only the next-card case;
    - dropping the lift from `setPlayable()` fails the two your-turn cases, and dropping it from
      `layOut()` fails the rebuild case.
- **Three decisions by the owner, 2026-10-20:**
  - **Illegal cards darken, opaque**, as the mock-up does (brightness 0.55, then saturate 0.55). The
    45% opacity showed the card beneath through a hand that overlaps by half. Section 2.8 and section
    18 of `QT_CLIENT_PLAN_proposal1.md` say so.
  - **Hand screenshots with the mock-up beside them, committed:** see below.
  - **One `[.timing]` run** at the end of the phase.
- **Section 2.8 was corrected in six places**, now written into it above:
  - every hand card casts a shadow;
  - the hover's ring, glow and shadow;
  - the hover's hit area;
  - the dimming;
  - and from the review round, a card under a still cursor rising, and the strip's mismatch.
- **PR #33's review** (eight commits after the phase's six):
  - **Fixed: a card under a still cursor stayed down when your turn came.** The fan worked out which
    card to raise only on a mouse move, and a prompt arriving, a rebuild or a theme change is not
    one. `HandItem` now keeps the cursor's last position over the fan (forgotten when it leaves), and
    `layOut()` and `setPlayable()` raise the card under it through `liftAt()`, the rule a hover uses.
  - **Fixed: `capture_hands.py` saved a failed page.** A failed check drew a red banner, Chrome
    photographed it, and the script saved that over the committed picture and exited 0. The page now
    marks `<body>` only once every check has passed, and the script reads that mark
    (`--dump-dom`) before each shot, stopping with the banner's reason and saving nothing.
  - **Fixed: a card wider than the fan reversed the hand.** The step went negative above 720, laying
    each card left of the one before it while still drawing it on top. It is clamped at zero, so the
    hand stacks. Defensive only: the table's cards are 155 wide.
  - **`cardshadow::cardRect()`** is the one definition of where a card is in its own frame:
    `CardItem` draws there (`CardItem::cardRect()`), `HandItem` hits there, and every shadow is cast
    round it. It also removed locals in `CardItem::paint()` that shadowed members.
  - **`HandItem` has `ItemHasNoContents`**, as `TrickItem` has: it paints nothing, and its rect
    covers the whole fan. Presses and hovers still reach it.
  - **Documented, not changed:** the strip's mismatch above, in `HandItem::slotAt()`'s comment.
  - **Not changed, at the owner's call:** `showLift()` updates every card on a hover change, where
    only two can change. It costs microseconds.
  - The screenshots before and after the review's fixes are pixel-identical, 9 of 9.
  - The round also added `CLAUDE.md`'s "Explaining something visual" section: a working rule about
    offering screen recordings, not phase 7's work.
- **The hand matches the mock-up.**
  - `capture_hands.py` photographs the mock-up at the moment the client's hand shots draw.
  - Over the 8-card hand the two differ by about 5 levels on average, and only at edges.
  - A dimmed card's white is 140 in both, so Chrome applies `brightness()` and `saturate()` to sRGB
    values, as `CardRenderer` does.
  - `table-empty.png` is unchanged but for the logo's known band.
- **What later phases take from here:**
  - **Where a card in your hand goes:** `handitem::cardPlacement(count, slot, card)`, a
    `tablespace::Placement`.
    - `HandItem` sits at the scene's origin with its cards in table space, as `TrickItem` does.
    - **`tablespace::Placement`** is the one `{centre, rotation}` for the trick, the rhombus and the
      hand; it was `trickitem::Placement`.
  - **A card's look:** `cardshadow::Look` (`Played`, `Winning`, `Held`, `Raised`), with `lift()` and
    `reach()`.
    - `CardItem::setHeld()` sits beside `setPlayed()`, and `look()` / `isRaised()` are for tests.
    - The hand shares the played cards' cache, `TableScene::cardShadows()` (was `playedShadows()`).
  - **The rise is drawn, not a move:** a raised card's `pos` and rotation stay its resting place.
    Phase 12's card-to-table animation can start from `pos` as it is.
  - **The hit test** asks each card in its own frame: at rest, plus the raised card where it rose to.
    Anything that changes the hand's shape must keep both. "On the card" is
    `cardshadow::cardRect()`, everywhere.
  - **The fan remembers the cursor.** Anything that changes the hand without a mouse move must end
    in `layOut()` or `setPlayable()`, so the card under a still cursor is raised again.
  - **For phase 12:**
    - the mock-up's 0.15 s ease on the hover;
    - its pointer and not-allowed cursors;
    - a keyboard focus ring.
    None are ported: the first is motion, and the client has no focus model for cards.
  - **The hand screenshots:** `hand-1-card.png`, `hand-4-cards.png` and `hand-8-cards.png` from the
    screenshot tool, paired with `screenshots/hand-*.jpg` as `CLAUDE.md` says.
    `mockup/source/capture_hands.py` takes those again if the mock-up changes. Its shots and
    `ScreenshotTool.cpp`'s `handShots()` must stay card for card the same. It saves nothing for a
    page that did not finish setting up, so a table.js change that breaks it stops it with a reason.
- **Resizing**, release build, `[.timing]`, one run, against phase 6's recorded runs (not measured
  back to back):

  | | Phase 6, after review | Phase 7 |
  |---|---|---|
  | Drag frame, median | 8.0 ms | 8.8 ms |
  | Drag frame, worst | 9.1 ms | 9.9 ms |
  | First step | 75 ms | 78 ms |
  | Settle frame | 83 ms | 88 ms |

  - **The hand costs about 0.8 ms a drag frame.** That is eight cards drawn turned, where they used
    to be square to the screen, plus eight shadow pictures blended at their turns. The played cards'
    five cost about half a millisecond in phase 6.
  - It is under the 1 ms the phase allowed before acting. The fix in reserve is one cached picture
    of the resting fan, recomposed on a hover, and it was not built.
  - The first step and the settle frame draw the hand's two pictures afresh: a few ms, within one
    run's noise.

- **Pitfall: the built mock-up leans on its host.** `romanian_whist_table.html` starts at its
  `<title>`, with no doctype and no charset, and sets neither `body { margin: 0 }` nor
  `[hidden] { display: none }`.
  - Opened bare in Chrome, it renders in quirks mode and reads its UTF-8 as Latin-1, so the suit
    glyphs come out as three letters.
  - Its hidden bid strip and modal scrim, which set their own `display`, show over the table.
  - `capture_hands.py` supplies all four.
- **Pitfall: Chrome loads no font from a `file://` URL into a `file://` page** (CORS), so the
  capture embeds the TTFs as data URIs.
- **Pitfall: CSS turns about `transform-origin`; QGraphicsItem turns about the item's origin.**
  `CardItem`'s origin is its middle, so `cardPlacement()` puts the middle where turning about the
  hinge would have carried it. `setTransformOriginPoint()` would also work, but then `pos` would no
  longer be the card's middle, which the hit test and phase 12's animation both read.

### Phase 8: scoreboard, trick panel, bottom buttons

Files: `src/ui/ScoreboardWidget.*`, `src/ui/InfoStrip.*`, `src/ui/PromptBar.*`, new
`src/ui/TrickPanel.h/.cpp`, new `src/ui/TableButtons.h/.cpp` (or similar), `src/MainWindow.cpp`,
`src/table/TableView.*`, tests for each.

1. Scoreboard per section 2.9: header lines (reuse `infostrip::fieldsFor()` for round, cards, round
   type and trump), tension line, four-column table in bid order, stacked trump.
2. Trick panel per section 2.10, built on `promptbar::fieldsFor()`.
3. Two bottom-left buttons per section 2.11 (wired to phase 11; disabled or showing an empty sheet
   until then).
4. Host all of them as described in section 3 and pin them on every resize (8 px in from the
   window's corners, 8 px under the menu bar, scale 1.2 / 1.2 / 1.0).
5. `MainWindow::buildTable()`: remove `InfoStrip`, the right-hand `ScoreboardWidget` column and the
   `PromptBar` from the layout; the table view takes the whole page. The prompt bar's "Menu" button
   goes; Esc still opens the pause menu (section 12's amendment).
6. Tests: the scoreboard's rows, header strings and tension text for bidding / over / under / no trump;
   the trick panel's strings for each prompt case; the pinned positions after a resize sit inside
   the viewport and do not overlap any seat panel at 1920×1080, 1920×1200 and 1440×900.

Done when: the whole page matches `screenshots/table-6-players.jpg` except the menu bar.

### Phase 9: menu bar and commands

Files: `src/MainWindow.*`, `src/MainWindow.ui`, possibly new `src/ui/RulesSheet.*` and
`src/ui/SettingsSheet.*`, tests `MainWindowTests.cpp`.

1. Style the existing `QMenuBar` per section 2.12 with a stylesheet scoped to it, and add top-level
   actions (no drop-down) **New Game**, **Settings**, **Rules**, and **Exit** right-aligned (a
   corner widget, or a spacer action).
2. Wire them: New Game → the existing `showNewGameDialog()` path (it already handles a running
   game). Exit → the existing abandon-to-main-menu path, through the pause overlay so it can be
   cancelled. Rules → a sheet with the rules summary (the mock-up's `showRules()` text, which follows
   the engine's `docs/RULES.md`; make it depend on the current player count). Settings → see Q4.
3. Keep the View menu (Log, Card art) reachable for development; it can stay as a normal menu.
4. Tests: each action exists, is enabled in the right states, and triggers the same path as before.

Done when: the menu bar matches the mock-up and every item works.

### Phase 10: bid strip and sheets

Files: `src/ui/BidStrip.*`, `src/ui/RoundSheet.*`, `src/ui/GameOverPanel.*`,
`src/ui/PauseOverlay.*`, `src/ui/MainMenuPage.*`, `src/table/TableScene.*` (`overlayBottom()`), tests.

1. Bid strip look and position per section 2.13 (table (428, 486), width 680, drawn over the felt).
2. Restyle the sheets per section 2.13. Their content and buttons stay; only the look changes.
3. Main-menu page: room background, the logo plaque, buttons in the panel style. Optional but cheap.
4. Tests: the existing BidStrip, RoundSheet and GameOver tests stay green; update any that assert old
   geometry.

Done when: `screenshots/bidding.jpg` is matched and the end-of-round sheet reads in the new style.

### Phase 11: round history and last trick

Files: `src/MainWindow.*`, new `src/ui/HistorySheet.*` and `src/ui/LastTrickSheet.*`, tests.

1. **Round history.** `RoundResult` arrives on the GUI thread through `onRoundScored()`;
   `RoundResult.h` notes nothing accumulates them yet. Accumulate them in `MainWindow` (GUI thread
   only), clear them on a new game, and show a sheet: one row per round (round number, cards), one
   column per player with the round score (signed, coloured) and "bid/taken" small and muted, then a
   totals row. Mock-up: `showHistory()`.
2. **Last trick.** Keep the last complete table (a snapshot whose `table` has one card per seat) in
   `MainWindow`; reset it at the start of each round. The sheet shows the cards in play order with
   the owner's name under each and the winner highlighted. Mock-up: `showLastTrick()`; "No trick has
   finished yet this round." when empty.
3. Tests: history accumulates and resets; the last-trick store keeps only complete tricks and resets
   per round.

Done when: both buttons open working sheets.

### Phase 12: polish, translations, documentation

1. Optional motion, within section 18.2's animation budget: card from hand to table, trick sweeping
   to the winner, the "Bids 2" bubble. Respect any existing reduced-motion or pace settings.
2. HiDPI: check 1.0, 1.5 and 2.0 device pixel ratios; textures and cached layers must be crisp.
3. Window sizes: 1920×1080, 2560×1440, 1920×1200, 1440×900. Nothing overlaps; the table stays round.
4. Translations: build the `update_translations` target that `qt_add_translations()` in
   `CMakeLists.txt` creates, so every new `tr()` string reaches `i18n/romanian_whist_desktop_ro_RO.ts`.
5. Documentation: amend `docs/QT_CLIENT_PLAN_proposal1.md` sections 12, 14, 17 and 18 where this
   redesign changes what they describe (the file already uses "AMENDED" boxes for this), and note
   here which open questions were decided and how.

Done when: side-by-side screenshots of the client and `screenshots/` match closely at 1920×1080 for
3 to 6 players and for bidding.

---

## 5. What not to copy from the mock-up

- Its **game logic and bots** (`table.js` "rules" and "simple bots" sections). The client has the
  real engine. They exist only so the mock-up could be played.
- The **Players 3–6 switch**, the mid-game demo opening, the pace setting in its Settings sheet,
  and the "Design mock-up" label.
- The mock-up's **toasts** ("You must follow ♥") when an illegal card is clicked: the client already
  dims illegal cards and refuses the click; a toast is optional.
- Its HTML-specific workarounds (the 0×0 `.fan` container, CSS `rotateX`): port the geometry, not the
  mechanism.

---

## 6. Risks

- **Projective card racks** are the only unusual drawing. If `quadToQuad` misbehaves at some angle,
  fall back to an affine approximation (scale the far edge with a shear) before reaching for OpenGL.
- **Proxy widgets** under a scaled view can have focus and HiDPI quirks. Section 3 names the fallback.
- **Fixed layout tables**: the per-n panel positions are hand-tuned. The geometric "no overlap"
  tests in phase 4 are what keep later edits honest.
- **Test churn**: phases 2, 4, 6, 7 and 8 legitimately rewrite tests that encode the old adaptive
  layout. Keep the intent of each test where it still applies, and say in the commit which tests
  were retired and why.

---

## 7. Open questions (with defaults)

Ask the owner if possible. If not, use the default and record it.

| # | Question | Default |
|---|---|---|
| Q1 | Light/dark: the mock-up is dark only. Should the table follow the system scheme? | No: the table page is always the dark design; menus and dialogs may keep following the system. **Decided by the owner, 2026-10-17: as the default** (phase 1) |
| Q2 | Fonts: bundle Cinzel Decorative, Marcellus and Alegreya Sans (OFL)? | Yes. **Decided by the owner, 2026-10-17: yes** (phase 1) |
| Q3 | Scoreboard: the old Round-score and Streak columns are gone in the mock-up. Bring them back? | No; round scores live in the round sheet and history. Streak bonuses show there too |
| Q4 | Settings: what should it hold? The client has no settings beyond `NewGameDialog` (pace etc.) | A sheet with the bot pace control, applied to the next game; anything else later |
| Q5 | Bot names: the mock-up dropped the small "AI" tag. Keep it off? | Off |
| Q6 | Portraits: drawn busts as in the mock-up, or real art later? | Drawn busts now through `Theme::avatar()`, which is the seam for real art later |
| Q7 | 2 players: not in the mock-up | Opponent at the top position, as in section 2.5 |
