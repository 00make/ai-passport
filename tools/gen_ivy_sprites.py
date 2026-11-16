#!/usr/bin/env python3
"""Generate Flash rodata sprite tables for IVY Digital Business Card."""

from __future__ import annotations

import argparse
from collections import deque
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage


REPO_ROOT = Path(__file__).resolve().parent.parent
IMG_OFFWORK = REPO_ROOT / "assets" / "images" / "ivy-offwork-spritesheet.jpg"
IMG_TURNAROUND = REPO_ROOT / "assets" / "images" / "ivy-turnaround-expressions.jpg"
OUT_H = REPO_ROOT / "main" / "ui" / "ivy_sprites_data.h"
OUT_C = REPO_ROOT / "main" / "ui" / "ivy_sprites_data.c"


def floodfill_exterior_bg(arr: np.ndarray, tol: int = 24, border_white_floor: int = 232) -> np.ndarray:
    """Return boolean foreground mask by flood-filling background from all image borders."""
    h, w, _ = arr.shape
    bg = np.median(
        np.concatenate([arr[0, :], arr[-1, :], arr[:, 0], arr[:, -1]], axis=0),
        axis=0,
    )
    arr_i = arr.astype(np.int16)
    diff = np.abs(arr_i - bg.astype(np.int16)).max(axis=2)
    is_bg_cand = (diff <= tol) | (arr_i.min(axis=2) >= border_white_floor)

    visited = np.zeros((h, w), dtype=bool)
    q: deque[tuple[int, int]] = deque()

    for x in range(w):
        for y in (0, h - 1):
            if is_bg_cand[y, x] and not visited[y, x]:
                visited[y, x] = True
                q.append((y, x))
    for y in range(1, h - 1):
        for x in (0, w - 1):
            if is_bg_cand[y, x] and not visited[y, x]:
                visited[y, x] = True
                q.append((y, x))

    while q:
        cy, cx = q.popleft()
        for ny, nx in ((cy - 1, cx), (cy + 1, cx), (cy, cx - 1), (cy, cx + 1)):
            if 0 <= ny < h and 0 <= nx < w and not visited[ny, nx] and is_bg_cand[ny, nx]:
                visited[ny, nx] = True
                q.append((ny, nx))

    return ~visited


def trim_white_halo(arr: np.ndarray, mask: np.ndarray, luma_limit: int = 165) -> np.ndarray:
    """Strip bright anti-aliased fringe pixels along the exterior silhouette of an inked sprite."""
    eroded = ndimage.binary_erosion(mask, iterations=1, border_value=0)
    perimeter = mask & ~eroded
    luma = (
        arr[:, :, 0].astype(np.int32) * 299
        + arr[:, :, 1].astype(np.int32) * 587
        + arr[:, :, 2].astype(np.int32) * 114
    ) // 1000
    low_sat = (arr.max(axis=2).astype(np.int16) - arr.min(axis=2).astype(np.int16)) < 28
    clean_mask = mask & ~(perimeter & (luma > luma_limit) & low_sat)

    # Second pass for any 2px AA halo on very light JPEG boundaries
    eroded2 = ndimage.binary_erosion(clean_mask, iterations=1, border_value=0)
    perim2 = clean_mask & ~eroded2
    clean_mask = clean_mask & ~(perim2 & (luma > (luma_limit + 20)) & low_sat)

    # Remove tiny isolated JPEG compression specks (< 12 pixels)
    labeled, n = ndimage.label(clean_mask)
    if n > 1:
        sizes = ndimage.sum(clean_mask, labeled, range(1, n + 1))
        keep = np.zeros(n + 1, dtype=bool)
        for idx, sz in enumerate(sizes, start=1):
            if sz >= 12:
                keep[idx] = True
        clean_mask = keep[labeled]
    return clean_mask


def crop_rgba(arr: np.ndarray, mask: np.ndarray, pad: int = 1) -> Image.Image:
    ys, xs = np.where(mask)
    if len(ys) == 0:
        raise ValueError("Empty sprite mask")
    y0 = max(0, int(ys.min()) - pad)
    y1 = min(arr.shape[0], int(ys.max()) + 1 + pad)
    x0 = max(0, int(xs.min()) - pad)
    x1 = min(arr.shape[1], int(xs.max()) + 1 + pad)

    sub_rgb = arr[y0:y1, x0:x1].copy()
    sub_mask = mask[y0:y1, x0:x1]
    # Darken surviving semi-neutral fringe pixels near the edge slightly so dark outline looks crisp
    eroded = ndimage.binary_erosion(sub_mask, iterations=1, border_value=0)
    rim = sub_mask & ~eroded
    luma = sub_rgb.mean(axis=2)
    low_sat = (sub_rgb.max(axis=2).astype(np.int16) - sub_rgb.min(axis=2).astype(np.int16)) < 30
    fade_rim = rim & (luma > 95) & low_sat
    if np.any(fade_rim):
        sub_rgb[fade_rim] = np.clip(sub_rgb[fade_rim].astype(np.float32) * 0.45, 0, 255).astype(np.uint8)

    rgba = np.zeros((y1 - y0, x1 - x0, 4), dtype=np.uint8)
    rgba[:, :, :3] = sub_rgb
    rgba[:, :, 3] = np.where(sub_mask, 255, 0)
    return Image.fromarray(rgba)


def resize_rgba_nearest_alpha(img: Image.Image, target_w: int, target_h: int) -> Image.Image:
    """Resize RGB with LANCZOS and alpha with hard threshold after bilinear downsample to prevent fringes."""
    rgb = img.convert("RGB").resize((target_w, target_h), Image.Resampling.LANCZOS)
    alpha_soft = img.getchannel("A").resize((target_w, target_h), Image.Resampling.BILINEAR)
    rgb_arr = np.array(rgb, dtype=np.float32)
    a_arr = np.array(alpha_soft, dtype=np.float32)

    # Any pixel blended with transparent RGB=(0,0,0) gets darker/cleaner instead of white halo,
    # threshold alpha >= 120 cleanly for 1-bit transparency.
    hard_mask = a_arr >= 120
    # Un-premultiply? PIL LANCZOS on RGB used black (0,0,0) in transparent pixels!
    # Un-premultiply slightly where a_arr is between 120..240 so edges don't get too dark or blown out.
    scale_factor = np.where((a_arr >= 120) & (a_arr < 250), 255.0 / np.maximum(a_arr, 120.0), 1.0)
    rgb_arr = np.clip(rgb_arr * scale_factor[:, :, None], 0, 255)

    out = np.zeros((target_h, target_w, 4), dtype=np.uint8)
    out[:, :, :3] = rgb_arr.astype(np.uint8)
    out[:, :, 3] = np.where(hard_mask, 255, 0)
    return Image.fromarray(out)


def extract_offwork_sprites() -> list[tuple[str, Image.Image]]:
    im = Image.open(IMG_OFFWORK).convert("RGB")
    arr = np.array(im, dtype=np.uint8)
    sprites: list[tuple[str, Image.Image]] = []

    # First compute each cell's raw foreground height so we can scale uniformly by 0.84
    raw_frames: list[tuple[str, Image.Image]] = []
    # Exact character cutoffs before "下班啦!" caption outline starts
    cell_ymax = {
        (0, 0): 216,
        (0, 1): 216,
        (0, 2): 213,
        (0, 3): 216,
        (1, 0): 217,
        (1, 1): 217,
        (1, 2): 217,
        (1, 3): 217,
    }
    for r in range(2):
        for c in range(4):
            idx = r * 4 + c
            y0 = r * 253
            x0 = c * 256
            sub = arr[y0 : y0 + cell_ymax[(r, c)], x0 : x0 + 256]
            # Pad 1px white border on all 4 sides so open bottoms/edges flood-fill cleanly!
            padded = np.full((sub.shape[0] + 2, sub.shape[1] + 2, 3), 255, dtype=np.uint8)
            padded[1:-1, 1:-1] = sub
            fg = floodfill_exterior_bg(padded, tol=25, border_white_floor=234)
            fg = fg[1:-1, 1:-1]
            fg = trim_white_halo(sub, fg, luma_limit=160)
            raw_frames.append((f"WORK_{idx}", crop_rgba(sub, fg, pad=1)))

    # Uniform scale for all 8 story frames so IVY fills the 240x260 stage cleanly.
    work_scale = 1.06
    for name, img in raw_frames:
        scale = min(work_scale, 216.0 / max(1, img.width), 218.0 / max(1, img.height))
        tw = max(16, int(round(img.width * scale)))
        th = max(16, int(round(img.height * scale)))
        sprites.append((name, resize_rgba_nearest_alpha(img, tw, th)))

    # Extract "下班啦!" comic banner from cell (0, 3) bottom (y_local 217..252, x 40..220)
    ban_sub = arr[217:252, 768 + 40 : 768 + 220]
    padded = np.full((ban_sub.shape[0] + 2, ban_sub.shape[1] + 2, 3), 255, dtype=np.uint8)
    padded[1:-1, 1:-1] = ban_sub
    ban_fg = floodfill_exterior_bg(padded, tol=25, border_white_floor=234)[1:-1, 1:-1]
    # Remove any small top sliver from shoes if present (keep largest bottom text components y >= 4)
    labeled, n = ndimage.label(ban_fg)
    keep = np.zeros_like(ban_fg)
    for cid in range(1, n + 1):
        comp = labeled == cid
        if comp.sum() >= 20 and np.where(comp)[0].mean() >= 5:
            keep |= comp
    ban_fg = trim_white_halo(ban_sub, keep, luma_limit=175)
    ban_img = crop_rgba(ban_sub, ban_fg, pad=1)
    ban_scale = 136.0 / max(1, ban_img.width)
    sprites.append(
        (
            "OFFWORK_BANNER",
            resize_rgba_nearest_alpha(
                ban_img,
                int(round(ban_img.width * ban_scale)),
                int(round(ban_img.height * ban_scale)),
            ),
        )
    )
    return sprites


def extract_turnaround_and_expressions() -> list[tuple[str, Image.Image]]:
    im = Image.open(IMG_TURNAROUND).convert("RGB")
    arr = np.array(im, dtype=np.uint8)

    # Character outline & fill sits cleanly inside < 215 dark ink boundary; apply 3 hairline bridge cuts.
    fg = arr.min(axis=2) < 215
    fg[190:290, 682:685] = False
    fg[195:220, 829:832] = False
    fg[480:505, 830:833] = False

    labeled, n = ndimage.label(fg)
    objs = ndimage.find_objects(labeled)

    # Identify each of the 7 components by approximate center coordinate
    found: dict[str, tuple[tuple[slice, slice], int]] = {}
    for cid, sl in enumerate(objs, start=1):
        h = sl[0].stop - sl[0].start
        w = sl[1].stop - sl[1].start
        if h < 80 or w < 80:
            continue
        cx = (sl[1].start + sl[1].stop) * 0.5
        cy = (sl[0].start + sl[0].stop) * 0.5
        if h > 400:
            if cx < 290:
                found["TURN_FRONT"] = (sl, cid)
            elif cx < 480:
                found["TURN_SIDE"] = (sl, cid)
            else:
                found["TURN_BACK"] = (sl, cid)
        else:
            if cy < 340:
                if cx < 830:
                    found["EXPR_LAUGH"] = (sl, cid)
                else:
                    found["EXPR_SHOCK"] = (sl, cid)
            else:
                if cx < 830:
                    found["EXPR_DEADPAN"] = (sl, cid)
                else:
                    found["EXPR_SMIRK"] = (sl, cid)

    # Patch the small upper-left blonde hair notch on EXPR_DEADPAN occluded by TURN_BACK's right hand
    # using EXPR_LAUGH's unoccluded upper-left hair crown aligned to (dx=-1, dy=-283).
    laugh_sl, laugh_cid = found["EXPR_LAUGH"]
    laugh_mask_full = ndimage.binary_fill_holes(labeled == laugh_cid)
    dead_sl, dead_cid = found["EXPR_DEADPAN"]
    dead_mask_full = ndimage.binary_fill_holes(labeled == dead_cid)
    # Build extended arrays in full im2 coordinates for patch step
    patch_arr = arr.copy()
    patch_mask = dead_mask_full.copy()
    hole_region = np.zeros_like(patch_mask)
    for y in range(382, 456):
        ly = y - 283
        if 0 <= ly < arr.shape[0]:
            for x in range(672, 742):
                lx = x - 1
                if 0 <= lx < arr.shape[1] and not patch_mask[y, x] and laugh_mask_full[ly, lx]:
                    hole_region[y, x] = True
    hole_blend = ndimage.binary_dilation(hole_region, iterations=3)
    for y in range(382, 458):
        ly = y - 283
        if 0 <= ly < arr.shape[0]:
            for x in range(672, 742):
                lx = x - 1
                if 0 <= lx < arr.shape[1] and hole_blend[y, x] and laugh_mask_full[ly, lx]:
                    patch_arr[y, x] = arr[ly, lx]
                    patch_mask[y, x] = True
    patch_mask = ndimage.binary_fill_holes(patch_mask)

    expected = [
        ("TURN_FRONT", 242),
        ("TURN_SIDE", 242),
        ("TURN_BACK", 242),
        ("EXPR_LAUGH", 224),
        ("EXPR_SHOCK", 224),
        ("EXPR_DEADPAN", 224),
        ("EXPR_SMIRK", 224),
    ]
    sprites: list[tuple[str, Image.Image]] = []
    for name, target_h in expected:
        if name not in found:
            raise RuntimeError(f"Missing turnaround/expression component: {name}")
        sl, cid = found[name]
        if name == "EXPR_DEADPAN":
            y0 = max(0, min(sl[0].start, 384) - 2)
            y1 = min(arr.shape[0], sl[0].stop + 2)
            x0 = max(0, min(sl[1].start, 675) - 2)
            x1 = min(arr.shape[1], sl[1].stop + 2)
            sub = patch_arr[y0:y1, x0:x1]
            filled = patch_mask[y0:y1, x0:x1]
        else:
            # Expand slice by 2px safely to preserve outline edges during erosion/hole-fill
            y0 = max(0, sl[0].start - 2)
            y1 = min(arr.shape[0], sl[0].stop + 2)
            x0 = max(0, sl[1].start - 2)
            x1 = min(arr.shape[1], sl[1].stop + 2)
            sub = arr[y0:y1, x0:x1]
            comp_mask = labeled[y0:y1, x0:x1] == cid
            filled = ndimage.binary_fill_holes(comp_mask)
        filled = trim_white_halo(sub, filled, luma_limit=155)
        cropped = crop_rgba(sub, filled, pad=1)
        scale = float(target_h) / float(cropped.height)
        tw = max(16, int(round(cropped.width * scale)))
        sprites.append((name, resize_rgba_nearest_alpha(cropped, tw, target_h)))
    return sprites


def quantize_sprite_rgb565(img: Image.Image) -> tuple[list[int], bytes]:
    """Quantize opaque pixels into up to 255 RGB565 colors (index 0 = transparent)."""
    rgba = np.array(img, dtype=np.uint8)
    h, w, _ = rgba.shape
    mask = rgba[:, :, 3] >= 128

    rgb_only = img.convert("RGB")
    quant = rgb_only.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw_pal = quant.getpalette()[: 255 * 3]
    q_idx = np.array(quant, dtype=np.uint8) + 1  # Shift 0..254 -> 1..255

    pal565 = [0] * 256
    for i in range(255):
        r = raw_pal[i * 3 + 0]
        g = raw_pal[i * 3 + 1]
        b = raw_pal[i * 3 + 2]
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        pal565[i + 1] = rgb565

    out_idx = np.where(mask, q_idx, 0).astype(np.uint8)
    return pal565, out_idx.tobytes()


def write_c_sources(sprites: list[tuple[str, Image.Image]]) -> None:
    total_bytes = 0
    lines_h = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        "typedef enum {",
    ]
    for i, (name, _) in enumerate(sprites):
        lines_h.append(f"    IVY_SPRITE_{name} = {i},")
    lines_h.extend(
        [
            f"    IVY_SPRITE_COUNT = {len(sprites)},",
            "} ivy_sprite_id_t;",
            "",
            "typedef struct {",
            "    uint16_t w;",
            "    uint16_t h;",
            "    const uint16_t *palette;",
            "    const uint8_t *pixels;",
            "} ivy_sprite_t;",
            "",
            "const ivy_sprite_t *ivy_sprite_get(ivy_sprite_id_t id);",
            "",
            "#ifdef __cplusplus",
            "}",
            "#endif",
            "",
        ]
    )

    lines_c = [
        '#include "ivy_sprites_data.h"',
        "#include <stddef.h>",
        "",
    ]

    for name, img in sprites:
        pal, px_bytes = quantize_sprite_rgb565(img)
        w, h = img.size
        total_bytes += len(pal) * 2 + len(px_bytes)
        lname = name.lower()

        lines_c.append(f"static const uint16_t s_pal_{lname}[256] = {{")
        for row_start in range(0, 256, 12):
            chunk = ", ".join(f"0x{v:04X}" for v in pal[row_start : row_start + 12])
            lines_c.append(f"    {chunk},")
        lines_c.append("};")
        lines_c.append("")

        lines_c.append(f"static const uint8_t s_px_{lname}[{len(px_bytes)}] = {{")
        for row_start in range(0, len(px_bytes), 20):
            chunk = ", ".join(f"{b}" for b in px_bytes[row_start : row_start + 20])
            lines_c.append(f"    {chunk},")
        lines_c.append("};")
        lines_c.append("")

    lines_c.append("static const ivy_sprite_t s_sprites[IVY_SPRITE_COUNT] = {")
    for name, img in sprites:
        lname = name.lower()
        lines_c.append(
            f"    [IVY_SPRITE_{name}] = {{ {img.width}, {img.height}, s_pal_{lname}, s_px_{lname} }},"
        )
    lines_c.extend(
        [
            "};",
            "",
            "const ivy_sprite_t *ivy_sprite_get(ivy_sprite_id_t id) {",
            "    if ((int)id < 0 || id >= IVY_SPRITE_COUNT) {",
            "        return NULL;",
            "    }",
            "    return &s_sprites[id];",
            "}",
            "",
        ]
    )

    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text("\n".join(lines_h), encoding="utf-8")
    OUT_C.write_text("\n".join(lines_c), encoding="utf-8")
    print(f"Generated {len(sprites)} sprites ({total_bytes / 1024.0:.1f} KiB Flash rodata)")
    for name, img in sprites:
        print(f"  {name:16s}: {img.width:3d} x {img.height:3d}")


def save_preview(sprites: list[tuple[str, Image.Image]], preview_path: Path) -> None:
    cols = 4
    rows = (len(sprites) + cols - 1) // cols
    cell_w, cell_h = 180, 210
    canvas = Image.new("RGB", (cols * cell_w, rows * cell_h), (10, 11, 14))
    for i, (_, img) in enumerate(sprites):
        r, c = divmod(i, cols)
        # Roundtrip through palette quantization so preview shows exact device output!
        pal, px_bytes = quantize_sprite_rgb565(img)
        w, h = img.size
        buf = np.zeros((h, w, 3), dtype=np.uint8)
        idx_arr = np.frombuffer(px_bytes, dtype=np.uint8).reshape(h, w)
        for y in range(h):
            for x in range(w):
                idx = int(idx_arr[y, x])
                if idx != 0:
                    c565 = pal[idx]
                    r8 = ((c565 >> 11) & 0x1F) * 255 // 31
                    g8 = ((c565 >> 5) & 0x3F) * 255 // 63
                    b8 = (c565 & 0x1F) * 255 // 31
                    buf[y, x] = (r8, g8, b8)
                else:
                    buf[y, x] = (10, 11, 14)
        sprite_im = Image.fromarray(buf)
        ox = c * cell_w + (cell_w - w) // 2
        oy = r * cell_h + (cell_h - h) // 2
        canvas.paste(sprite_im, (ox, oy))
    preview_path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(preview_path)
    print(f"Saved quantized dark-background preview to {preview_path}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", type=Path, default=None)
    args = parser.parse_args()

    sprites = extract_offwork_sprites() + extract_turnaround_and_expressions()
    write_c_sources(sprites)
    if args.preview is not None:
        save_preview(sprites, args.preview)


if __name__ == "__main__":
    main()
