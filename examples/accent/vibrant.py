"""Vibrant.py-style multi-swatch extraction with on-disk caching.

Shared by accent.py (per-output borders, uses just `vibrant`) and
pill-accents.py (workspace pill colors, uses the full swatch dict).

extract_swatches() returns up to 7 keys:

  vibrant        — high saturation, mid lightness  (the loud accent)
  light_vibrant  — high saturation, light          (lighter accent)
  dark_vibrant   — high saturation, dark           (deep accent)
  muted          — low saturation, mid lightness   (medium pill fill)
  light_muted    — low saturation, light           (subtle bg)
  dark_muted     — low saturation, dark            (restful pill fill)
  on             — black or white, picked by luminance contrast against `vibrant`

Any swatch may be absent if the image lacks pixels in its (s, l) target
window. Callers should fall back gracefully (`vibrant` -> `light_vibrant`
-> `muted` -> neutral).

Cache is keyed by sha256(image_path + "\\0" + mtime) under
$XDG_CACHE_HOME/hyprmural/<key>.json.
"""
from __future__ import annotations

import hashlib
import json
import os
import pathlib

CACHE_DIR = pathlib.Path(
    os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache"))
) / "hyprmural"


# (target_s, target_l, min_s, max_s, min_l, max_l)
_TARGETS = {
    "vibrant":       (1.0, 0.50, 0.35, 1.00, 0.30, 0.70),
    "light_vibrant": (1.0, 0.74, 0.35, 1.00, 0.55, 0.95),
    "dark_vibrant":  (1.0, 0.26, 0.35, 1.00, 0.05, 0.45),
    "muted":         (0.3, 0.50, 0.00, 0.40, 0.30, 0.70),
    "light_muted":   (0.3, 0.74, 0.00, 0.40, 0.55, 0.95),
    "dark_muted":    (0.3, 0.26, 0.00, 0.40, 0.05, 0.45),
}


def _on_color(hex_rgb: str) -> str:
    r, g, b = int(hex_rgb[0:2], 16), int(hex_rgb[2:4], 16), int(hex_rgb[4:6], 16)
    lum = 0.299 * r + 0.587 * g + 0.114 * b  # Rec.601 perceptual
    return "000000" if lum > 140 else "ffffff"


def extract_swatches(path: str) -> dict[str, str]:
    """Return {swatch_name: 'rrggbb', ...}. Missing swatches are absent.

    Single-pass over a downsampled (96x54) RGB-quantized (5x5x5) histogram,
    scoring each populated bin against every target and tracking the best
    hit per swatch.
    """
    from PIL import Image  # delayed — only on cache miss

    im = Image.open(path).convert("RGB").resize((96, 54))
    raw = im.tobytes()

    counts = [0] * 125
    for i in range(0, len(raw), 3):
        qr = min(4, raw[i] * 5 // 256)
        qg = min(4, raw[i + 1] * 5 // 256)
        qb = min(4, raw[i + 2] * 5 // 256)
        counts[qr * 25 + qg * 5 + qb] += 1

    max_count = max(counts) or 1
    best: dict[str, tuple[tuple[int, int, int] | None, float]] = {
        name: (None, -1.0) for name in _TARGETS
    }

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
            s = 0.0
        else:
            d = mx - mn
            s = d / (2 - mx - mn) if l > 0.5 else d / (mx + mn)
        pop_match = pop / max_count

        for name, (ts, tl, min_s, max_s, min_l, max_l) in _TARGETS.items():
            if s < min_s or s > max_s or l < min_l or l > max_l:
                continue
            sat_match = 1.0 - abs(s - ts)
            lit_match = 1.0 - abs(l - tl) * 2.0
            score = 3.0 * sat_match + 1.0 * lit_match + 1.0 * pop_match
            if score > best[name][1]:
                best[name] = ((int(r * 255), int(g * 255), int(b * 255)), score)

    result: dict[str, str] = {}
    for name, (rgb, _) in best.items():
        if rgb is not None:
            result[name] = f"{rgb[0]:02x}{rgb[1]:02x}{rgb[2]:02x}"

    if "vibrant" in result:
        result["on"] = _on_color(result["vibrant"])
    elif "light_vibrant" in result:
        result["on"] = _on_color(result["light_vibrant"])

    # `bg` — the wallpaper's "background" color, for pill backgrounds that
    # mirror the wallpaper's overall character (white-dominant wallpapers
    # → light pills, dark wallpapers → dark pills). Most populous bin
    # whose saturation is below 0.3 (i.e. visually neutral). If none
    # qualify, falls through to the most-populous-overall bin.
    best_bg_rgb: tuple[int, int, int] | None = None
    best_bg_pop = -1
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
            s = 0.0
        else:
            d = mx - mn
            s = d / (2 - mx - mn) if l > 0.5 else d / (mx + mn)
        if s > 0.30:
            continue
        if pop > best_bg_pop:
            best_bg_pop = pop
            best_bg_rgb = (int(r * 255), int(g * 255), int(b * 255))
    if best_bg_rgb is None:
        # No neutral bins — fall back to whatever bin is most populous overall
        top_bin = max(range(125), key=lambda i: counts[i])
        qr, qg, qb = (top_bin // 25) % 5, (top_bin // 5) % 5, top_bin % 5
        best_bg_rgb = (
            int(((qr + 0.5) / 5.0) * 255),
            int(((qg + 0.5) / 5.0) * 255),
            int(((qb + 0.5) / 5.0) * 255),
        )
    result["bg"] = f"{best_bg_rgb[0]:02x}{best_bg_rgb[1]:02x}{best_bg_rgb[2]:02x}"

    return result


def cached_swatches(path: str) -> dict[str, str]:
    """Vibrant-extract `path`, cached by (path, mtime). JSON on disk."""
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    mtime = int(os.stat(path).st_mtime)
    key = hashlib.sha256(f"{path}\x00{mtime}".encode()).hexdigest()[:16]
    cache_file = CACHE_DIR / f"{key}.json"
    if cache_file.exists():
        try:
            return json.loads(cache_file.read_text())
        except (json.JSONDecodeError, OSError):
            pass  # fall through and re-extract
    swatches = extract_swatches(path)
    cache_file.write_text(json.dumps(swatches))
    return swatches


def resolve(swatches: dict[str, str], *fallback_chain: str, default: str = "808080") -> str:
    """Pick the first present swatch from `fallback_chain`, else `default`."""
    for name in fallback_chain:
        if name in swatches:
            return swatches[name]
    return default


def relative_luminance(hex_rgb: str) -> float:
    """WCAG relative luminance, sRGB → linear → weighted sum."""
    def _lin(c: float) -> float:
        return c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    r = _lin(int(hex_rgb[0:2], 16) / 255)
    g = _lin(int(hex_rgb[2:4], 16) / 255)
    b = _lin(int(hex_rgb[4:6], 16) / 255)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast_ratio(a: str, b: str) -> float:
    """WCAG contrast ratio between two hex colors."""
    la, lb = relative_luminance(a), relative_luminance(b)
    lighter, darker = max(la, lb), min(la, lb)
    return (lighter + 0.05) / (darker + 0.05)


def accent_for_bg(swatches: dict[str, str], bg: str, target: float = 4.5) -> str:
    """Pick the wallpaper-derived accent variant with the best contrast vs `bg`.

    Considers all six Vibrant slots (vibrant + dark/light variants and the
    three muted variants) and picks the one with highest contrast against
    `bg`. If none clears `target` (WCAG-AA = 4.5:1), falls back to black or
    white based on `bg` luminance.

    Used by both pill-accents.py (text/border on a pill bg) and accent.py
    (per-window border on the wallpaper bg) so the same workspace accent
    drives the pill and the focused window outline together.
    """
    candidates = [
        swatches.get(k) for k in (
            "vibrant", "dark_vibrant", "light_vibrant",
            "muted", "dark_muted", "light_muted",
        )
    ]
    candidates = [c for c in candidates if c]
    if candidates:
        best = max(candidates, key=lambda c: contrast_ratio(c, bg))
        if contrast_ratio(best, bg) >= target:
            return best
    return "000000" if relative_luminance(bg) > 0.40 else "ffffff"
