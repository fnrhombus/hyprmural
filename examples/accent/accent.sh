#!/usr/bin/env bash
# hyprmural hook → matugen-driven per-workspace accent colors.
#
# Wire up via hyprmural.conf:
#   hook = ~/.config/hyprmural/accent.sh
#
# Hyprmural fires this on each per-output image change with:
#   HYPRMURAL_MONITOR    e.g. "eDP-1"
#   HYPRMURAL_WORKSPACE  e.g. "5" or "special:scratchpad"
#   HYPRMURAL_IMAGE      absolute path to the image now showing
#
# Behavior:
#   1. Run matugen with the slim per-workspace config to regenerate
#      ~/.config/themes/accent.{css,conf,ini} from $HYPRMURAL_IMAGE.
#      (Matugen's own post_hooks reload waybar + swaync.)
#   2. Parse the rendered accent.conf to extract $primary/$tertiary/
#      $primary_container/$on_primary_container/$error.
#   3. Push live Hyprland keywords for borders + groupbar via
#      `hyprctl --batch` — fast (no reload).
#
# Multi-monitor policy: any per-output change triggers an update; the
# last monitor's colors win for the global Hyprland keywords. If you
# want focused-monitor-only, gate on `hyprctl activeworkspace -j`.

set -euo pipefail

CONFIG="${HYPRMURAL_MATUGEN_CONFIG:-$HOME/.config/hyprmural/matugen-per-workspace.toml}"
ACCENT_CONF="${HOME}/.config/themes/accent.conf"

[[ -n "${HYPRMURAL_IMAGE:-}" ]] || { echo "accent.sh: HYPRMURAL_IMAGE not set" >&2; exit 1; }
[[ -r "$HYPRMURAL_IMAGE" ]] || { echo "accent.sh: image unreadable: $HYPRMURAL_IMAGE" >&2; exit 1; }

# 1. Regenerate accent.{css,conf,ini}
matugen image --config "$CONFIG" --source-color-index 0 -q "$HYPRMURAL_IMAGE"

# 2. Parse accent.conf for the M3 slot values matugen rendered.
declare -A v
while IFS= read -r line; do
    [[ "$line" =~ ^\$([a-z_]+)[[:space:]]*=[[:space:]]*(rgb\([0-9A-Fa-f]+\))[[:space:]]*$ ]] || continue
    v[${BASH_REMATCH[1]}]="${BASH_REMATCH[2]}"
done < "$ACCENT_CONF"

# 3. Live push to Hyprland — borders + groupbar. Mirrors the slot-to-keyword
#    mapping from borders.conf so a no-reload update is visually equivalent
#    to `hyprctl reload`. Add or remove keywords here to taste.
hyprctl --batch "
keyword general:col.active_border ${v[primary]} ${v[tertiary]} 45deg ;
keyword group:col.border_active ${v[primary]} ${v[tertiary]} 45deg ;
keyword group:col.border_locked_active ${v[error]} ;
keyword group:groupbar:col.active ${v[primary_container]} ;
keyword group:groupbar:text_color ${v[on_primary_container]} ;
" >/dev/null
