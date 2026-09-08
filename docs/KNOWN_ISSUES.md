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

## The trump card is stale after a screen or font change

*Found in the B2b review. Deferred to phase E.*

**What you see.** Drag the window from one screen to another with a different device pixel ratio,
part-way through a round, and the trump card in the info strip stays at the old screen's resolution
— soft on the sharper screen, or needlessly heavy on the coarser one. The cards on the table beside
it re-rasterize correctly, which makes the strip look like the odd one out. The same gap applies to
a font change under a running window: the card keeps its old size and the strip its old margins,
while every label inside it takes the new font.

**Why.** `InfoStrip::refresh()` is the only place that rasterizes, and `setSnapshot()` will not call
it unless something in the round actually changed. That guard is not incidental — `refresh()` is on
a path that runs on every one of a game's ~1300 frames, and a `QLabel::setText()` is a relayout
whether the text differs or not. A screen change moves the ratio without moving the round, so the
guard correctly decides there is nothing to redraw and is wrong about the pixmap. `trumpSlot` and
the layout's margins have the matching gap for `QEvent::FontChange`: both are derived from the font
in the constructor and never again.

**Why it was left.** The fix is not the one-liner it looks like. It needs a way *past* a change
guard that exists for a good reason — so either a force flag through `setSnapshot()` or, better,
splitting rasterization out of `refresh()` so the two can be triggered independently — plus a
`changeEvent()` override handling `QEvent::ScreenChangeInternal` and `QEvent::FontChange` together.
That is real machinery, and what it buys is a card that looks slightly wrong on a multi-monitor drag
mid-round. It was found at the end of phase B, where the alternative was holding the phase open for
it.

**Where it goes.** Phase E, with the rest of §18.1's device-pixel-ratio work — the `QSize trumpSlot`
member exists partly for this: recomputing it is the whole of the font half of the fix.
