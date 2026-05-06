#!/usr/bin/env python3
"""hyprmural hook — per-output Vibrant accent borders.

Wired via hyprmural.conf:
    hook = ~/.config/hyprmural/accent.py

Hyprmural fires this on each per-output image change with HYPRMURAL_MONITOR
/ HYPRMURAL_WORKSPACE / HYPRMURAL_IMAGE in the env. Behavior:

1. Vibrant-style multi-swatch extraction on the image (cached by path+mtime
   under XDG_CACHE_HOME/hyprmural). Uses the `vibrant` slot for the border;
   workspace pill colors live in pill-accents.py and consume the full dict.
2. Stash the per-monitor color to runtime state.
3. Iterate `hyprctl clients` and push `setprop bordercolor` per window —
   each window gets the color of *its* monitor, yielding genuinely
   per-output borders. Hyprland's keyword-level border colors are global,
   so we use per-window setprop instead.
4. For groupbar slots (truly global), focused-monitor wins.

New windows opened between hook fires get the global default until the
next workspace event.
"""
from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys

# Allow `from vibrant import ...` whether installed in the same dir as a
# module or run as a one-off script.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vibrant import accent_for_bg, cached_swatches, resolve  # noqa: E402

STATE_DIR = pathlib.Path(
    os.environ.get("XDG_RUNTIME_DIR", "/tmp")
) / "hyprmural"


def hyprctl_json(*args: str):
    return json.loads(subprocess.check_output(["hyprctl", *args, "-j"]))


def main() -> int:
    img = os.environ.get("HYPRMURAL_IMAGE")
    mon = os.environ.get("HYPRMURAL_MONITOR")
    if not img or not mon or not os.path.isfile(img):
        return 0

    STATE_DIR.mkdir(parents=True, exist_ok=True)

    # Use the same contrast-corrected accent the pill picks against this
    # workspace's wallpaper-bg. Keeps the focused-window border visually
    # in sync with the pill on the bar.
    swatches = cached_swatches(img)
    bg = resolve(swatches, "bg", "dark_muted", "muted", default="333333")
    color = accent_for_bg(swatches, bg)
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
