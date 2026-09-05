# Verifying locally

**Why this document exists:** the GitHub Actions minute quota for this account is spent, so CI
cannot be relied on for correctness until it resets. Everything CI used to do has to happen on one
Linux machine, and what it did that this machine cannot is worth knowing rather than forgetting.

## What CI actually gave, and what is lost

CI compiled and ran the test suite on Linux, Windows and macOS. **It never ran a sanitizer**, so it
was never the strongest correctness check here — the local ASan/UBSan/TSan runs are, and two real
defects in phase A were found by them and by nothing else.

What CI uniquely gave was **portability**, and that is what is gone:

| Lost | Substitute | Quality of the substitute |
|---|---|---|
| AppleClang | `clang` preset | Partial — the same front end, a different standard library |
| MSVC | none | None. Windows is unchecked until the quota resets |
| libc++ | none | Impossible here — see below |

**libc++ cannot be used against the official Qt Linux binaries.** They are built with libstdc++, so
anything crossing the ABI with a standard type fails to link — `QString::toStdString()` is the one
this repository happens to hit. Building Qt from source to change that is not worth it.

**It would not help anyway, and that was measured rather than assumed.** libc++ can still *parse*
this tree (`-fsyntax-only` links nothing, so the ABI never comes up), so it was tried as an
include check: nine standard includes were deleted one at a time and each toolchain asked whether
it noticed.

| Removed | gcc/libstdc++ | clang/libstdc++ | clang/libc++ |
|---|---|---|---|
| `<algorithm>` from `HandLayout.cpp` | catches | catches | **misses** |
| `<utility>`, `<memory>`, `<mutex>`, `<optional>`, `<cstddef>`, `<vector>` (8 cases) | misses | misses | misses |

libc++ caught nothing the others did not and missed one they caught. The reason is structural, not
luck: every `.cpp` here includes its own header first, that header pulls in Qt and engine headers,
and those drag in most of the standard library transitively — so there is nothing left for a
stricter library to object to. **Do not reach for libc++ again expecting it to find missing
includes in this repository.** MSVC may still find them, because it differs in what it drags in
rather than in how strict it is.

## Before opening a PR

```bash
QT=/home/dan/Qt/6.11.2/gcc_64      # Qt is not on PATH; `which qmake6` finds nothing

# 1. The everyday build
cmake --preset default -DCMAKE_PREFIX_PATH=$QT && cmake --build build -j
ctest --preset default --output-on-failure --no-tests=error

# 2. Release, which is a different configuration and the one CI used
cmake --preset release -DCMAKE_PREFIX_PATH=$QT && cmake --build build-release -j
ctest --preset release --output-on-failure --no-tests=error

# 3. The second compiler
cmake --preset clang -DCMAKE_PREFIX_PATH=$QT && cmake --build --preset clang -j
ctest --preset clang --output-on-failure --no-tests=error
```

**Check the test COUNT, not just the colour.** `ctest` on a stale build directory happily runs an
old binary and reports it green — a preset last configured before a commit that added tests reports
the old number and passes. All three presets must report the same count as each other. This has
already happened once: `release` reported 42 while `default` reported 46.

Warnings are on for `desktop_core` and `desktop_tests` only (`-Wall -Wextra -Wpedantic`, `/W4` on
MSVC); the engine submodule and Catch2 are built as they come. The build should be silent. If a
change makes it noisy, that is the finding.

## Sanitizers, for anything that touches a thread

Not optional on this repository: every threading defect found so far was found this way or by
deliberately breaking a fix, and none by reading.

```bash
S=/tmp/build-tsan    # or anywhere outside the source tree

cmake -S . -B $S -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=$QT \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -g -O1" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"

setarch -R cmake --build $S --target desktop_tests -j
setarch -R $S/tests/desktop_tests --order rand
```

**`setarch -R` is required, not a nicety.** Kernel 6.8's ASLR entropy collides with
ThreadSanitizer's shadow mappings, and without it TSan dies with "unexpected memory mapping" before
running a line. That is an environment quirk, not a fault in this code.

Swap `thread` for `address,undefined` for the ASan/UBSan pass, which needs no `setarch`. Run each
several times with `--order rand`: a race that reproduces once in twenty is the normal case.

## Running the APPLICATION under ASan

The test suite needs nothing special. The application does:

```bash
ASAN_OPTIONS=suppressions=$PWD/tools/asan.supp:detect_leaks=0 ./build/romanian_whist_desktop
```

Without the suppression every run aborts inside `w.show()` before the window appears —
`libxcb-cursor` allocates a 21-byte buffer and `strchr`s past the end of it while loading the
window's cursor, reached from `QWindowPrivate::applyCursor()`. The allocation and the read are both
inside that library; the only frame of ours in the report is the `show()` that triggered it. It is
a system-library bug, and suppressing it is what lets ASan get far enough to find one of ours.

## Stress, for the same reason

```bash
for i in $(seq 1 40); do ./build/tests/desktop_tests >/dev/null || echo "run $i FAILED"; done
```

And a batch in parallel to put the machine under load — timing bugs surface under contention that
never appear on an idle box.

## The habit that has actually found things

**Break each fix on purpose and confirm the test fails.** Every phase so far has had at least one
assertion that passed for the wrong reason: a hand comparison that was `0 == 0` because the deal had
not happened, a shutdown test that passed on a ten-second timeout rather than on a handshake, a
CMake edit that reported success while matching nothing. None of those would have been caught by
running the suite; all of them were caught by removing the code under test and watching what did
*not* fail.

## When the quota resets

Re-run CI on the branch before merging anything that landed in the meantime — Windows especially,
since nothing here has compiled against MSVC in that window. The likely findings are missing
`#include`s that libstdc++ supplies transitively and libc++/MSVC do not, and `windows.h`'s `min` and
`max` macros, which `romanian_whist_terminal` hit and documents in `src/Pacer.cpp`.
