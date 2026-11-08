# Upscaled sprites (the UPSCALED SPRITES option): every frame of the shapes listed in a job file redrawn larger by
# Fooocus (SDXL), cut back to the original's outline, written as <out>/<method>/SSSS_FFF.png (RGBA, 4x the frame;
# shape and frame in decimal).
#
#   1. spriteexport tools/release frames.txt raw            (frames.txt: "shape frame" lines; missing frames skipped)
#   2. <fooocus>/python_embeded/python.exe -s tools/upscale/upscale_sprites.py <fooocus>/Fooocus
#          tools/upscale/jobs.txt raw assets/sprites
#
# Methods (one folder each, chosen in the game's options):
#   degrain  ("DEGRAIN + SDXL") the dithering smoothed first, Fooocus's ESRGAN 4x, then SDXL at 0.40 asked for
#            smooth clean surfaces;
#   sdxl55   ("ESRGAN + SDXL") Fooocus's ESRGAN 4x, then SDXL at 0.55 asked for a detailed modern prop.
# Stable from frame to frame and piece to piece: all the frames of a shape are redrawn together on one canvas (one
# look for all of them), with the same seed and description for a whole family (jobs.txt), and each result has its
# colours brought back to the original's (pieces that join still match).
import argparse
import math
import os
import sys

parser = argparse.ArgumentParser()
parser.add_argument('fooocus')
parser.add_argument('jobs', help='lines: "<shapes, comma separated> <description>" (# starts a comment)')
parser.add_argument('raw', help='the frames as spriteexport wrote them (SSSS_FFF.png)')
parser.add_argument('out')
parser.add_argument('--methods', default='degrain,sdxl55')
parser.add_argument('--canvas', help='also save each canvas before and after, into this folder')
opts = parser.parse_args()  # (not "args": Fooocus's launch.py below has its own)
fooocus_dir = os.path.abspath(opts.fooocus)
jobs_path, raw_dir, out_dir = (os.path.abspath(p) for p in (opts.jobs, opts.raw, opts.out))
canvas_dir = os.path.abspath(opts.canvas) if opts.canvas else None

# Fooocus's launch.py sets up its paths, config and models: run all of it but the web UI.
sys.argv = [os.path.join(fooocus_dir, 'launch.py'), '--disable-in-browser']
__file__ = sys.argv[0]
sys.path.insert(0, fooocus_dir)
exec(compile(open(sys.argv[0], encoding='utf-8').read().replace('from webui import *', ''), 'launch.py', 'exec'))

import numpy as np
from PIL import Image, ImageFilter
import modules.patch
modules.patch.patch_all()  # as async_worker does before any model loads
import modules.core as core
import modules.default_pipeline as pipeline
from modules.upscaler import perform_upscale

modules.patch.patch_settings[os.getpid()] = modules.patch.PatchSettings(2.0, 0.3, 1.5, 0.8, 0.25, 7.0)

SCALE = 4
GAP = 32  # black between frames on a canvas (at 4x)
SEED = 1234
NEGATIVE = 'blurry, low quality, jpeg artifacts, text, letters, watermark, deformed, background scenery'
METHODS = {
    'degrain': dict(degrain=True, denoise=0.40,
                    style=', 1995 pre-rendered 3D video game sprite, isometric, soft studio lighting, smooth clean '
                          'surfaces, crisp edges, dark industrial sci-fi, plain black background',
                    negative=NEGATIVE + ', grain, film grain, noise, dithering, speckles, checkerboard pattern'),
    'sdxl55': dict(degrain=False, denoise=0.55,
                   style=', isometric sci-fi video game prop, 1990s industrial dystopia, highly detailed, sharp '
                         'focus, plain black background',
                   negative=NEGATIVE),
}


def frames_of(shape):
    names = sorted(f for f in os.listdir(raw_dir) if f.startswith(f'{shape:04d}_') and f.endswith('.png'))
    return [(n[:-4], Image.open(os.path.join(raw_dir, n)).convert('RGBA')) for n in names]


def on_black(rgba):
    rgb = Image.new('RGB', rgba.size)
    rgb.paste(rgba, mask=rgba.getchannel('A'))
    return rgb


def layout(sizes):
    """A grid as near to square as the frames allow; the top-left of each frame and the canvas size."""
    best = None
    for cols in range(1, len(sizes) + 1):
        rows = math.ceil(len(sizes) / cols)
        cw = [max(w for w, _ in sizes[c::cols]) for c in range(cols)]
        rh = [max(h for _, h in sizes[r * cols:(r + 1) * cols]) for r in range(rows)]
        W, H = sum(cw) + GAP * (cols + 1), sum(rh) + GAP * (rows + 1)
        score = max(W, H) / min(W, H)
        if best is None or score < best[0]:
            spots = [(GAP + sum(cw[:i % cols]) + GAP * (i % cols), GAP + sum(rh[:i // cols]) + GAP * (i // cols))
                     for i in range(len(sizes))]
            best = (score, spots, (W, H))
    return best[1], best[2]


def redraw(canvas, prompt, method):
    """SDXL over the canvas, its longest side brought to 1024..1536 (multiples of 8), returned at the canvas' size."""
    W, H = canvas.size
    side = min(max(max(W, H), 1024), 1536)
    s = side / max(W, H)
    w, h = round(W * s / 8) * 8, round(H * s / 8) * 8
    pixels = core.numpy_to_pytorch(np.array(canvas.resize((w, h), Image.LANCZOS)))
    latent = core.encode_vae(vae=pipeline.final_vae, pixels=pixels, tiled=True)
    m = METHODS[method]
    pos = pipeline.clip_encode(texts=[prompt + m['style']], pool_top_k=1)
    neg = pipeline.clip_encode(texts=[m['negative']], pool_top_k=1)
    img = pipeline.process_diffusion(
        positive_cond=pos, negative_cond=neg, steps=30, switch=30, width=w, height=h, image_seed=SEED,
        callback=lambda *a: None, sampler_name='dpmpp_2m_sde_gpu', scheduler_name='karras', latent=latent,
        denoise=m['denoise'], tiled=True, cfg_scale=4.0, refiner_swap_method='joint', disable_preview=True)[0]
    return Image.fromarray(img).resize((W, H), Image.LANCZOS)


def outline(alpha, size):
    """The original's outline at the new size: smooth, cut at half, then softened by about a pixel."""
    a = alpha.resize(size, Image.BILINEAR).point(lambda v: 255 if v >= 128 else 0)
    return a.filter(ImageFilter.GaussianBlur(0.7))


def match_colours(result, original, mask):
    """Each channel's mean and spread inside the outline brought to the original's (its dithering smoothed away)."""
    r = np.asarray(result, dtype=np.float32).copy()
    o = np.asarray(original.resize(result.size, Image.LANCZOS).filter(ImageFilter.GaussianBlur(3)), dtype=np.float32)
    m = np.asarray(mask) > 128
    if m.sum() < 16:
        return result
    for c in range(3):
        rm, rs = r[..., c][m].mean(), r[..., c][m].std() + 1e-3
        om, os_ = o[..., c][m].mean(), o[..., c][m].std()
        k = np.clip(os_ / rs, 0.8, 1.25)
        r[..., c] = (r[..., c] - rm) * k + om
    return Image.fromarray(np.clip(r, 0, 255).astype(np.uint8))


jobs = []
for line in open(jobs_path, encoding='utf-8'):
    line = line.split('#', 1)[0].strip()
    if line:
        shapes, prompt = line.split(None, 1)
        jobs.append(([int(s) for s in shapes.split(',')], prompt))

for method in opts.methods.split(','):
    os.makedirs(os.path.join(out_dir, method), exist_ok=True)
    for shapes, prompt in jobs:
        for shape in shapes:
            frames = frames_of(shape)
            if not frames:
                continue
            bigs = []
            for stem, src in frames:
                rgb = on_black(src)
                if METHODS[method]['degrain']:
                    rgb = rgb.filter(ImageFilter.SMOOTH_MORE)
                big = Image.fromarray(perform_upscale(np.array(rgb)))
                bigs.append(big.resize((src.width * SCALE, src.height * SCALE), Image.LANCZOS))
            spots, size = layout([b.size for b in bigs])
            canvas = Image.new('RGB', size)
            for big, spot in zip(bigs, spots):
                canvas.paste(big, spot)
            result = redraw(canvas, prompt, method)
            if canvas_dir:
                os.makedirs(canvas_dir, exist_ok=True)
                canvas.save(os.path.join(canvas_dir, f'{method}_{shape:04d}_in.png'))
                result.save(os.path.join(canvas_dir, f'{method}_{shape:04d}_out.png'))
            for (stem, src), big, (x, y) in zip(frames, bigs, spots):
                mask = outline(src.getchannel('A'), big.size)
                sprite = match_colours(result.crop((x, y, x + big.width, y + big.height)), on_black(src), mask)
                sprite = sprite.convert('RGBA')
                sprite.putalpha(mask)
                sprite.save(os.path.join(out_dir, method, stem + '.png'), optimize=True)
            print(f'{method} {shape}: {len(frames)} frames', flush=True)
