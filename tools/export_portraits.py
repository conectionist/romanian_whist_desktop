#!/usr/bin/env python3
"""Write the table mock-up's six portraits as SVG files.

The client drew these six head-and-shoulders busts in code until the portraits
became files (resources/avatars.qrc), so that new avatars could be added as art
rather than as drawing code. This writes the original six from the mock-up's own
source, so they are the same drawing the client painted:

    python3 tools/export_portraits.py            # writes resources/images/avatars/
    python3 tools/export_portraits.py --out DIR

WHERE EVERY NUMBER COMES FROM: docs/ui-redesign/mockup/source/table.js. The
template below is its portrait() function, path for path, and LOOKS is its
PLAYERS[].look, colour for colour, in the same order. Change either there first.

The sepia filter page.html puts on a portrait is NOT baked in: the client applies
it to every avatar as it draws one (Theme::avatar()), files added later included.

Needs nothing beyond the standard library. The output is deterministic.
"""

import argparse
import pathlib

# table.js PLAYERS[].look, in its order, under the id each file is known by in
# resources/images/avatars/avatars.json. The mock-up's own names for the six are
# in the comments; its look ids (0-5) are the gradient's id below.
LOOKS = [
    # "You"
    ("man-dark-hair-beard", dict(id=0, bg="#6b5233", skin="#d9ab86", shade="#b98a68", hair="#2a1c14",
                                 coat="#3a3530", shirt="#d8cfbf", beard="#3a2a1e")),
    # "Bogdan"
    ("man-hat-mustache", dict(id=1, bg="#5a4a35", skin="#cf9f7c", shade="#a87c5e", hair="#3b2a1c",
                              coat="#4a3524", shirt="#bfb39e", hat="#3d3a36", mustache="#3b2a1c")),
    # "Ana"
    ("woman-long-brown-hair", dict(id=2, bg="#6e5a3e", skin="#e2b997", shade="#c09576", hair="#4a2f1d",
                                   coat="#4b3324", shirt="#e8e0cf", longHair=True)),
    # "Costel"
    ("man-white-hair-hat", dict(id=3, bg="#5f5440", skin="#d5a888", shade="#ae8466", hair="#d8d2c6",
                                coat="#3c3a32", shirt="#cfc6b2", hat="#6a5134", mustache="#ece6da")),
    # "Dana"
    ("woman-long-red-hair", dict(id=4, bg="#5c4636", skin="#e8c2a2", shade="#c79e80", hair="#8a3f22",
                                 coat="#2f3b34", shirt="#e4dccb", longHair=True)),
    # "Emil"
    ("man-black-hair-beard", dict(id=5, bg="#4f4a3c", skin="#c99877", shade="#a3765a", hair="#1f1712",
                                  coat="#5a4a32", shirt="#d2c8b4", beard="#1f1712")),
]


def portrait(p):
    """table.js portrait(p), with the xmlns a file needs and a browser's inline SVG does not."""
    g = f"pg{p['id']}"
    parts = [
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 114">',
        f'  <defs><radialGradient id="{g}" cx="50%" cy="36%" r="72%"><stop offset="0" stop-color="{p["bg"]}"/>'
        f'<stop offset="1" stop-color="#17100a"/></radialGradient></defs>',
        f'  <rect width="100" height="114" fill="url(#{g})"/>',
    ]

    if p.get("longHair"):
        parts.append(f'  <path d="M29 46 Q24 84 32 100 L68 100 Q76 84 71 46 Z" fill="{p["hair"]}"/>')

    parts += [
        f'  <path d="M6 114 Q10 82 34 77 L66 77 Q90 82 94 114 Z" fill="{p["coat"]}"/>',
        f'  <path d="M39 77 L50 94 L61 77 Z" fill="{p["shirt"]}"/>',
        f'  <rect x="43" y="62" width="14" height="17" rx="5" fill="{p["shade"]}"/>',
        f'  <ellipse cx="50" cy="48" rx="16" ry="19.5" fill="{p["skin"]}"/>',
        f'  <ellipse cx="33.5" cy="50" rx="2.6" ry="4.4" fill="{p["shade"]}"/>'
        f'<ellipse cx="66.5" cy="50" rx="2.6" ry="4.4" fill="{p["shade"]}"/>',
        f'  <path d="M34 44 Q33 26 50 25 Q67 26 66 44 Q62 34 50 33.5 Q38 34 34 44Z" fill="{p["hair"]}"/>',
    ]

    if p.get("hat"):
        parts.append(f'  <path d="M32 38 Q33 22 50 21 Q68 22 69 37 Q50 32 32 38Z" fill="{p["hat"]}"/>'
                     f'<path d="M30 39 Q50 31 74 37 L72 42 Q50 36 31 42Z" fill="{p["hat"]}" opacity=".8"/>')

    parts += [
        '  <path d="M41 46.5 Q44 44.8 47 46.5 M53 46.5 Q56 44.8 59 46.5" stroke="#2a1a10" stroke-width="1.6" '
        'fill="none" stroke-linecap="round"/>',
        f'  <path d="M48 52 Q50 56 52 52" stroke="{p["shade"]}" stroke-width="1.4" fill="none"/>',
    ]

    if p.get("mustache"):
        parts.append(f'  <path d="M41 58 Q50 53 59 58 Q55 61 50 59 Q45 61 41 58Z" fill="{p["mustache"]}"/>')
    else:
        parts.append('  <path d="M45 60 Q50 62.5 55 60" stroke="#8a4f3c" stroke-width="1.5" fill="none" '
                     'stroke-linecap="round"/>')

    if p.get("beard"):
        parts.append(f'  <path d="M36 54 Q38 70 50 70 Q62 70 64 54 Q60 64 50 64 Q40 64 36 54Z" '
                     f'fill="{p["beard"]}" opacity=".35"/>')

    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=pathlib.Path, default=root / "resources" / "images" / "avatars")
    out = parser.parse_args().out
    out.mkdir(parents=True, exist_ok=True)

    for name, look in LOOKS:
        path = out / f"{name}.svg"
        path.write_text(portrait(look), encoding="utf-8")
        print(path)


if __name__ == "__main__":
    main()
