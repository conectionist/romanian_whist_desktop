# Code quality review

A review of the desktop client as of v2.1.0 (commit `7babaed`, October 2026): what is good, what
gets in the way, and what to change first. The engine submodule is out of scope.

**Verdict: 6 out of 10.** On a scale where 1 is AI slop and 10 is a codebase others should copy.
On correctness, threading and testing it is an 8. On readability and maintainability it is a 4 or 5,
mostly because of the comments and the size of `MainWindow`. Every one of those problems is fixable
without touching what works.

## How this was measured

- Read `src/` (25.7k lines), `tests/` (27.7k lines), both CMake files, both CI workflows and `docs/`.
- Counted lines of code against lines of comment in `src/`, and counted how often the comments
  refer to project history (phases, plan sections, PRs, review findings).
- Checked CI: the last runs on `master` and on the PR before it passed on Linux, Windows and macOS.
  The client was not built locally for this review.

## Strengths

### The thread boundary is designed with care

The engine runs on its own thread and the GUI never touches it directly. Everything crosses through
three small classes (`QtMoveProvider`, `GameBridge`, `GameController`), and the hard cases are
handled:

- **Stale clicks are dropped.** Each prompt has a generation number, and `QtMoveProvider::submit()`
  ignores an answer for any other one. A double click or a click during a bot's turn cannot answer
  the next prompt.
- **Quitting never hangs.** `abandon()` sets a flag and wakes the waiting game thread, which then
  throws `GameAbandoned`. The flag is checked both before a prompt goes out and after the wait.
- **No self-deadlock.** The mutex is released while the prompt signal is emitted, so a handler that
  answers straight away (a direct connection) can take it.
- **Lifetimes across threads are planned.** The engine owns a `SeatProvider`, a thin forwarder,
  never the real `QtMoveProvider`. So nothing the GUI thread uses is destroyed on the game thread.
  `GameController` is declared last in `MainWindow`, so the game thread is joined before the widgets
  its slots write to are gone.

### It does not trust its own UI

An illegal bid or card asks again instead of letting the engine throw out of `run()`. A submitted
bet is range-checked as a `size_t` before it is narrowed (`QtMoveProvider.cpp`), so a huge value
cannot wrap round to a legal one.

### The tests are better than in most professional codebases

- About 705 test cases, and slightly more test code than application code.
- Test names describe behaviour: "The window can be closed with the hand waiting on a card",
  "Abandoning while you bid leaves nothing of the bid on the menu page".
- Threading tests drive a real engine on a real thread, and every wait has a deadline, so a broken
  test fails instead of hanging.
- Comments show the tests were checked by deliberately breaking the code and watching them fail.

### Build, CI and release are solid

- `desktop_core` is a static library holding everything but `main()`, so the tests link the real
  code.
- CMake presets cover Debug, Release, a second compiler (Clang) and ASan/UBSan.
- Warnings (`-Wall -Wextra -Wpedantic`, `/W4`) are on for the project's own targets only.
- CI builds and tests on three operating systems, checks the packaged output (including the Visual
  C++ runtime on Windows) and publishes releases from the version number.
- The deploy setup is trimmed to what the app loads, and the reasons are written down.

### It is tidy

- Layout numbers are named constants that say where in the mock-up they come from
  (`ScoreboardItem.cpp`).
- No `TODO`, `FIXME` or commented-out code.
- `docs/KNOWN_ISSUES.md` records each deferred defect with what you see, why, why it was left and
  where it is scheduled.
- Translations go through Qt Linguist, and the Romanian is complete.

## Weaknesses

### 1. There are too many comments

| | Lines |
|---|---|
| Code in `src/` | 13,131 |
| Comments in `src/` | 7,808 |
| Comment lines per 10 lines of code | 6 |

Some headers are mostly comment:

| File | Code | Comment |
|---|---|---|
| `src/engine/GameController.h` | 64 | 255 |
| `src/MainWindow.h` | 150 | 294 |
| `src/table/TableSpace.h` | 71 | 144 |
| `src/paint/CardRenderer.h` | 35 | 82 |

About 735 comment lines open in capitals ("UNLOCKED ACROSS THE EMIT", "NOT A GameController
MEMBER"). When every comment is emphasised, none of them stands out. The reader has to scroll past
paragraphs to find a few lines of code, and the important warnings (the ones that prevent a
deadlock or a use-after-free) are buried among ones that are not.

### 2. The comments read like a changelog

Occurrences in `src/`:

| Reference | Count |
|---|---|
| "section N" (of a 3,100-line plan in `docs/`) | 276 |
| "phase X" | 159 |
| Internal step labels such as "C5", "D1", "B1" | 72 |
| "the owner" | 56 |
| PR numbers | 53 |
| "finding #N" | 45 |

Examples:

- `src/MainWindow.cpp:49` is a comment about code that no longer exists, with no code under it:
  "PHASE A's HARDCODED GAME IS GONE, and this is what replaced it: nothing."
- `src/MainWindow.cpp:312` tells the story of an old bug: "this used to hide it here … The comment
  claimed the dock came back with the next one; it did not."
- `src/engine/QtMoveProvider.cpp:96`: "NO TEST CAN PIN THIS LINE, and C5 established that by
  trying."
- Several comments cite "the owner's call" with a date.

This is history, and `docs/KNOWN_ISSUES.md:13` already states the rule: "This file is not a
history; `git log` is." The code comments do not follow it. These references also go stale: the
section numbers point into `QT_CLIENT_PLAN_proposal1.md`, and a reader without that document (or
with a later version of it) cannot follow them.

This comment style is the clearest sign the code was written by an AI agent, and it is the part a
human reviewer would object to first.

### 3. `MainWindow` does too much

`MainWindow.cpp` is 1,543 lines. The window holds about 25 child widget pointers and builds the
table, the menu page, seven overlays, the menu bar and the log dock. It also arbitrates the game's
state through loose members:

```cpp
bool haveStarted = false;
bool closing = false;
bool returningToMenu = false;
bool pauseMenuHolding = false;
bool roundSheetHolding = false;
bool dialogHolding = false;
bool sheetHolding = false;
TableState tableState = TableState::NoGame;
```

The hold logic itself is right: each reason is kept apart and `applyHold()` ORs them, so releasing
one cannot release another. But it lives as four loose flags in a large class, and several comments
(`canPause()`, `showMenuSheet()`) describe bugs that came from flags interacting. `closing` and
`returningToMenu` are two flags for one question: why the game was stopped.

Other large files: `NewGameDialog.cpp` (1,219 lines), `Overlay.cpp` (1,052) and
`tests/MainWindowTests.cpp` (2,526).

### 4. The C++ is closer to C++17 than C++20

The project requires C++20 but uses little of it:

- No `std::ranges` anywhere. `std::find(v.begin(), v.end(), x) != v.end()` appears in
  `HandLayout.cpp:37` and `QtMoveProvider.cpp:204`, and `std::sort` takes iterator pairs in
  `HandLayout.cpp:21` and `CardOrder.cpp:48`.
- A hand-written loop fills `0, 1, 2, …` where `std::iota` would do (`HandLayout.cpp:18`).
- No `[[nodiscard]]`, and a single `noexcept`.

None of this is a bug, but it is not what a C++20 codebase used as an example would look like.

### 5. Style and warnings are not enforced

- No `.clang-format`, so the consistent style depends on whoever (or whatever) writes the code.
- No clang-tidy or other static analysis.
- `-Werror` is off on purpose (`CMakeLists.txt`), but CI does not turn warnings into errors either,
  so a new warning passes unnoticed.
- The ASan/UBSan preset exists but CI does not run it.

### 6. Planning documents live next to the code

`docs/` holds about 8,000 lines of plans and review records (`QT_CLIENT_PLAN_proposal1.md`,
`QT_CLIENT_BUILD_ORDER.md`, `PHASE_A_PLAN.md`, `ui-redesign/IMPLEMENTATION_PLAN.md`,
`ui-redesign/PHASE5_REVIEW_FINDINGS.md`) and 12 MB of redesign history, including a 4.5 MB video.
They were useful while the work was planned. Now the work is done, they mostly explain the code
comments that point at them.

### 7. Smaller points

- Some tests prove that something stays blocked by sleeping a fixed 100–200 ms and checking it did
  not move (`GameBridgeTests.cpp`, `GameControllerTests.cpp`). That is a fair way to test a
  negative, but it adds time and can fail on a heavily loaded machine.
- Phrases such as "for the reason makeBet() gives" send the reader to another function to
  understand this one.

## Suggestions, most important first

### 1. Set a comment rule, then thin the comments

The code is written by an agent, so the fix that lasts is a rule in `CLAUDE.md`. Without one, the
next change writes comments the same way. Suggested text:

```markdown
## Comments

- A comment says why the code is the way it is, when the code cannot say it. One to three lines.
- Never mention phases, plan sections, PR numbers, review findings, dates or who decided something.
  That is history, and history goes in the commit message.
- Never describe code that was removed or what the code used to do.
- No capitals for emphasis. Keep the warnings that prevent a crash, a deadlock or a hang, and make
  them short.
- Say a reason once, next to the code it explains. Don't send the reader to another function.
```

Then clean up the existing comments, one area per PR, starting with the headers people read first:
`GameController.h`, `MainWindow.h`, `QtMoveProvider.h`, `GameBridge.h`. A target of about 1 comment
line per 4 lines of code is realistic. For example, this 15-line comment in
`QtMoveProvider::makeBet()` …

```cpp
// Still needed, and not made redundant by the check above: the wait
// returns on an abandon with `answer` empty, and falling through would
// dereference an empty optional.
//
// NO TEST CAN PIN THIS LINE, and C5 established that by trying. ...
// (10 more lines)
if(abandoned)
    throw GameAbandoned{};
```

… keeps everything a reader needs in two lines:

```cpp
// The wait also returns on abandon, with `answer` empty: never read it after one.
// No test can catch this line's removal, so don't delete it.
if(abandoned)
    throw GameAbandoned{};
```

A grep in CI can keep the history out once it is gone:

```bash
! grep -rnE '\b([Pp]hase [A-Z0-9]+|[Ss]ection [0-9]+|PR ?#[0-9]+|finding #?[0-9]+)' src
```

### 2. Take state and overlays out of `MainWindow`

Put the hold reasons in one small type, so the window says what it means and the rule "any reason
holds the game" lives in one place:

```cpp
enum class HoldReason
{
    PauseMenu,
    RoundSheet,
    NewGameDialog,
    MenuSheet,
    Count
};

class GameHolds
{
public:
    void set(HoldReason reason, bool held)
    {
        reasons.set(static_cast<std::size_t>(reason), held);
    }

    [[nodiscard]] bool any() const
    {
        return reasons.any();
    }

private:
    std::bitset<static_cast<std::size_t>(HoldReason::Count)> reasons;
};
```

Replace `closing` and `returningToMenu` with one value, since they answer one question:

```cpp
enum class StopReason
{
    None,
    WindowClosing,
    ReturningToMenu
};
```

Then move the seven overlays (creating them, raising one over another, holding the game while one
is open, giving the keyboard back) into their own class, so `MainWindow` connects the controller to
the table and little else. Aim for `MainWindow.cpp` under 600 lines. The existing `MainWindowTests`
cover this behaviour well, which makes the refactor safe.

### 3. Enforce style and warnings in CI

- Add a `.clang-format` matching the current style (Allman braces, `if(` with no space, 4-space
  indent, about 100 columns) and check it in CI.
- Add a `.clang-tidy` with `bugprone-*`, `modernize-*` and `readability-*` (minus the noisy ones)
  and run it on `desktop_core` in the Linux job.
- Add an option such as `WHIST_DESKTOP_WERROR` that appends `-Werror` / `/WX` to
  `WHIST_DESKTOP_WARNINGS`. It stays off by default, as the comment in `CMakeLists.txt` wants, and
  CI turns it on. That keeps the engine and Catch2 out of it, which
  `CMAKE_COMPILE_WARNING_AS_ERROR` would not.
- Run the `asan` preset's tests in CI, on Linux, nightly or on PRs to `master`.

### 4. Use C++20 where it reads better

```cpp
std::vector<std::size_t> ordered(hand.size());
std::iota(ordered.begin(), ordered.end(), std::size_t{0});

std::ranges::sort(ordered, [&hand, &trump](std::size_t left, std::size_t right)
{
    return cardorder::inDisplayOrder(hand[left], hand[right], trump);
});

const bool legal = !choosing || std::ranges::find(legalCards, card) != legalCards.end();
```

With C++23 the last line becomes `std::ranges::contains(legalCards, card)`. That needs the standard
library of every CI compiler to support it (Apple Clang's is the one to check first), so it can
wait.

Add `[[nodiscard]]` to functions whose result must be used, such as `layOutHand()`,
`newgame::buildDeal()` and `Theme::forScheme()`. clang-tidy's `modernize-use-nodiscard` and
`modernize-use-ranges` checks will find most of the rest.

### 5. Archive the finished plans

Move the plans and review records in `docs/` to `docs/archive/`, or delete them and let git keep
them. If some decisions should stay visible, write each as a short decision record: what was
decided, why, and what was rejected. Keep the redesign video and frames out of the repo (a release
asset or a wiki page), so cloning stays small.

### 6. Split the largest test files

Split `tests/MainWindowTests.cpp` (2,526 lines) by area: pause, sheets, keyboard, game lifecycle.
Where a fixed sleep proves a negative, keep it, but give the duration a named constant shared
by every such test, so it can be raised in one place on a slow CI runner.

## What would make it an 8

Suggestions 1 to 3. They take nothing away from the parts that are already good (the threading,
the tests and the CI), and they remove what a reader trips over first.
