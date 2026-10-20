#!/usr/bin/env python3
"""Photograph the mock-up's hand with 1, 4 and 8 cards, for the redesign's phase 7.

None of the pictures in docs/ui-redesign/screenshots/ shows an 8-card hand, so this sets
the mock-up to the moment the client's screenshot tool draws (tests/ScreenshotTool.cpp,
"Screenshots of the hand at the mock-up's own scale") and photographs it in headless
Chrome:

    four players; Bogdan, on your left, has led the King of Hearts, and it is your turn;
    the same cards in the same order, those that follow suit lit and the rest dimmed;
    and in the 8-card shot the Queen of Hearts raised, as though the cursor were on it.

It writes hand-1-card.jpg, hand-4-cards.jpg and hand-8-cards.jpg into
docs/ui-redesign/screenshots/, at 1920x1080 like the other pictures there.

    python3 docs/ui-redesign/mockup/source/capture_hands.py

THE PAGE IS NOT CHANGED. Each shot is a copy of romanian_whist_table.html with three
things added:
- what its host would give it: a doctype, a charset, and two base rules (see STYLE);
- a style sheet with the bundled fonts and no transitions;
- a script that runs after the mock-up's own. It takes the 4-player table, stops the bots,
  sets the game to the shot's moment and draws it. The mock-up's own rules decide which cards are legal and
its own sort decides their order; the script checks both against the shot below.

NOTHING IS SAVED UNLESS THE PAGE SAYS IT SET THE SHOT UP. The script marks the page's
<body> only at the end of a run that passed every check, and each shot is loaded once
to read that mark before it is photographed. A check that fails, or a script that throws
- a table.js change that renames setPlayers(), say - draws a red banner saying why, and
this stops with the same words and leaves the committed picture alone.

THE FONTS ARE THE CLIENT'S OWN TTFs (resources/fonts/), as data URIs. The page loads its
fonts from Google Fonts, which needs a network and can change under us, and Chrome will
not load a font from a file:// URL into a file:// page.

Needs google-chrome or chromium on the PATH, and Pillow.
"""

import base64
import html
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parents[3]
PAGE = HERE.parent / 'romanian_whist_table.html'
OUT = HERE.parents[1] / 'screenshots'
FONTS = REPO / 'resources' / 'fonts'

# table.js's card: `r` indexes ALL_RANKS ('2' is 0 and the Ace 12), `s` names the suit.
RANKS = {'7': 5, '8': 6, '9': 7, '10': 8, 'J': 9, 'Q': 10, 'K': 11, 'A': 12}
SUITS = {'S': 'spades', 'H': 'hearts', 'C': 'clubs', 'D': 'diamonds'}

# THE SHOTS, KEPT IN STEP WITH tests/ScreenshotTool.cpp's handShots. Cards are written
# rank then suit ('QH' is the Queen of Hearts) and listed in the order the hand shows them:
# trump first, then table.js's alternating colours, high to low. `round` is the 0-based
# round of a 4-player game whose hand size is the shot's (scheduleFor(4)), and `bids` the
# four seats' bids, yours first.
LEAD = 'KH'

SHOTS = [
    {'file': 'hand-1-card.jpg', 'round': 0, 'trump': '7C',
     'hand': ['JD'], 'legal': ['JD'], 'raised': None, 'bids': [0, 1, 0, 0]},
    {'file': 'hand-4-cards.jpg', 'round': 6, 'trump': '7C',
     'hand': ['9C', 'AS', '10H', '8H'], 'legal': ['10H', '8H'], 'raised': None, 'bids': [1, 2, 0, 1]},
    {'file': 'hand-8-cards.jpg', 'round': 10, 'trump': None,
     'hand': ['AS', 'JS', '8S', 'QH', '9H', '7H', '10C', 'QD'], 'legal': ['QH', '9H', '7H'], 'raised': 'QH',
     'bids': [2, 3, 1, 1]},
]

# The bundled faces, under the family names page.html asks for.
FACES = [
    ('Alegreya Sans', 400, 'alegreya-sans/AlegreyaSans-Regular.ttf'),
    ('Alegreya Sans', 500, 'alegreya-sans/AlegreyaSans-Medium.ttf'),
    ('Alegreya Sans', 700, 'alegreya-sans/AlegreyaSans-Bold.ttf'),
    ('Cinzel Decorative', 700, 'cinzel-decorative/CinzelDecorative-Bold.ttf'),
    ('Marcellus', 400, 'marcellus/Marcellus-Regular.ttf'),
]

# The fonts; no transitions, so nothing is caught half way; and the two base rules the
# page's host supplies and the page itself leaves out. Without them the body keeps the
# browser's 8 px margin, and the bid strip and the modal's scrim, which set their own
# display, show although they are hidden.
STYLE = '''
<style>
%s
*, *::before, *::after { transition: none !important; animation: none !important; }
html, body { margin: 0; }
[hidden] { display: none !important; }
</style>
'''

# Runs after table.js, which has already dealt a 6-player demo game and is waiting on you.
# In a block, so its names cannot collide with table.js's globals.
SCRIPT = '''
<script>
{
const fail = text =>
{
  document.body.dataset.capture = "failed";

  const banner = document.createElement("div");
  banner.id = "capture-failed";
  banner.textContent = "capture_hands.py: " + text;
  banner.style.cssText = "position:fixed;left:0;right:0;top:40%%;z-index:99;padding:24px;" +
                         "background:#c00;color:#fff;font:bold 32px sans-serif;text-align:center";
  document.body.appendChild(banner);
};

document.fonts.ready.then(() =>
{
  const SHOT = %s;

  setPlayers(4);
  token++;   // every bot loop checks the token, so none moves again

  const backs = count => Array.from({ length: count }, () => ({ r: 0, s: "spades" }));

  G.roundIdx = SHOT.round;
  G.handSize = G.schedule[G.roundIdx];
  G.trumpCard = SHOT.trump;
  G.trump = SHOT.trump ? SHOT.trump.s : null;
  G.hands = [SHOT.hand, backs(G.handSize - 1), backs(G.handSize), backs(G.handSize)];
  G.bids = SHOT.bids.slice();
  G.won = [0, 0, 0, 0];
  G.totals = [0, 0, 0, 0];
  G.first = 1;
  G.leader = 1;
  G.turn = 0;
  G.phase = "play";
  G.trick = [{ seat: 1, card: SHOT.lead }];
  G.trickNo = 0;
  G.lastTrick = null;
  busy = false;

  if (G.handSize !== SHOT.hand.length)
    return fail(`round ${SHOT.round} deals ${G.handSize} cards, not ${SHOT.hand.length}`);

  // renderHand()'s own sort, which must leave the shot's order as it is.
  const order = suitOrder(G.trump);
  const sorted = SHOT.hand.slice().sort((a, b) => order.indexOf(a.s) - order.indexOf(b.s) || b.r - a.r);
  if (sorted.some((c, i) => c !== SHOT.hand[i]))
    return fail("the hand is not in the mock-up's order");

  const legalNow = legal(G, 0);
  if (legalNow.length !== SHOT.legal.length || SHOT.legal.some(i => !legalNow.includes(SHOT.hand[i])))
    return fail("the mock-up's rules make other cards legal");

  render();

  if (SHOT.raised !== null)
  {
    // The hover rule, copied onto a class: headless Chrome has no cursor to hover with.
    for (const sheet of document.styleSheets)
      for (const rule of sheet.cssRules)
        if (rule.selectorText && rule.selectorText.includes(".hcard.playable:hover .face"))
          sheet.insertRule(`.hcard.playable.raised .face { ${rule.style.cssText} }`, sheet.cssRules.length);

    document.querySelectorAll("#hand .hcard")[SHOT.raised].classList.add("raised");
  }

  // Every check passed and the shot is drawn: the one mark check() accepts.
  document.body.dataset.capture = "ok";
}).catch(error => fail("the script threw " + error));
}
</script>
'''


def card(name):
    return {'r': RANKS[name[:-1]], 's': SUITS[name[-1]]}


def font_faces():
    rules = []

    for family, weight, path in FACES:
        data = base64.b64encode((FONTS / path).read_bytes()).decode('ascii')
        rules.append(f"@font-face {{ font-family: '{family}'; font-weight: {weight}; font-style: normal; "
                     f"src: url(data:font/ttf;base64,{data}) format('truetype'); }}")

    return '\n'.join(rules)


def page_for(shot, base):
    hand = [card(name) for name in shot['hand']]

    # The script compares cards by identity, as table.js does, so the legal set and the
    # raised card are positions in the hand rather than cards of their own.
    state = {
        'round': shot['round'],
        'trump': card(shot['trump']) if shot['trump'] else None,
        'hand': hand,
        'legal': [shot['hand'].index(name) for name in shot['legal']],
        'raised': shot['hand'].index(shot['raised']) if shot['raised'] else None,
        'bids': shot['bids'],
        'lead': card(LEAD),
    }

    script = SCRIPT % json.dumps(state)

    head_end = base.index('</head>') if '</head>' in base else base.index('<style>')
    page = base[:head_end] + (STYLE % font_faces()) + base[head_end:]

    body_end = page.rindex('</body>') if '</body>' in page else len(page)
    page = page[:body_end] + script + page[body_end:]

    # THE BUILT PAGE STARTS AT ITS <title>, with no doctype and no charset: whatever
    # hosts it supplies both. Opened bare, Chrome would draw it in quirks mode and
    # read its UTF-8 as Latin-1, so every suit glyph would come out as three letters.
    if not page.lstrip().lower().startswith('<!doctype'):
        page = '<!doctype html>\n<meta charset="utf-8">\n' + page

    return page


def without_web_fonts(html):
    # Drop the Google Fonts links, so the bundled faces are the only ones in play.
    lines = [line for line in html.split('\n')
             if 'fonts.googleapis.com' not in line and 'fonts.gstatic.com' not in line]
    return '\n'.join(lines)


def browser():
    for name in ('google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'):
        path = shutil.which(name)
        if path:
            return path

    sys.exit('capture_hands.py needs google-chrome or chromium on the PATH')


def run_chrome(chrome, page, profile, output):
    # The page loaded once in headless Chrome at the pictures' size, `output` saying what
    # to hand back. Returns what Chrome wrote to stdout.
    command = [chrome, '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
               '--no-default-browser-check', '--force-device-scale-factor=1', '--window-size=1920,1080',
               f'--user-data-dir={profile}', '--virtual-time-budget=5000', output, page.as_uri()]

    # At the lowest priority: the machine this runs on is somebody's desktop.
    if shutil.which('nice'):
        command = ['nice', '-n', '19'] + command

    return subprocess.run(command, check=True, capture_output=True, text=True).stdout


def check(chrome, page, profile, name):
    # THE PAGE'S OWN WORD that the shot is set up, read from its DOM: the mark the script
    # puts on <body> once every check has passed. Without it a check failed, the script
    # threw, or it never ran at all - and Chrome photographs any of those without a word.
    dom = run_chrome(chrome, page, profile, '--dump-dom')

    body = re.search(r'<body[^>]*>', dom)
    if body and 'data-capture="ok"' in body.group(0):
        return

    banner = re.search(r'<div id="capture-failed"[^>]*>capture_hands\.py: ([^<]*)</div>', dom)
    reason = html.unescape(banner.group(1)) if banner else 'the page never finished setting up'

    sys.exit(f"{name}: {reason}; nothing saved")


def capture(chrome, page, png, profile):
    run_chrome(chrome, page, profile, f'--screenshot={png}')


def main():
    chrome = browser()
    base = without_web_fonts(PAGE.read_text(encoding='utf-8'))

    with tempfile.TemporaryDirectory(prefix='capture_hands_') as scratch:
        scratch = pathlib.Path(scratch)

        for shot in SHOTS:
            page = scratch / 'page.html'
            png = scratch / 'shot.png'
            page.write_text(page_for(shot, base), encoding='utf-8')

            check(chrome, page, scratch / 'profile', shot['file'])
            capture(chrome, page, png, scratch / 'profile')

            image = Image.open(png).convert('RGB')
            if image.size != (1920, 1080):
                sys.exit(f"{shot['file']}: Chrome drew {image.size[0]}x{image.size[1]}, not 1920x1080")

            target = OUT / shot['file']
            image.save(target, 'JPEG', quality=90)
            print(f'{target.relative_to(REPO)}')


if __name__ == '__main__':
    main()
