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
