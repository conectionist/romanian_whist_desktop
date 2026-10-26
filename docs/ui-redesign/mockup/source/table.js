"use strict";

// ---------- rules (docs/RULES.md) ----------
const SUITS = ["spades", "hearts", "clubs", "diamonds"];   // alternating colours, used for hand order
const ALL_RANKS = ["2", "3", "4", "5", "6", "7", "8", "9", "10", "jack", "queen", "king", "ace"];
const ACE = 12, KING = 11, QUEEN = 10;
const RANK_LABEL = { "2": "2", "3": "3", "4": "4", "5": "5", "6": "6", "7": "7", "8": "8", "9": "9", "10": "10", jack: "J", queen: "Q", king: "K", ace: "A" };
const RANK_NAME = { "3": "3", "5": "5", "7": "7", "9": "9", jack: "Jack" };
const SYM = { spades: "♠", hearts: "♥", clubs: "♣", diamonds: "♦" };
const SUIT_NAME = { spades: "Spades", hearts: "Hearts", clubs: "Clubs", diamonds: "Diamonds" };

const isRed = s => s === "hearts" || s === "diamonds";
const cardId = c => `${ALL_RANKS[c.r]}_of_${c.s}`;
const rankLabel = c => RANK_LABEL[ALL_RANKS[c.r]];
const suitSpan = s => `<span class="${isRed(s) ? "red" : "blk"}">${SYM[s]}</span>`;
const cardLabel = c => `${rankLabel(c)}${suitSpan(c.s)}`;
const cardName = c => `${rankLabel(c)} of ${SUIT_NAME[c.s]}`;

// rule 2: two ranks per player, from the Ace down, so the deck is always 8 cards per player
const lowestRank = n => 13 - 2 * n;
// rule 3, the 1-8-1 schedule: the 1-card and 8-card rounds repeat once per player
function scheduleFor(n)
{
  const ones = Array(n).fill(1), eights = Array(n).fill(8);
  return [...ones, 2, 3, 4, 5, 6, 7, ...eights, 7, 6, 5, 4, 3, 2, ...ones];
}

let rand = Math.random;
function rng32(a)
{
  return function ()
  {
    a |= 0; a = a + 0x6D2B79F5 | 0;
    let t = Math.imul(a ^ a >>> 15, 1 | a);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}
function shuffle(arr)
{
  for (let i = arr.length - 1; i > 0; i--)
  {
    const j = Math.floor(rand() * (i + 1));
    [arr[i], arr[j]] = [arr[j], arr[i]];
  }
  return arr;
}
function newDeck(n)
{
  const d = [];
  for (const s of SUITS) for (let r = lowestRank(n); r <= ACE; r++) d.push({ r, s });
  return d;
}

function newGame(n)
{
  return { n, schedule: scheduleFor(n), roundIdx: 0, totals: Array(n).fill(0), streak: Array(n).fill(0), history: [] };
}
const seatsOf = G => [...Array(G.n).keys()];

function dealRound(G)
{
  const n = G.schedule[G.roundIdx];
  const deck = shuffle(newDeck(G.n));
  G.handSize = n;
  G.hands = seatsOf(G).map(i => deck.slice(i * n, (i + 1) * n));
  G.trumpCard = n < 8 ? deck[G.n * n] : null;     // rule 4: the 8-card round has no trump
  G.trump = G.trumpCard ? G.trumpCard.s : null;
  G.first = G.roundIdx % G.n;
  G.bids = Array(G.n).fill(null);
  G.won = Array(G.n).fill(0);
  G.phase = "bid";
  G.turn = G.first;
  G.leader = G.first;
  G.trick = [];
  G.trickNo = 0;
  G.lastTrick = null;
  G.scored = false;
}

// rule 5: only the final bidder is restricted, and only while a bid could still hit the total
function forbiddenBid(G)
{
  const placed = G.bids.filter(b => b !== null);
  if (placed.length !== G.n - 1) return null;
  const f = G.handSize - placed.reduce((a, b) => a + b, 0);
  return f >= 0 && f <= G.handSize ? f : null;
}
function placeBid(G, seat, b)
{
  G.bids[seat] = b;
  G.turn = (seat + 1) % G.n;
  if (G.bids.every(x => x !== null))
  {
    G.phase = "play";
    G.turn = G.leader = G.first;
  }
}
// rule 6: follow suit, else trump, else anything; overtrumping not required
function legal(G, seat)
{
  const h = G.hands[seat];
  if (!G.trick.length) return h.slice();
  const lead = G.trick[0].card.s;
  const follow = h.filter(c => c.s === lead);
  if (follow.length) return follow;
  if (G.trump)
  {
    const t = h.filter(c => c.s === G.trump);
    if (t.length) return t;
  }
  return h.slice();
}
// rule 7
const beats = (a, b, trump) => a.s === b.s ? a.r > b.r : a.s === trump;
function winningIdx(trick, trump)
{
  let w = 0;
  for (let i = 1; i < trick.length; i++) if (beats(trick[i].card, trick[w].card, trump)) w = i;
  return w;
}
function playCard(G, seat, card)
{
  G.hands[seat] = G.hands[seat].filter(c => c !== card);
  G.trick.push({ seat, card });
  G.turn = (seat + 1) % G.n;
  return G.trick.length === G.n;
}
function resolveTrick(G)
{
  const w = G.trick[winningIdx(G.trick, G.trump)].seat;
  G.won[w]++;
  G.lastTrick = { cards: G.trick, winner: w };
  G.trick = [];
  G.trickNo++;
  G.leader = G.turn = w;
  if (G.hands[0].length === 0) G.phase = "end";
  return w;
}
// rule 8
function scoreRound(G)
{
  const rows = seatsOf(G).map(i =>
  {
    const exact = G.bids[i] === G.won[i];
    const base = exact ? 5 + G.bids[i] : -Math.abs(G.bids[i] - G.won[i]);
    let bonus = 0;
    if (G.handSize > 1)
    {
      if (exact)
      {
        G.streak[i] = G.streak[i] > 0 ? G.streak[i] + 1 : 1;
        if (G.streak[i] === 5) { bonus = 10; G.streak[i] = 0; }
      }
      else
      {
        G.streak[i] = G.streak[i] < 0 ? G.streak[i] - 1 : -1;
        if (G.streak[i] === -5) { bonus = -10; G.streak[i] = 0; }
      }
    }
    G.totals[i] += base + bonus;
    return { base, bonus };
  });
  G.history.push({
    round: G.roundIdx + 1, handSize: G.handSize, trumpCard: G.trumpCard,
    bids: G.bids.slice(), won: G.won.slice(), rows, totals: G.totals.slice()
  });
  G.scored = true;
}

// ---------- simple bots (stand-ins for the engine's Reckoner) ----------
function botBid(G, seat)
{
  const h = G.hands[seat];
  const tCount = G.trump ? h.filter(c => c.s === G.trump).length : 0;
  let est = 0;
  for (const c of h)
  {
    if (G.trump && c.s === G.trump) est += c.r >= QUEEN ? 0.9 : (tCount >= 3 ? 0.5 : 0.25);
    else if (c.r === ACE) est += 0.85;
    else if (c.r === KING) est += h.filter(x => x.s === c.s).length <= 3 ? 0.45 : 0.2;
  }
  let b = Math.max(0, Math.min(G.handSize, Math.round(est)));
  const f = forbiddenBid(G);
  if (b === f) b = (b > 0 && (est < b || b === G.handSize)) ? b - 1 : b + 1;
  return b;
}
function botPlay(G, seat)
{
  const val = c => c.r + (c.s === G.trump ? 20 : 0);
  const opts = legal(G, seat).sort((a, b) => val(a) - val(b));
  const need = G.bids[seat] - G.won[seat];
  if (!G.trick.length) return need > 0 ? opts[opts.length - 1] : opts[0];
  const best = G.trick[winningIdx(G.trick, G.trump)].card;
  const winners = opts.filter(c => beats(c, best, G.trump));
  if (need > 0)
  {
    if (!winners.length) return opts[0];
    return G.trick.length === G.n - 1 ? winners[0] : winners[winners.length - 1];
  }
  const losers = opts.filter(c => !winners.includes(c));
  return losers.length ? losers[losers.length - 1] : opts[0];
}
function autoRound(G)
{
  while (G.phase === "bid") placeBid(G, G.turn, botBid(G, G.turn));
  while (G.phase === "play") if (playCard(G, G.turn, botPlay(G, G.turn))) resolveTrick(G);
  scoreRound(G);
}

// A believable mid-game opening: the rounds up to the first 6-card round already scored,
// third trick of that round, and you play last with some of your hand barred by the follow rule.
function demoGame(n)
{
  const target = n + 4;
  for (let seed = 11; seed < 4000; seed++)
  {
    rand = rng32(seed);
    const g = newGame(n);
    for (let r = 0; r < target; r++) { dealRound(g); autoRound(g); g.roundIdx++; }
    dealRound(g);
    if (!g.trump) continue;
    while (g.phase === "bid") placeBid(g, g.turn, botBid(g, g.turn));
    let ok = false;
    while (g.phase === "play")
    {
      if (g.turn === 0 && g.trickNo === 2 && g.trick.length === n - 1)
      {
        const L = legal(g, 0);
        ok = L.length >= 2 && L.length < g.hands[0].length && g.won.some(w => w > 0)
          && g.trick[winningIdx(g.trick, g.trump)].seat !== g.leader;
        break;
      }
      if (playCard(g, g.turn, botPlay(g, g.turn))) resolveTrick(g);
    }
    if (ok) { rand = Math.random; return g; }
  }
  rand = Math.random;
  const g = newGame(n); dealRound(g); return g;
}

// ---------- art ----------
const PLAYERS = [
  { name: "You", ai: false, look: { id: 0, bg: "#6b5233", skin: "#d9ab86", shade: "#b98a68", hair: "#2a1c14", coat: "#3a3530", shirt: "#d8cfbf", beard: "#3a2a1e" } },
  { name: "Bogdan", ai: true, look: { id: 1, bg: "#5a4a35", skin: "#cf9f7c", shade: "#a87c5e", hair: "#3b2a1c", coat: "#4a3524", shirt: "#bfb39e", hat: "#3d3a36", mustache: "#3b2a1c" } },
  { name: "Ana", ai: true, look: { id: 2, bg: "#6e5a3e", skin: "#e2b997", shade: "#c09576", hair: "#4a2f1d", coat: "#4b3324", shirt: "#e8e0cf", longHair: true } },
  { name: "Costel", ai: true, look: { id: 3, bg: "#5f5440", skin: "#d5a888", shade: "#ae8466", hair: "#d8d2c6", coat: "#3c3a32", shirt: "#cfc6b2", hat: "#6a5134", mustache: "#ece6da" } },
  { name: "Dana", ai: true, look: { id: 4, bg: "#5c4636", skin: "#e8c2a2", shade: "#c79e80", hair: "#8a3f22", coat: "#2f3b34", shirt: "#e4dccb", longHair: true } },
  { name: "Emil", ai: true, look: { id: 5, bg: "#4f4a3c", skin: "#c99877", shade: "#a3765a", hair: "#1f1712", coat: "#5a4a32", shirt: "#d2c8b4", beard: "#1f1712" } }
];
function portrait(p)
{
  const g = `pg${p.id}`;
  return `<svg viewBox="0 0 100 114" aria-hidden="true">
    <defs><radialGradient id="${g}" cx="50%" cy="36%" r="72%"><stop offset="0" stop-color="${p.bg}"/><stop offset="1" stop-color="#17100a"/></radialGradient></defs>
    <rect width="100" height="114" fill="url(#${g})"/>
    ${p.longHair ? `<path d="M29 46 Q24 84 32 100 L68 100 Q76 84 71 46 Z" fill="${p.hair}"/>` : ""}
    <path d="M6 114 Q10 82 34 77 L66 77 Q90 82 94 114 Z" fill="${p.coat}"/>
    <path d="M39 77 L50 94 L61 77 Z" fill="${p.shirt}"/>
    <rect x="43" y="62" width="14" height="17" rx="5" fill="${p.shade}"/>
    <ellipse cx="50" cy="48" rx="16" ry="19.5" fill="${p.skin}"/>
    <ellipse cx="33.5" cy="50" rx="2.6" ry="4.4" fill="${p.shade}"/><ellipse cx="66.5" cy="50" rx="2.6" ry="4.4" fill="${p.shade}"/>
    <path d="M34 44 Q33 26 50 25 Q67 26 66 44 Q62 34 50 33.5 Q38 34 34 44Z" fill="${p.hair}"/>
    ${p.hat ? `<path d="M32 38 Q33 22 50 21 Q68 22 69 37 Q50 32 32 38Z" fill="${p.hat}"/><path d="M30 39 Q50 31 74 37 L72 42 Q50 36 31 42Z" fill="${p.hat}" opacity=".8"/>` : ""}
    <path d="M41 46.5 Q44 44.8 47 46.5 M53 46.5 Q56 44.8 59 46.5" stroke="#2a1a10" stroke-width="1.6" fill="none" stroke-linecap="round"/>
    <path d="M48 52 Q50 56 52 52" stroke="${p.shade}" stroke-width="1.4" fill="none"/>
    ${p.mustache ? `<path d="M41 58 Q50 53 59 58 Q55 61 50 59 Q45 61 41 58Z" fill="${p.mustache}"/>` : `<path d="M45 60 Q50 62.5 55 60" stroke="#8a4f3c" stroke-width="1.5" fill="none" stroke-linecap="round"/>`}
    ${p.beard ? `<path d="M36 54 Q38 70 50 70 Q62 70 64 54 Q60 64 50 64 Q40 64 36 54Z" fill="${p.beard}" opacity=".35"/>` : ""}
  </svg>`;
}
function backSvg()
{
  const w = 100, h = 145, cx = 50, cy = 72.5;
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w} ${h}">
    <defs><pattern id="p" width="9" height="9" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
      <rect width="9" height="9" fill="#7a1c1c"/><path d="M0 4.5h9M4.5 0v9" stroke="#d2ac63" stroke-width=".5" opacity=".55"/><circle cx="4.5" cy="4.5" r="1.4" fill="#a8392b"/></pattern></defs>
    <rect width="${w}" height="${h}" rx="7" fill="#f2e8d2"/>
    <rect x="4.5" y="4.5" width="${w - 9}" height="${h - 9}" rx="4.5" fill="#7a1c1c"/>
    <rect x="8" y="8" width="${w - 16}" height="${h - 16}" rx="3" fill="url(#p)" stroke="#d8b56a" stroke-width="1"/>
    <ellipse cx="${cx}" cy="${cy}" rx="15" ry="21" fill="#7a1c1c" stroke="#d8b56a" stroke-width="1.2"/>
    <path d="M${cx} ${cy - 10} L${cx + 6} ${cy} L${cx} ${cy + 10} L${cx - 6} ${cy} Z" fill="#d8b56a"/></svg>`;
  return `url("data:image/svg+xml,${encodeURIComponent(svg)}")`;
}
const NOISE = `url("data:image/svg+xml,${encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="220" height="220"><filter id="n"><feTurbulence type="fractalNoise" baseFrequency=".9" numOctaves="3" stitchTiles="stitch"/></filter><rect width="220" height="220" filter="url(#n)"/></svg>')}")`;
const GRAIN = `url("data:image/svg+xml,${encodeURIComponent('<svg xmlns="http://www.w3.org/2000/svg" width="600" height="300"><filter id="g"><feTurbulence type="fractalNoise" baseFrequency=".004 .09" numOctaves="4" seed="3"/></filter><rect width="600" height="300" filter="url(#g)"/></svg>')}")`;
document.documentElement.style.setProperty("--noise", NOISE);
document.documentElement.style.setProperty("--grain", GRAIN);
document.documentElement.style.setProperty("--back-p", backSvg());
// wood and felt are generated textures (tangential grain round the rim, fuzzy cloth), embedded as images
document.documentElement.style.setProperty("--wood-tex", `url("${WOOD_TEX}")`);
document.documentElement.style.setProperty("--felt-tex", `url("${FELT_TEX}")`);

// ---------- stage scaling ----------
const viewport = document.getElementById("viewport");
const stage = document.getElementById("stage");
let scale = 1;
function fit()
{
  const cs = getComputedStyle(viewport);
  const pl = parseFloat(cs.paddingLeft), pr = parseFloat(cs.paddingRight);
  const pt = parseFloat(cs.paddingTop), pb = parseFloat(cs.paddingBottom);
  const w = viewport.clientWidth - pl - pr, h = viewport.clientHeight - pt - pb;
  // Fit a box wider and taller than the 1536x1024 stage, so the seats outside the table always have
  // room round it (the top seat above it, yours below), then centre the stage in that box.
  const VY0 = -150, VH = 1324;
  scale = Math.min(w / 1880, h / VH);
  const x = pl + (w - 1536 * scale) / 2, y = pt + (h - VH * scale) / 2 - VY0 * scale;
  stage.style.transform = `translate(${x}px, ${y}px) scale(${scale})`;
  // The scoreboard and the trick panel hug the menu bar and the window's own corners, 8px in,
  // whatever space the window leaves around the table.
  const bar = document.querySelector(".menubar").offsetHeight;
  const top = (bar + 8 - y) / scale;
  const score = document.getElementById("score"), info = document.getElementById("trickinfo");
  score.style.top = info.style.top = `${top}px`;
  score.style.left = `${(8 - x) / scale}px`;
  info.style.right = `${1536 - (viewport.clientWidth - 8 - x) / scale}px`;
  // Round history and Last trick sit in the window's bottom-left corner, the same 8px in.
  const hist = document.getElementById("btn-history"), last = document.getElementById("btn-last");
  const left = (8 - x) / scale, bottom = (viewport.clientHeight - 8 - y) / scale;
  hist.style.left = `${left}px`;
  last.style.left = `${left + 228}px`;
  hist.style.top = last.style.top = `${bottom - hist.offsetHeight}px`;
  // the mock-up label sits in the window's bottom-right corner
  const tag = document.getElementById("tag");
  tag.style.bottom = `${1024 - bottom}px`;
  tag.style.left = `${(viewport.clientWidth - 8 - x) / scale}px`;
  tag.style.transform = "translateX(-100%)";
}
addEventListener("resize", fit);
fit();
function toStage(rect)
{
  const s = stage.getBoundingClientRect();
  return { x: (rect.left - s.left) / scale, y: (rect.top - s.top) / scale, w: rect.width / scale, h: rect.height / scale };
}

// ---------- geometry ----------
// Seats go clockwise from you at the bottom, at 90 + i * 360/n degrees round the table (plan section 14).
// Name plaques stay upright and readable. Side and diagonal seats sit just outside the table's edge; the top
// seat and yours overlap the rim, as the table fills the window's height. The spots are picked per player
// count so they also clear the corner panels.
// Cards are what turn: each face-down fan lies on the rim and opens towards the centre, and a played card
// points at its owner.
const TABLE_X = 768, TABLE_Y = 512, RIM_R = 452, LINE_R = 416;   // LINE_R: the felt's thin gold circle
const PLAQUE_W = 280, PLAQUE_H = 118;
const LAYOUTS = {
  3: [null, [96, 49], [1160, 49]],
  4: [null, [-34, 453], [628, -140], [1290, 453]],
  5: [null, [-24, 611], [96, 49], [1160, 49], [1280, 611]],
  6: [null, [1, 700], [96, 49], [628, -140], [1160, 49], [1255, 700]]
};
const seatDeg = (G, s) => 90 + s * 360 / G.n;
const seatAngle = (G, s) => seatDeg(G, s) * Math.PI / 180;
const trickSize = G => G.n <= 4 ? { w: 141.6, h: 205.2 } : { w: 124.8, h: 181.2 };
// Each played card lands squarely in front of its owner's rack, on the line from the rack to the table centre,
// and turned to match the rack. Yours lands just in front of your hand.
const rackAngle = [];
function trickPos(G, s)
{
  const { w, h } = trickSize(G);
  if (s === 0) return [768 - w / 2, 850 - 60 - h, 0];
  const a = rackAngle[s] ?? seatAngle(G, s);
  const r = 440 - 60 - h / 2;   // felt edge, a small gap, then half the card
  const cx = TABLE_X + r * Math.cos(a), cy = TABLE_Y + r * Math.sin(a);
  return [cx - w / 2, cy - h / 2, a * 180 / Math.PI - 90];
}
function plaqueCenter(s)
{
  if (s === 0) return [768, 1105];
  const [x, y] = LAYOUTS[G.n][s];
  return [x + PLAQUE_W / 2, y + PLAQUE_H / 2];
}
const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)").matches;

// ---------- rendering ----------
const $ = id => document.getElementById(id);
let G = null;

function buildSeats()
{
  const host = $("seats");
  host.innerHTML = "";
  // fans first, so each plaque sits over the base of its own fan
  for (let s = 1; s < G.n; s++)
  {
    // The fan lies on the rim in line with its plaque. Where the plaque itself covers that bit of rim
    // (the top seat), it slides clockwise along the rim until it is clear.
    const [cx, cy] = plaqueCenter(s);
    const [px, py] = LAYOUTS[G.n][s];
    const at = a => [TABLE_X + RIM_R * Math.cos(a), TABLE_Y + RIM_R * Math.sin(a)];
    const covered = a =>
    {
      const [fx, fy] = at(a), m = 10;
      return fx > px - m && fx < px + PLAQUE_W + m && fy > py - m && fy < py + PLAQUE_H + m;
    };
    let a = Math.atan2(cy - TABLE_Y, cx - TABLE_X);
    if (covered(a))
    {
      for (let k = 0; k < 90 && covered(a); k++) a += Math.PI / 180;
      a += 9 * Math.PI / 180;   // and clear the fan's own spread past the plaque's corner
    }
    const [fx, fy] = at(a);
    rackAngle[s] = a;
    const fan = document.createElement("div");
    fan.className = "fan";
    fan.id = `fan-${s}`;
    fan.style.left = `${fx}px`;
    fan.style.top = `${fy}px`;
    fan.style.transform = `rotate(${Math.atan2(-Math.cos(a), Math.sin(a)) * 180 / Math.PI}deg)`;
    host.appendChild(fan);
  }
  for (let s = 1; s < G.n; s++)
  {
    const [x, y] = LAYOUTS[G.n][s];
    const p = document.createElement("div");
    p.className = "panel plaque seat";
    p.id = `seat-${s}`;
    p.style.left = `${x}px`;
    p.style.top = `${y}px`;
    host.appendChild(p);
  }
  document.querySelectorAll(".players button").forEach(b => b.setAttribute("aria-pressed", +b.dataset.n === G.n));
}
function pillFor(seat)
{
  const b = G.bids[seat], w = G.won[seat];
  if (b === null) return `<span class="pill">${G.phase === "bid" && G.turn === seat ? "Bidding…" : "No bid yet"}</span>`;
  let cls = "", note = "";
  if (G.phase !== "bid")
  {
    if (w === b) { cls = "on"; note = "on target"; }
    else if (w < b) { cls = "owing"; note = `needs ${b - w}`; }
    else { cls = "over"; note = `over by ${w - b}`; }
  }
  return `<span class="pill ${cls}" title="${note}">Taken/Bid: ${w}/${b}</span>`;
}
function renderPlaques()
{
  for (let s = 0; s < G.n; s++)
  {
    const p = PLAYERS[s];
    const el = $(`seat-${s}`);
    const keep = el.querySelector(".bubble");
    el.innerHTML = `<div class="portrait">${portrait(p.look)}</div>
      <div><div class="name">${p.name} <span class="pts">(${G.totals[s]})</span></div>
      <div class="stats">${pillFor(s)}</div></div>`;
    if (keep) el.appendChild(keep);
    el.classList.toggle("active", (G.phase === "bid" || G.phase === "play") && G.turn === s && !(G.phase === "play" && G.trick.length === G.n));
  }
}
function renderFans()
{
  for (let s = 1; s < G.n; s++)
  {
    const n = G.hands[s].length, mid = (n - 1) / 2;
    let html = "";
    for (let i = 0; i < n; i++) html += `<div class="back" style="transform:translateX(${(i - mid) * 26}px) rotate(${(i - mid) * -2}deg) rotateX(50deg)"></div>`;
    const w = Math.max(0, n - 1) * 26 + 78;
    if (n) html += `<div class="ledge" style="left:${-w / 2}px;width:${w}px"></div>`;
    $(`fan-${s}`).innerHTML = html;
  }
}
function suitOrder(trump)
{
  const start = trump ? SUITS.indexOf(trump) : 0;
  return [0, 1, 2, 3].map(i => SUITS[(start + i) % 4]);
}
function renderHand()
{
  const order = suitOrder(G.trump);
  const cards = G.hands[0].slice().sort((a, b) => order.indexOf(a.s) - order.indexOf(b.s) || b.r - a.r);
  const myTurn = G.phase === "play" && G.turn === 0 && G.trick.length < G.n && !busy;
  const L = myTurn ? legal(G, 0) : [];
  const n = cards.length, CW = 154.9;
  const step = n > 1 ? Math.min(84, (720 - CW) / (n - 1)) : 0;
  const total = (n - 1) * step + CW;
  const x0 = 768 - total / 2, mid = (n - 1) / 2;
  const hand = $("hand");
  hand.innerHTML = "";
  cards.forEach((c, i) =>
  {
    const b = document.createElement("button");
    const ok = L.includes(c);
    b.className = "hcard" + (myTurn ? (ok ? " playable" : " illegal") : "");
    b.style.setProperty("--x", `${x0 + i * step}px`);
    b.style.setProperty("--y", `${850 + Math.pow(i - mid, 2) * 1.6}px`);
    b.style.setProperty("--rot", `${(i - mid) * 1.9}deg`);
    b.style.zIndex = i;
    b.tabIndex = myTurn && ok ? 0 : -1;
    b.setAttribute("aria-label", `${cardName(c)}${myTurn && !ok ? ", not playable" : ""}`);
    b.innerHTML = `<div class="face"><img alt="" src="${CARD_IMG[cardId(c)]}"></div>`;
    b.addEventListener("click", () => humanPlay(c, b));
    hand.appendChild(b);
  });
}
function renderTrick()
{
  const layer = $("trick");
  layer.innerHTML = "";
  const w = G.trick.length >= 2 ? winningIdx(G.trick, G.trump) : -1;
  const size = trickSize(G);
  G.trick.forEach((t, i) =>
  {
    const [x, y, rot] = trickPos(G, t.seat);
    const el = document.createElement("div");
    el.className = "tcard" + (i === w ? " winning" : "");
    el.dataset.id = cardId(t.card);
    el.style.left = `${x}px`;
    el.style.top = `${y}px`;
    el.style.width = `${size.w}px`;
    el.style.height = `${size.h}px`;
    el.style.setProperty("--rot", `${rot}deg`);
    el.innerHTML = `<div class="inner"><img alt="${cardName(t.card)}" src="${CARD_IMG[cardId(t.card)]}"></div>`;
    layer.appendChild(el);
  });
  // The leader's mark sits on the felt's gold circle, on the line from the leader's rack to the centre.
  // Your hand covers that circle at the bottom, so yours sits in the gap between your card and your hand.
  if (G.phase === "play")
  {
    const pip = document.createElement("div");
    pip.className = "leadpip";
    pip.title = `${PLAYERS[G.leader].name} leads`;
    let px, py, deg;
    if (G.leader === 0)
    {
      px = 768; py = 850 - 30; deg = 0;
    }
    else
    {
      const a = rackAngle[G.leader] ?? seatAngle(G, G.leader);
      px = TABLE_X + LINE_R * Math.cos(a); py = TABLE_Y + LINE_R * Math.sin(a); deg = a * 180 / Math.PI - 90;
    }
    pip.style.left = `${px}px`;
    pip.style.top = `${py}px`;
    pip.style.transform = `rotate(${deg}deg) scaleY(1.45) rotate(45deg)`;   // a rhombus, long axis pointing at the leader
    layer.appendChild(pip);
  }
}
function renderScore()
{
  const order = seatsOf(G).map(i => (G.first + i) % G.n);   // rows in bid order
  const rows = order.map(s => `<tr class="${(G.phase === "bid" || G.phase === "play") && G.turn === s ? "turn" : ""}">
      <td>${PLAYERS[s].name}</td><td>${G.bids[s] ?? "–"}</td><td>${G.phase === "bid" ? "–" : G.won[s]}</td><td>${G.totals[s]}</td></tr>`).join("");
  const placed = G.bids.filter(b => b !== null);
  const sum = placed.reduce((a, b) => a + b, 0);
  let tension;
  if (placed.length < G.n) tension = `Bids so far: <b>${sum}</b> of ${G.handSize} tricks`;
  else if (sum > G.handSize) tension = `Bids total <b>${sum}</b> for ${G.handSize} tricks: over by ${sum - G.handSize}`;
  else tension = `Bids total <b>${sum}</b> for ${G.handSize} tricks: under by ${G.handSize - sum}`;
  const trump = G.trumpCard
    ? `<div class="label">Trump</div><img alt="${cardName(G.trumpCard)}" src="${CARD_IMG[cardId(G.trumpCard)]}"><div class="name">${suitSpan(G.trump)} ${SUIT_NAME[G.trump]}</div>`
    : `<div class="label">Trump</div><div class="name">None this round</div>`;
  $("score").innerHTML = `
    <div class="head"><span class="round">Round ${G.roundIdx + 1} of ${G.schedule.length}</span></div>
    <div class="sub">${G.handSize} card${G.handSize > 1 ? "s" : ""} dealt &middot; Normal</div>
    <div class="tension">${tension}</div>
    <table><thead><tr><th>Player</th><th>Bid</th><th>Taken</th><th>Pts</th></tr></thead><tbody>${rows}</tbody></table>
    <div class="divider"></div>
    <div class="trump">${trump}</div>`;
}
function renderTrickInfo()
{
  const el = $("trickinfo");
  if (G.phase === "bid")
  {
    el.innerHTML = `<div class="title">Bidding</div>
      <div class="row">First to bid: <b>${PLAYERS[G.first].name}</b></div>
      <div class="prompt">${G.turn === 0 ? "Your bid. How many tricks will you take?" : `${PLAYERS[G.turn].name} is thinking…`}</div>`;
    return;
  }
  if (G.phase === "end")
  {
    el.innerHTML = `<div class="title">Round over</div><div class="prompt">Scores are in.</div>`;
    return;
  }
  const t = G.trick;
  let prompt;
  if (t.length === G.n)
  {
    const ws = t[winningIdx(t, G.trump)].seat;
    prompt = ws === 0 ? "You win the trick." : `${PLAYERS[ws].name} wins the trick.`;
  }
  else if (G.turn === 0)
  {
    if (!t.length) prompt = "Your turn. You lead.";
    else
    {
      const lead = t[0].card.s;
      const L = legal(G, 0);
      if (L.length === G.hands[0].length) prompt = "Your turn. Any card.";
      else if (L[0].s === lead) prompt = `Your turn. You must follow ${suitSpan(lead)}.`;
      else prompt = `Your turn. No ${suitSpan(lead)}, so you must trump.`;
    }
  }
  else prompt = `${PLAYERS[G.turn].name} is thinking…`;
  const w = t.length ? t[winningIdx(t, G.trump)] : null;
  el.innerHTML = `<div class="title">Trick ${Math.min(G.trickNo + 1, G.handSize)} of ${G.handSize}</div>
    <div class="row">Leader: <b>${PLAYERS[G.leader].name}</b>${t.length ? ` &middot; Led ${suitSpan(t[0].card.s)}` : ""}</div>
    <div class="row">Winning: <b>${w ? `${PLAYERS[w.seat].name}, ${cardLabel(w.card)}` : "–"}</b></div>
    <div class="prompt">${prompt}</div>`;
}
function render()
{
  renderScore();
  renderPlaques();
  renderFans();
  renderHand();
  renderTrick();
  renderTrickInfo();
}

// ---------- flow ----------
const PACES = [{ label: "Relaxed", k: 1.6 }, { label: "Normal", k: 1 }, { label: "Brisk", k: 0.45 }];
let paceIdx = 1;
let token = 0;
let busy = false;
const wait = ms => new Promise(r => setTimeout(r, ms * PACES[paceIdx].k));

function bubble(seat, text)
{
  const el = $(`seat-${seat}`);
  el.querySelector(".bubble")?.remove();
  const b = document.createElement("div");
  b.className = "bubble";
  b.textContent = text;
  el.appendChild(b);
  setTimeout(() => b.remove(), 1700);
}
function animateIn(card, from)
{
  const el = $("trick").querySelector(`[data-id="${cardId(card)}"]`);
  if (!el || reducedMotion) return Promise.resolve();
  const x = parseFloat(el.style.left), y = parseFloat(el.style.top);
  const dx = from.x - x, dy = from.y - y;
  return el.animate(
    [{ transform: `translate(${dx}px, ${dy}px) scale(${from.s ?? 0.75})`, opacity: from.o ?? 0.6 }, { transform: "none", opacity: 1 }],
    { duration: 360 * Math.max(PACES[paceIdx].k, 0.6), easing: "cubic-bezier(.2,.75,.25,1)" }).finished;
}
async function sweep(winner)
{
  if (reducedMotion) return;
  const [tx, ty] = plaqueCenter(winner);
  const { w, h } = trickSize(G);
  const anims = [...$("trick").querySelectorAll(".tcard")].map(el =>
  {
    const x = parseFloat(el.style.left) + w / 2, y = parseFloat(el.style.top) + h / 2;
    return el.animate([{ transform: "none", opacity: 1 }, { transform: `translate(${tx - x}px, ${ty - y}px) scale(.35)`, opacity: 0 }],
      { duration: 480, easing: "cubic-bezier(.5,0,.75,0)", fill: "forwards" }).finished;
  });
  await Promise.all(anims);
}
async function finishTrick(t)
{
  render();
  await wait(1100);
  if (t !== token) return;
  const w = G.trick[winningIdx(G.trick, G.trump)].seat;
  await sweep(w);
  if (t !== token) return;
  resolveTrick(G);
}
async function step()
{
  const t = token;
  while (t === token)
  {
    render();
    if (G.phase === "bid")
    {
      if (G.turn === 0) { showBidStrip(); return; }
      await wait(750);
      if (t !== token) return;
      const s = G.turn, b = botBid(G, s);
      placeBid(G, s, b);
      bubble(s, `Bids ${b}`);
      if (G.phase === "play") { render(); await wait(1000); }
      continue;
    }
    if (G.phase === "play")
    {
      if (G.turn === 0) return;   // the hand is live; humanPlay() resumes
      await wait(800);
      if (t !== token) return;
      const s = G.turn;
      const full = playCard(G, s, botPlay(G, s));
      render();
      const [sx, sy] = plaqueCenter(s), size = trickSize(G);
      await animateIn(G.trick[G.trick.length - 1].card, { x: sx - size.w / 2, y: sy - size.h / 2, s: 0.55, o: 0.3 });
      if (full) await finishTrick(t);
      continue;
    }
    if (G.phase === "end")
    {
      if (!G.scored) scoreRound(G);
      render();
      await wait(500);
      if (t === token) showRoundSheet();
      return;
    }
    return;
  }
}
async function humanPlay(card, btn)
{
  if (busy || G.phase !== "play" || G.turn !== 0 || G.trick.length === G.n) return;
  if (!legal(G, 0).includes(card))
  {
    const lead = G.trick[0].card.s;
    toast(G.hands[0].some(c => c.s === lead) ? `You must follow ${SYM[lead]}.` : `You have no ${SYM[lead]}, so you must trump.`);
    return;
  }
  const r = toStage(btn.querySelector(".face").getBoundingClientRect());
  const size = trickSize(G);
  busy = true;
  const t = token;
  const full = playCard(G, 0, card);
  render();
  await animateIn(card, { x: r.x + (r.w - size.w) / 2, y: r.y + (r.h - size.h) / 2, s: 1.1, o: 1 });
  if (full) await finishTrick(t);
  busy = false;
  if (t === token) step();
}

// ---------- bid strip ----------
function showBidStrip()
{
  const el = $("bidstrip");
  const f = forbiddenBid(G);
  let opts = "";
  for (let b = 0; b <= G.handSize; b++) opts += `<button class="opt" data-bid="${b}" ${b === f ? "disabled aria-disabled=\"true\"" : ""}>${b}</button>`;
  const sum = G.bids.reduce((a, b) => a + (b ?? 0), 0);
  const note = f !== null
    ? `You bid last. ${f} would make the bids add up to ${G.handSize}, and somebody has to be wrong.`
    : `${G.bids.filter(b => b !== null).length} bid so far, totalling ${sum} of ${G.handSize} tricks.`;
  el.innerHTML = `<h2>How many tricks will you take?</h2><div class="opts">${opts}</div><p>${note}</p>`;
  el.hidden = false;
  el.querySelectorAll(".opt").forEach(b => b.addEventListener("click", () => humanBid(+b.dataset.bid)));
  el.querySelector(".opt:not(:disabled)")?.focus({ preventScroll: true });
}
function humanBid(b)
{
  if (G.phase !== "bid" || G.turn !== 0 || b === forbiddenBid(G) || b > G.handSize) return;
  $("bidstrip").hidden = true;
  placeBid(G, 0, b);
  bubble(0, `You bid ${b}`);
  step();
}

// ---------- modal ----------
function openModal(html, actions)
{
  $("sheet").innerHTML = html + `<div class="actions">${actions.map((a, i) => `<button data-i="${i}" class="${a.quiet ? "quiet" : ""}">${a.label}</button>`).join("")}</div>`;
  $("sheet").querySelectorAll(".actions button").forEach(b => b.addEventListener("click", () => actions[+b.dataset.i].run()));
  $("modal").hidden = false;
  $("sheet").querySelector(".actions button:last-child")?.focus({ preventScroll: true });
}
const closeModal = () => { $("modal").hidden = true; };
// The menu bar sits outside the scrim, so Rules or Settings can open over the round sheet: closing returns to it.
const back = () => { G.phase === "end" ? showRoundSheet() : closeModal(); };
const signed = v => v > 0 ? `<span class="plus">+${v}</span>` : v < 0 ? `<span class="minus">−${-v}</span>` : "0";

function showRoundSheet()
{
  const h = G.history[G.history.length - 1];
  const rows = seatsOf(G).map(s =>
  {
    const r = h.rows[s];
    return `<tr><td>${PLAYERS[s].name}</td><td>${h.bids[s]}</td><td>${h.won[s]}</td><td>${signed(r.base)}${r.bonus ? ` <span class="note">(${r.bonus > 0 ? "+10 streak" : "−10 streak"})</span>` : ""}</td><td>${h.totals[s]}</td></tr>`;
  }).join("");
  const last = G.roundIdx + 1 >= G.schedule.length;
  openModal(`<h2>Round ${h.round} &middot; ${h.handSize} card${h.handSize > 1 ? "s" : ""}</h2>
    <div class="meta">${h.trumpCard ? `Trump ${cardLabel(h.trumpCard)}` : "No trump"} &middot; exact bid scores 5 + bid, a miss loses the difference</div>
    <table><thead><tr><th>Player</th><th>Bid</th><th>Taken</th><th>Round</th><th>Total</th></tr></thead><tbody>${rows}</tbody></table>`,
    [{ label: "Round history", quiet: true, run: showHistory },
     { label: last ? "Final standings" : `Deal round ${G.roundIdx + 2}`, run: () => { closeModal(); last ? showStandings() : nextRound(); } }]);
}
function showHistory()
{
  const ps = seatsOf(G);
  const head = `<tr><th>Round</th><th>Cards</th>${ps.map(s => `<th>${PLAYERS[s].name}</th>`).join("")}</tr>`;
  const body = G.history.map(h => `<tr><td>${h.round}</td><td>${h.handSize}</td>${ps.map(s => `<td>${signed(h.rows[s].base + h.rows[s].bonus)} <span class="note">${h.bids[s]}/${h.won[s]}</span></td>`).join("")}</tr>`).join("");
  const tot = `<tr><td><b>Total</b></td><td></td>${G.totals.map(v => `<td><b>${v}</b></td>`).join("")}</tr>`;
  const roundDone = G.phase === "end";
  openModal(`<h2>Round history</h2><div class="meta">Score per round, then bid/taken</div>
    <div class="scroll"><table><thead>${head}</thead><tbody>${body || `<tr><td colspan="${G.n + 2}" class="note">No rounds finished yet.</td></tr>`}${tot}</tbody></table></div>`,
    [{ label: roundDone ? "Back" : "Close", run: back }]);
}
function showStandings()
{
  const order = seatsOf(G).sort((a, b) => G.totals[b] - G.totals[a]);
  const top = G.totals[order[0]];
  const winners = order.filter(s => G.totals[s] === top).map(s => PLAYERS[s].name);
  openModal(`<h2>${winners.length > 1 ? `A draw between ${winners.join(" and ")}` : winners[0] === "You" ? "You win the game" : `${winners[0]} wins the game`}</h2>
    <table><thead><tr><th>Place</th><th>Player</th><th>Total</th></tr></thead><tbody>
    ${order.map(s => `<tr><td>${1 + order.filter(o => G.totals[o] > G.totals[s]).length}</td><td style="text-align:left">${PLAYERS[s].name}</td><td>${G.totals[s]}</td></tr>`).join("")}</tbody></table>`,
    [{ label: "New game", run: () => { closeModal(); startNewGame(); } }]);
}
function showLastTrick()
{
  const lt = G.lastTrick;
  if (!lt) { toast("No trick has finished yet this round."); return; }
  openModal(`<h2>Last trick</h2><div class="meta">${lt.winner === 0 ? "You" : PLAYERS[lt.winner].name} took it</div>
    <div class="mini">${lt.cards.map(t => `<figure class="${t.seat === lt.winner ? "win" : ""}"><img alt="" src="${CARD_IMG[cardId(t.card)]}">${PLAYERS[t.seat].name}</figure>`).join("")}</div>`,
    [{ label: "Close", run: closeModal }]);
}
function showRules()
{
  const n = G.n, low = ALL_RANKS[lowestRank(n)];
  openModal(`<h2>Rules at a glance</h2><ul>
    <li>${n} players use Ace down to ${RANK_NAME[low] ?? low}: eight cards per player, ${8 * n} in all.</li>
    <li>Rounds go 1 (&times;${n}), 2, 3 &hellip; 7, 8 (&times;${n}), 7 &hellip; 3, 2, 1 (&times;${n}): ${G.schedule.length} rounds.</li>
    <li>Below 8 cards, the next card is turned as trump. The 8-card rounds have no trump.</li>
    <li>Everyone bids how many tricks they will take. The last bidder may not make the bids add up to the number of tricks.</li>
    <li>Follow suit if you can, otherwise trump if you can. You never have to overtrump.</li>
    <li>An exact bid scores 5 + bid. A miss loses the difference. Five exact bids in a row earn +10, five misses cost 10.</li></ul>`,
    [{ label: "Close", run: back }]);
}
function showSettings()
{
  const seg = PACES.map((p, i) => `<button aria-pressed="${i === paceIdx}" data-p="${i}">${p.label}</button>`).join("");
  openModal(`<h2>Settings</h2><div class="meta">Only the table pace works in this mock-up. Pick the number of players in the menu bar.</div>
    <div class="label" style="margin-top:18px">Bot pace</div><div class="seg" id="pace">${seg}</div>`,
    [{ label: "Done", run: back }]);
  $("pace").querySelectorAll("button").forEach(b => b.addEventListener("click", () =>
  {
    paceIdx = +b.dataset.p;
    $("pace").querySelectorAll("button").forEach(x => x.setAttribute("aria-pressed", x === b));
  }));
}

let toastTimer = 0;
function toast(text)
{
  const el = $("toast");
  el.innerHTML = text;
  el.style.opacity = 1;
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.style.opacity = 0; }, 2200);
}

function resetTable()
{
  token++;
  busy = false;
  $("bidstrip").hidden = true;
  closeModal();
}
function nextRound()
{
  token++;
  G.roundIdx++;
  dealRound(G);
  busy = false;
  step();
}
function startNewGame()
{
  resetTable();
  G = newGame(G.n);
  dealRound(G);
  buildSeats();
  step();
}
function setPlayers(n)
{
  resetTable();
  G = demoGame(n);
  buildSeats();
  step();
}

$("m-new").addEventListener("click", startNewGame);
$("m-settings").addEventListener("click", showSettings);
$("m-rules").addEventListener("click", showRules);
$("m-exit").addEventListener("click", () => toast("In the app, Exit returns to the main menu."));
document.querySelectorAll(".players button").forEach(b => b.addEventListener("click", () => setPlayers(+b.dataset.n)));
$("btn-history").addEventListener("click", showHistory);
$("btn-last").addEventListener("click", showLastTrick);
$("modal").addEventListener("click", e => { if (e.target === $("modal") && G.phase !== "end") closeModal(); });
addEventListener("keydown", e =>
{
  if (e.key === "Escape" && !$("modal").hidden && G.phase !== "end") closeModal();
  if (!$("bidstrip").hidden && /^[0-8]$/.test(e.key)) humanBid(+e.key);
});

setPlayers(6);
