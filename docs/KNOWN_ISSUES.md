# Known issues

Defects that are known, reproducible, and deferred **on purpose**. A thing belongs here when it has
been found, understood, and left alone — not when it is merely unfinished. Work that has not been
started yet lives in `QT_CLIENT_BUILD_ORDER.md` as an unchecked box, and a defect nobody has
diagnosed is a bug rather than an entry.

Each entry says four things, in this order: **what you would see**, **why it happens**, **why it was
not fixed when it was found**, and **where it is scheduled**. The third is the one worth writing
carefully — an entry that only records the symptom invites the next reader to conclude it was
missed.

Delete an entry when the fix lands. This file is not a history; `git log` is.

---

## Your lead rhombus hides under a raised card

*Found in PR #39's review (phase 13). Not scheduled.*

**What you see.** You are to lead, and the gold rhombus between your played card's place and your
hand says so. Hover the card over the middle of your hand and it rises over the rhombus: with up to
four players it hides it completely, with five or six about three quarters of it. The rhombus is
gone just while you choose the card to lead.

**Why.** Phase 13 put every played card on one circle round the logo, which moved yours down: its
bottom edge went from 790 to 824.7 with up to four players, and to 800.7 with five or six. The
rhombus stays half way between that edge and your hand's top at 850
(`trickitem::leadMarkPlacement()`), so it is centred at 837.4 or 825.4, and it is 22.6 tall. A
hovered card rises 30 (`cardshadow::raisedLift`), to a top of 820, and your hand is drawn over the
rhombus (`tablespace::z::hand`, 40, over `z::leadMark`, 30). Before phase 13 the rhombus sat at 820
and a raised card covered half of it.

**Why it was left.** With up to four players there is no gap to move it into: a raised card's top
(820) is already above your played card's bottom (824.7). So the fix is a layout choice, not a
number: the rhombus beside your played card, or drawn over your hand, or your card a little off the
logo's circle. Each changes how the table looks, and that choice is the owner's.

**Where it goes.** Not scheduled. It needs the owner's choice of where the rhombus goes.

## The Last trick sheet is cut off on the right in a short window

*Found in PR #39's review (phase 13). Not scheduled.*

**What you see.** Open the Last trick sheet in a window too short for it, so that its body scrolls,
and its right edge is clipped: the last card of the row, or the end of the title, or the end of a
line like "Bartholomew Fairbank won it · round 12". A window narrower than the sheet does the same.

**Why.** Phase 13 made the sheet exactly as wide as its widest line or row plus padding
(`LastTrickSheet::preferredWidth()`), and stopped the title and the line under it wrapping so that
measure holds. That width leaves no room for the vertical scroll bar, which `Overlay::contentWidth()`
takes off when the body scrolls, and the body's horizontal scroll bar is always off
(`Overlay.cpp`). So the row of cards, whose size is fixed, and the two lines that no longer wrap are
wider than the space they get. Before phase 13 the sheet was 720 wide and its lines wrapped.

**Why it was left.** Only a short or narrow window shows it, and the app opens maximized. The fix is
small: add the scroll bar's width in `preferredWidth()` when the body scrolls, and let the two lines
wrap again when the sheet is narrower than they are.

**Where it goes.** Not scheduled. Left by the owner's call after PR #39's review.

## The language dialog copies the main-menu page's code

*Found in PR #39's review (phase 13). Not scheduled.*

**What you see.** Nothing on screen. It is a cost for the next change: `LanguageDialog.cpp` repeats
`MainMenuPage.cpp` almost line for line. `logo()` is `MainMenuPage::logo()` at another scale and
with one word, the lit-panel loop in `paintEvent()` is the same, so is `eventFilter()`, and the
plaque's half height is worked out by hand from `tablespace::logoBox`. A fix to one copy has to be
made in the other as well. The two already differ: the menu page repaints on
`QEvent::EnabledChange` and the dialog does not, which is harmless only because the dialog's buttons
are never disabled.

**Why.** The dialog was built in phase 13 from the menu page, and copying left the menu page
untouched.

**Why it was left.** Sharing the code means helpers both pages use (the logo's picture at a scale
with its lines, and the lit-panel painter), then checking both pages' screenshots. That is real work
for no visible change.

**Where it goes.** Not scheduled. Left by the owner's call after PR #39's review.

## The language dialog lights its button by dropping and retaking the focus

*Found in PR #39's review (phase 13). Not scheduled.*

**What you see.** Nothing on screen: when the dialog's window becomes active, the flag that has the
keyboard is lit, as it should be.

**Why.** A `LitButton` is lit by the keyboard only when its focus came by Tab
(`LitButton::focusInEvent()`), and when a window is activated Qt sends no focus event to the widget
that already has the keyboard. So on every `WindowActivate`, `LanguageDialog::event()` waits one
turn of the event loop (`QTimer::singleShot(0)`) for Qt to hand the focus back, then has the button
clear its focus and take it again with `Qt::TabFocusReason`. `Overlay::focusNextInside()` does the
same. Each activation sends a made-up focus-out and focus-in, and the timing works only because Qt's
own handover finishes first.

**Why it was left.** The fix belongs in `LitButton`: a method that marks a button as lit by the
keyboard (`setKeyboardLit()`, say), used by the dialog and by `Overlay`, which removes both copies.
Focus code needs careful testing, and the fix changes nothing visible.

**Where it goes.** Not scheduled. Left by the owner's call after PR #39's review.

## A gendered sentence's translation context is written out by hand several times

*Found in PR #40's review. Not scheduled.*

**What you see.** Nothing today: every gendered sentence shows its Romanian. The risk is the next
change. If one copy of a context is mistyped, that sentence shows English in the Romanian app, for
the one gender or the one seat (yours or somebody else's) whose variant has the wrong copy.

**Why.** `QT_TRANSLATE_NOOP3` gives lupdate the context, but its expansion keeps only the source and
the disambiguation (`persontext::Variant`). So the code names the context a second time where the
string is looked up: in `persontext::Sentence::context`, or as the first argument of
`persontext::translate()` for a `Gendered`. `leaderSentence` and `firstToBidSentence` in
`TrickPanelItem.cpp` write `"trickpanel"` five times each, and `youBidLast` in `BidStripItem.cpp`
writes `"bidstrip"` three times. Nothing checks that the copies agree. When they don't, lupdate files
the Romanian under the macro's context, the lookup asks for the other one, and Qt returns the English
source without a warning. A test run in English can't tell.

**Why it was left.** The full fix changes how the sentences are written and how lupdate runs. A macro
of our own, `PERSONTEXT_NOOP(context, source, disambiguation)` say, would expand to a `Variant` that
carries its own context. lupdate would be told to read it as another spelling of
`QT_TRANSLATE_NOOP3` (`-tr-function-alias QT_TRANSLATE_NOOP3+=PERSONTEXT_NOOP` in the
`LUPDATE_OPTIONS` of `CMakeLists.txt`; Qt 6.11's lupdate lists `QT_TRANSLATE_NOOP3` among the
functions it can alias, but this is untried). Each variant would then be looked up under the context
lupdate filed it under, so there would be no second copy to get wrong, and `Sentence::context` and
`translate()`'s context argument would go. The review chose a cheaper guard for now: two tests load
the app's own Romanian and fail as soon as any variant would show English in the app. They are "The
leader and first-bidder lines are in Romanian for everyone" (`TrickPanelTests.cpp`) and "You bid
last is in Romanian as a man and as a woman" (`BidStripTests.cpp`). A new gendered sentence needs
adding to one of them.

**Where it goes.** Not scheduled. Left by the owner's call after PR #40's review.

## The avatar rule lives in the paint layer, and two UI modules include each other

*Found in PR #40's review. Not scheduled.*

**What you see.** Nothing on screen. It is a cost for the next change. `avatars::Wearer` (a gender
and a bot's strategy) and `avatars::assign()` decide who wears which face. They are game-setup
rules, but they live in `paint/Avatars.h`, beside the catalogue that `Theme` draws from. So
`NewGameDialog.h` and `PlayerNames.h` include a paint header, and `playernames::draw()` takes an
`avatars::Wearer` although names have nothing to do with avatars. `PlayerNames.cpp` also includes
`ui/NewGameDialog.h` only for `newgame::MaximumNameLength`, while `NewGameDialog.cpp` includes
`ui/PlayerNames.h`, so each module depends on the other. The strategy keys both catalogues use
(`newgame::avatarStrategyOf()`) are in the dialog too.

**Why.** `Wearer` was written for the avatar rule (65be5fb). When bots' names came to be drawn by
gender and strategy as well (56cf77e), the names rule reused it where it was, and took the length
limit from the dialog that already had it.

**Why it was left.** The fix moves code between modules and touches about ten files, tests included,
and changes nothing the player sees. `Wearer` and the strategy keys would move to `core/`, beside
`PlayerProfile`. `MaximumNameLength` would go to a header both modules can use (`PlayerProfile.h`,
since it is a rule about a player's name). `assign()` could then stay with the catalogue or move with
`Wearer`.

**Where it goes.** Not scheduled. Left by the owner's call after PR #40's review.

## The avatar catalogue and the names list are read by two copies of one loop

*Found in PR #40's review (finding #5). Scheduled with nationality.*

**What you see.** Nothing on screen. It is a cost for the next change: `avatars::parse()`
(`src/paint/Avatars.cpp`) and `playernames::parse()` (`src/ui/PlayerNames.cpp`) each read their JSON
array the same way. Each reads `"gender"` as "male", "female" or "any", reads the optional
`"strategy"` and checks it is text, and skips a bad entry with a warning. A field both gain has to be
written twice, and the two can drift.

**Why.** The names list was built after the avatar catalogue, on its pattern, and the two rightly
differ in part. An avatar's id is a key, compared exactly. A name is shown to people, so it is
trimmed, held to `newgame::MaximumNameLength`, and compared ignoring case within its language. One
reader for both would have to choose one rule, and neither fits the other.

**Why it was left.** Two fields in common, gender and strategy, did not pay for a shared helper and
the changes to both modules and their tests. The owner's call after PR #40's review.

**Where it goes.** With nationality, which both are to gain. Then share a small helper for the common
fields (gender, strategy, nationality), and keep each file's own rules for its id or name and its
duplicates.

## In a huge, squarish window the scoreboard touches the top-right player's panel

*Found in PR #41's review (findings #1 and #2). Not scheduled.*

**What you see.** In a window about 4:3 or squarer and more than about 3,230 pixels wide, with three,
five or six players and long names, the scoreboard's left edge reaches the top-right player's panel.
The two panels touch, the scoreboard's border over the panel's rounded edge by a pixel or two at
3840 × 2880. Nothing on either is covered: the name, the pill and the portrait stay clear.

**Why.** In a window narrower than the fit box's 1880 : 1324, about 1.42 : 1, the window's width sets
the table's scale. The scoreboard's right edge is pinned 8 pixels from the window's edge, and the
table's side margin is 16 pixels. Both shrink in table units as the scale grows. So the scoreboard's
left edge (283 units wide at its cap, with 20-character names) moves out towards 1424.8 units, short
of the top-right seat's right edge at 1429.5 once the scale passes about 1.7. With ordinary names
the scoreboard is narrower and clear. The old layout had the same edge case, mirrored, on the
top-left seat.

**Why it was left.** No common setup reaches it. 16:9, 16:10 and wider screens are always clear,
because there the window's height sets the scale. Squarish monitors are far narrower than 3,230
pixels, and a 4K screen at the usual 150% or 200% scaling counts as 2560 or 1920 wide. About the only
way there is a window stretched over two big monitors stacked one above the other, at 100% scaling.
And what you see is two panels touching. The owner saw it simulated and left it, after PR #41's
review.

**Where it goes.** Not scheduled. If it is wanted: widen `tablespace::fitBox` from 1880 to about 1900
units. That leaves about 5 units of room at any scale, and changes nothing in windows wider than
about 1.43 : 1 (the screenshots included). In narrower windows the table is drawn at most about 1%
smaller, and the window's minimum size grows by a few pixels (update `TableSpaceTests`). Then add
large squarish sizes, such as 2560 × 1800 and 3840 × 2700, to `HudTests`' clearance case; the
second fails without the change.
