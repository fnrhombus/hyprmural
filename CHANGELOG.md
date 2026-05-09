# Changelog

## [1.1.0](https://github.com/fnrhombus/hyprmural/compare/v1.0.0...v1.1.0) (2026-05-08)


### Features

* random wallpaper mode with in-place reshuffle ([#3](https://github.com/fnrhombus/hyprmural/pull/3))

  Two new config keys: `randomize = true|false` and `wallpaper_dir =
  <glob>` (POSIX glob(3), repeatable). When randomize is on, workspaces
  without an explicit pin draw from the pool. Numeric workspaces 1..9
  are pre-picked from a shuffled pool at startup; lazy picks for any
  other workspace fall through to a uniform-random draw. hyprmural
  writes `$XDG_RUNTIME_DIR/hyprmural/assignments.json` so consumers
  (waybar pill colors, etc.) can render full state without scraping
  per-fire hook env. `SIGUSR1` triggers an in-place reshuffle —
  recommended trigger:

  ```
  systemctl --user kill -s SIGUSR1 hyprmural.service
  ```

  Closes [#2](https://github.com/fnrhombus/hyprmural/issues/2).


### Miscellaneous

* automated releases via release-please ([#3](https://github.com/fnrhombus/hyprmural/pull/3))
