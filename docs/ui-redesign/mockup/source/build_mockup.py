#!/usr/bin/env python3
"""Rebuild docs/ui-redesign/mockup/romanian_whist_table.html from its sources.

The built page is one self-contained file (open it in any browser, offline), so it
embeds the card art and the table textures as data URIs. Those are ~1 MB of base64
and make the page unreadable as source, so the readable parts live here instead:

    page.html   markup and CSS
    table.js    the mock-up's script (game flow, layout maths, rendering)

and this script stitches them together with the art, which it takes straight from
the client's own resources so the mock-up and the client can never use different
pictures:

    resources/images/cards/png/<rank>_of_<suit>.png    (ranks 3..ace: enough for 2-6 players)
    resources/images/table/table_wood.png, table_felt.png   (tools/generate_table_textures.py)

    python3 docs/ui-redesign/mockup/source/build_mockup.py

Needs Pillow (for the WebP re-encode that keeps the page small).
"""

import base64
import io
import json
import pathlib

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[3]
OUT = HERE.parent / 'romanian_whist_table.html'

RANKS = ['3', '4', '5', '6', '7', '8', '9', '10', 'jack', 'queen', 'king', 'ace']
SUITS = ['clubs', 'diamonds', 'hearts', 'spades']


def webp_uri(image, quality):
    buffer = io.BytesIO()
    image.save(buffer, 'WEBP', quality=quality, method=6)
    return 'data:image/webp;base64,' + base64.b64encode(buffer.getvalue()).decode()


def card_images():
    cards = {}
    folder = REPO / 'resources' / 'images' / 'cards' / 'png'
    for rank in RANKS:
        for suit in SUITS:
            name = f'{rank}_of_{suit}'
            image = Image.open(folder / f'{name}.png').convert('RGBA')
            image = image.resize((240, round(240 * image.height / image.width)), Image.LANCZOS)
            buffer = io.BytesIO()
            image.save(buffer, 'WEBP', quality=86)
            cards[name] = 'data:image/webp;base64,' + base64.b64encode(buffer.getvalue()).decode()
    return cards


def main():
    textures = REPO / 'resources' / 'images' / 'table'
    wood = webp_uri(Image.open(textures / 'table_wood.png'), 86)
    felt = webp_uri(Image.open(textures / 'table_felt.png'), 92)

    page = (HERE / 'page.html').read_text()
    script = (HERE / 'table.js').read_text()

    art = ('const CARD_IMG=' + json.dumps(card_images()) + ';\n'
           f'const WOOD_TEX="{wood}";\nconst FELT_TEX="{felt}";\n')

    OUT.write_text(page + '<script>\n' + art + '</script>\n<script>\n' + script + '</script>\n')
    print(f'wrote {OUT} ({OUT.stat().st_size // 1024} KB)')


if __name__ == '__main__':
    main()
