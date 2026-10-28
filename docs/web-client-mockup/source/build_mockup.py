#!/usr/bin/env python3
"""Rebuild docs/web-client-mockup/web_client_mockup.html from its sources.

A mock-up of the browser client planned in the engine's docs/web-client-plan.md.
Like the table redesign's mock-up (docs/ui-redesign/mockup/), the built page is one
self-contained file that opens in any browser, so it embeds its pictures as data
URIs. The readable parts live here:

    page.html   markup and CSS
    mockup.js   the screens: geometry, sample states, the little interaction there is

and this script stitches them together with the art, taken straight from this
repository's resources so the mock-up and the desktop client use the same pictures:

    resources/images/cards/png/<rank>_of_<suit>.png   only the cards the screens show
    resources/images/avatars/*                         the portrait catalogue
    resources/images/table/table_wood.png, table_felt.png

    python3 docs/web-client-mockup/source/build_mockup.py [--body-only OUT]

--body-only writes the page without its <!doctype>/<head> lines, for a host that
wraps a page in a document of its own.

Needs Pillow (for the WebP re-encode that keeps the page small). The fonts come
from Google Fonts, as the redesign mock-up's do, so they need a connection.
"""

import base64
import io
import json
import pathlib
import sys

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[2]
OUT = HERE.parent / 'web_client_mockup.html'

RANK_FILES = {'3': '3', '4': '4', '5': '5', '6': '6', '7': '7', '8': '8', '9': '9', 'T': '10',
              'J': 'jack', 'Q': 'queen', 'K': 'king', 'A': 'ace'}
SUIT_FILES = {'C': 'clubs', 'D': 'diamonds', 'H': 'hearts', 'S': 'spades'}

HEAD = ('<!doctype html>\n<html lang="en">\n<meta charset="utf-8">\n'
        '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n')

# The cards the screens put on the table, in a hand or as the trump. Keeping the
# list short keeps the page small; add a card here when a screen needs it.
CARDS = ['AH', 'TH', '7H', 'JC', 'QD', '8D', '9D', 'KD', '6D', '7D', 'TS', 'KS']


def webp_uri(image, quality):
    buffer = io.BytesIO()
    image.save(buffer, 'WEBP', quality=quality, method=6)
    return 'data:image/webp;base64,' + base64.b64encode(buffer.getvalue()).decode()


def card_images():
    folder = REPO / 'resources' / 'images' / 'cards' / 'png'
    cards = {}
    for code in CARDS:
        image = Image.open(folder / f'{RANK_FILES[code[0]]}_of_{SUIT_FILES[code[1]]}.png').convert('RGBA')
        image = image.resize((320, round(320 * image.height / image.width)), Image.LANCZOS)
        cards[code] = webp_uri(image, 86)
    return cards


def avatar_images():
    folder = REPO / 'resources' / 'images' / 'avatars'
    catalogue = json.loads((folder / 'avatars.json').read_text())
    avatars = {}
    for entry in catalogue:
        path = folder / entry['file']
        if path.suffix == '.svg':
            uri = 'data:image/svg+xml;base64,' + base64.b64encode(path.read_bytes()).decode()
        else:
            # Twice the 88 x 100 portrait box, for sharp portraits on a high-density screen.
            image = Image.open(path).convert('RGB')
            image = image.resize((176, round(176 * image.height / image.width)), Image.LANCZOS)
            uri = webp_uri(image, 82)
        avatars[entry['id']] = {'gender': entry['gender'], 'strategy': entry.get('strategy'), 'src': uri}
    return avatars


def main():
    textures = REPO / 'resources' / 'images' / 'table'
    wood = webp_uri(Image.open(textures / 'table_wood.png'), 86)
    felt = webp_uri(Image.open(textures / 'table_felt.png'), 92)

    page = (HERE / 'page.html').read_text()
    script = (HERE / 'mockup.js').read_text()

    art = ('const CARD_IMG=' + json.dumps(card_images()) + ';\n'
           'const AVATARS=' + json.dumps(avatar_images()) + ';\n'
           f'const WOOD_TEX="{wood}";\nconst FELT_TEX="{felt}";\n')

    body = page + '<script>\n' + art + '</script>\n<script>\n' + script + '</script>\n'

    if len(sys.argv) == 3 and sys.argv[1] == '--body-only':
        out = pathlib.Path(sys.argv[2])
        out.write_text(body)
    else:
        out = OUT
        out.write_text(HEAD + body)
    print(f'wrote {out} ({out.stat().st_size // 1024} KB)')


if __name__ == '__main__':
    main()
