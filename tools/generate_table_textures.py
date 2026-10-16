#!/usr/bin/env python3
"""Generate the round table's two textures: the wooden rim and the green felt.

These are the exact textures the table mock-up in docs/ui-redesign/mockup/ uses.
They are generated rather than photographed so there is no licence to track, and
the generator is committed so they can be regenerated (or re-tuned) instead of
being opaque binaries.

    python3 tools/generate_table_textures.py            # writes resources/images/table/
    python3 tools/generate_table_textures.py --out DIR

Needs numpy and Pillow. The output is deterministic: the same seed gives the same
pixels, byte for byte.

WHAT THE TWO IMAGES ARE
- table_wood.png: 1020x1020 RGBA. A disc whose OUTER radius is the table's edge
  (510 px). Only the ring between radius ~415 and 510 is opaque; the inside is
  transparent because the felt is drawn over it. The grain runs ROUND the table,
  like a bent solid-wood edge, and the outer lip has a soft highlight falling into
  shadow. Drawn at 1020 px across in the mock-up's 1536x1024 table space.
- table_felt.png: 880x880 RGB. Fine fuzzy cloth with a faint uneven nap. Drawn
  as an 880 px circle (radius 440) concentric with the wood. The vignette (lighter
  centre, darker towards the rim) is NOT baked in: the mock-up draws it on top as
  a radial gradient, so the client can do the same and keep it tunable.
"""

import argparse
import pathlib

import numpy as np
from PIL import Image, ImageFilter

SEED = 11

# Wood geometry, in pixels of the 1020 px image.
WOOD_SIZE = 1020
WOOD_OUTER_RADIUS = 510.0
WOOD_GRAIN_SAMPLES = 4096          # samples round the circle in the grain tables
WOOD_BAND = (380, 512)             # radial band the grain tables cover
WOOD_OPAQUE_FROM = 415             # inside this radius the image is transparent (felt covers it)

FELT_SIZE = 880


def wrap_blur_cols(a, size, passes=3):
    """Box blur along axis 1 with wrap-around, so the grain is seamless all the way round."""
    for _ in range(passes):
        c = np.cumsum(np.concatenate([a[:, -size:], a, a[:, :size]], axis=1), axis=1)
        a = (np.roll(c, -size, axis=1) - c)[:, size:size + a.shape[1]] / size
    return a


def blur_rows(a, size):
    k = np.ones(size) / size
    return np.apply_along_axis(lambda col: np.convolve(col, k, mode='same'), 0, a)


def normalised(a):
    return (a - a.mean()) / a.std()


def make_wood(rng):
    S, R_OUT, W = WOOD_SIZE, WOOD_OUTER_RADIUS, WOOD_GRAIN_SAMPLES
    R0, R1 = WOOD_BAND
    H = R1 - R0

    # Long fibres and fine fibres along the circumference.
    fib = normalised(blur_rows(wrap_blur_cols(rng.normal(size=(H, W)), 40, 3), 2))
    fine = normalised(wrap_blur_cols(rng.normal(size=(H, W)), 6, 2))

    y, x = np.mgrid[0:S, 0:S]
    dx, dy = x - S / 2 + 0.5, y - S / 2 + 0.5
    r = np.hypot(dx, dy)
    th = np.arctan2(dy, dx)
    ti = ((th + np.pi) / (2 * np.pi) * W).astype(int) % W
    ri = np.clip((r - R0).astype(int), 0, H - 1)

    # Growth rings, gently warped so they wander like real grain.
    warp = np.zeros_like(r)
    for k, amp in [(2, 0.9), (3, 0.6), (5, 0.35), (9, 0.18)]:
        ph = rng.uniform(0, 2 * np.pi)
        c = rng.uniform(-0.02, 0.02)
        warp += amp * np.sin(k * th + ph + r * c)
    warp += 0.35 * fib[ri, ti]
    rings = (0.5 + 0.5 * np.sin(2 * np.pi * (r / 11.0 + warp))) ** 3

    # Darker streaks that run a long way round the rim.
    streak = normalised(blur_rows(wrap_blur_cols(rng.normal(size=(H, W)), 160, 3), 3))
    late = 0.5 + 0.5 * np.sin(2 * np.pi * (r / 31.0 + 0.5 * warp))   # broad light/dark bands

    t = (0.52 + 0.12 * (late - 0.5) - 0.16 * rings + 0.10 * fib[ri, ti]
         + 0.05 * fine[ri, ti] + 0.09 * streak[ri, ti])
    t = np.clip(t, 0, 1)[..., None]
    light = np.array([150, 92, 52])
    mid = np.array([108, 60, 30])
    dark = np.array([58, 30, 14])
    col = np.where(t > 0.5, mid + (light - mid) * (t - 0.5) * 2, dark + (mid - dark) * t * 2)

    # Rounded outer edge: a soft highlight just inside the lip, then shadow at the very edge.
    e = R_OUT - r
    shade = np.ones_like(r)
    shade *= 1 + 0.16 * np.exp(-((e - 9) / 5.0) ** 2)
    shade *= np.clip(0.45 + e / 10.0, 0.45, 1.0)
    col = np.clip(col * shade[..., None], 0, 255)

    alpha = np.clip((R_OUT - r) * 2 + 0.5, 0, 1) * np.clip(r - WOOD_OPAQUE_FROM, 0, 1) * 255
    return Image.fromarray(np.dstack([col, alpha]).astype(np.uint8), 'RGBA')


def make_felt(rng):
    F = FELT_SIZE
    base = np.array([31, 104, 64], float)

    def noise(a, scale, blur=None, resize=None):
        img = Image.fromarray(((a * scale) + 128).clip(0, 255).astype(np.uint8))
        if blur is not None:
            img = img.filter(ImageFilter.GaussianBlur(blur))
        if resize is not None:
            img = img.resize(resize, Image.BICUBIC)
        return np.array(img, float) - 128

    n1 = noise(rng.normal(size=(F, F)), 40, blur=0.7)                    # fibres
    n2 = noise(rng.normal(size=(F // 22, F // 22)), 50, resize=(F, F))   # uneven nap
    n3 = noise(rng.normal(size=(F, F)), 60, blur=2.2)                    # mottling
    lum = 1 + n1 / 128 * 0.22 + n2 / 128 * 0.07 + n3 / 128 * 0.10
    return Image.fromarray(np.clip(base[None, None, :] * lum[..., None], 0, 255).astype(np.uint8), 'RGB')


def main():
    repo = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--out', type=pathlib.Path, default=repo / 'resources' / 'images' / 'table')
    args = parser.parse_args()

    # ONE generator, used in this order: wood first, then felt. Changing the order
    # changes every pixel of both images.
    rng = np.random.default_rng(SEED)
    wood = make_wood(rng)
    felt = make_felt(rng)

    args.out.mkdir(parents=True, exist_ok=True)
    wood.save(args.out / 'table_wood.png', optimize=True)
    felt.save(args.out / 'table_felt.png', optimize=True)
    print(f'wrote {args.out / "table_wood.png"} and {args.out / "table_felt.png"}')


if __name__ == '__main__':
    main()
