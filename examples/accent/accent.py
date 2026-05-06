#!/usr/bin/env python3
"""hyprmural hook — per-output Vibrant accent borders.

Wired via hyprmural.conf:
    hook = ~/.config/hyprmural/accent.py

Hyprmural fires this on each per-output image change with HYPRMURAL_MONITOR
/ HYPRMURAL_WORKSPACE / HYPRMURAL_IMAGE in the env. Behavior:

1. Vibrant-style extraction on the image (saturation/lightness scoring),
   cached by (path, mtime) so repeats are <10ms. Cold extraction on a
   modest image is ~30-80ms with Pillow.
2. Stash the per-monitor color to runtime state.
3. Iterate `hyprctl clients` and push `setprop bordercolor` per window,
   colored by the *window's* monitor — yielding genuinely per-output
   borders (Hyprland's keyword-level border colors are global, so we use
   per-window setprop instead).
4. For groupbar slots (which truly are global keywords), the focused
   monitor's color wins (follow-the-focus policy).

New windows opened between hook fires get the global default until the
next workspace event. Acceptable for v1; subscribe to openwindow events
in a sidecar later if it grates.
"""
from __future__ import annotations

import hashlib
import json
import os
import pathlib
import subprocess
import sys


CACHE_DIR = pathlib.Path(
    os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache"))
) / "hyprmural"
STATE_DIR = pathlib.Path(
    os.environ.get("XDG_RUNTIME_DIR", "/tmp")
) / "hyprmural"


def vibrant_color(path: str) -> str:
    """Return hex 'rrggbb' for the most vibrant color in `path`.

    Algorithm (Vibrant.py-style):
      1. Downsample to ~5k pixels (96x54).
      2. Quantize RGB into a 5x5x5 grid (125 bins) — kills noise from
         single-pixel artifacts that would otherwise win on saturation.
      3. For each populated bin, compute saturation/lightness via HSL.
         Drop bins outside [s>=0.35, 0.2<=l<=0.8] — washed-out, too dark,
         too light don't make good accents.
      4. Score = 3*saturation_match + 1*lightness_match + 1*population —
         saturation is weighted heaviest (Vibrant's whole point), with
         population breaking ties so a *prominent* saturated color wins
         over a rare one.
      5. Highest-scoring bin's centroid wins.
    """
    from PIL import Image  # delayed — only on cache miss

    im = Image.open(path).convert("RGB").resize((96, 54))
    raw = im.tobytes()  # b'RGBRGBRGB...'

    counts = [0] * 125
    for i in range(0, len(raw), 3):
        qr = min(4, raw[i] * 5 // 256)
        qg = min(4, raw[i + 1] * 5 // 256)
        qb = min(4, raw[i + 2] * 5 // 256)
        counts[qr * 25 + qg * 5 + qb] += 1

    max_count = max(counts) or 1
    best_score = -1.0
    best_rgb = (128, 128, 128)
    for bin_id, pop in enumerate(counts):
        if pop == 0:
            continue
        qr, qg, qb = (bin_id // 25) % 5, (bin_id // 5) % 5, bin_id % 5
        r = (qr + 0.5) / 5.0
        g = (qg + 0.5) / 5.0
        b = (qb + 0.5) / 5.0
        mx, mn = max(r, g, b), min(r, g, b)
        l = (mx + mn) * 0.5
        if mx == mn:
            continue
        d = mx - mn
        s = d / (2 - mx - mn) if l > 0.5 else d / (mx + mn)
        if s < 0.35 or l < 0.2 or l > 0.8:
            continue
        sat_match = 1.0 - abs(s - 1.0)
        lit_match = 1.0 - abs(l - 0.5) * 2.0
        pop_match = pop / max_count
        score = 3.0 * sat_match + 1.0 * lit_match + 1.0 * pop_match
        if score > best_score:
            best_score = score
            best_rgb = (int(r * 255), int(g * 255), int(b * 255))

    return f"{best_rgb[0]:02x}{best_rgb[1]:02x}{best_rgb[2]:02x}"


def cached_color(image_path: str) -> str:
    """Vibrant-extract `image_path`, cached by (path, mtime)."""
    mtime = int(os.stat(image_path).st_mtime)
    key = hashlib.sha256(f"{image_path}\x00{mtime}".encode()).hexdigest()[:16]
    cache_file = CACHE_DIR / f"{key}.rgb"
    if cache_file.exists():
        return cache_file.read_text().strip()
    color = vibrant_color(image_path)
    cache_file.write_text(color)
    return color


def hyprctl_json(*args: str):
    return json.loads(subprocess.check_output(["hyprctl", *args, "-j"]))


def main() -> int:
    img = os.environ.get("HYPRMURAL_IMAGE")
    mon = os.environ.get("HYPRMURAL_MONITOR")
    if not img or not mon or not os.path.isfile(img):
        return 0

    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    STATE_DIR.mkdir(parents=True, exist_ok=True)

    color = cached_color(img)
    (STATE_DIR / f"accent-{mon}.rgb").write_text(color)

    monitors = hyprctl_json("monitors")
    clients = hyprctl_json("clients")

    id_to_color: dict[int, str] = {}
    for m in monitors:
        f = STATE_DIR / f"accent-{m['name']}.rgb"
        if f.exists():
            id_to_color[m["id"]] = f.read_text().strip()

    batch: list[str] = []
    for c in clients:
        c_color = id_to_color.get(c["monitor"])
        if not c_color:
            continue
        batch.append(
            f"dispatch setprop address:{c['address']} bordercolor rgb({c_color})"
        )

    # Global keywords (groupbar/border_active): focused-monitor follows-focus.
    focused = next((m for m in monitors if m.get("focused")), None)
    if focused and focused["id"] in id_to_color:
        fc = id_to_color[focused["id"]]
        batch.append(f"keyword group:col.border_active rgb({fc})")
        batch.append(f"keyword group:groupbar:col.active rgb({fc})")

    if batch:
        subprocess.run(
            ["hyprctl", "--batch", " ; ".join(batch)],
            stdout=subprocess.DEVNULL,
            check=False,
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
