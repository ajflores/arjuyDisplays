# arjuy-displays

A desktop app for Omarchy (Hyprland) to see and, in later phases, arrange your monitors like in Windows.

**This version is a preview.** It draws your monitors to scale, lets you drag them around (with magnetic edges, no overlaps and no gaps) and warns about layout problems, but it never applies anything: no configuration is written and Hyprland is not reloaded. Applying the changes comes in a later phase.

## Arranging (preview only)

- Drag a monitor on the map. Its edges snap to the other monitors' edges (side by side, top to bottom, and aligned edges, within about 10 px of the map). Guides and the coordinates show while dragging.
- On release the monitor is pushed to the nearest valid spot: no overlap, touching another monitor, whole-number positions.
- Moved monitors show a thin outline where they really are, a blue banner *Unsaved changes — not applied* lists old and new positions, and the details panel shows the current and the pending position. `Esc` or *Reset* goes back to the real positions; `Ctrl+Z` / `Ctrl+Shift+Z` undo and redo.
- Layout checks run live, also while dragging: gaps between monitors (with the likely cause, for example a scaled monitor that is narrower than its pixel width), overlaps, monitors the cursor cannot reach, separate groups, and, as information only, edges that touch only partially (different heights). Gaps and overlaps are marked on the map.
- Quitting (`Q` or closing the window) asks first when there are unsaved changes.

## Workspaces (read only)

Each monitor on the map shows small tags for its workspaces: id (or name) and window count. The one shown now is filled with the theme accent, a diamond marks a persistent workspace, empty ones are dimmed, and a warning sign marks a *guest* (its id is outside the monitor's Omarchy block); if they do not fit, the rest is summed up as `+N`. They move with the monitor while you drag it. The details panel lists all of them with their Omarchy block (Omarchy gives every physical monitor a block of ten: 1-10, 11-20, ...; it reads `~/.local/state/omarchy/monitor-workspaces`, read only; if it is missing or not understood the block shows as unknown).

A *Workspace notes* banner explains, as information only (never an error), when a fixed workspace rule pins a workspace to a monitor other than the owner of its Omarchy block, when workspaces with windows sit outside their monitor's block, when a rule points to a monitor that is not connected, or when an id is duplicated. `special:*` workspaces are kept apart. Workspaces are never edited here; the data comes from `hyprctl -j workspaces`, `workspacerules` and `monitors`, and updates live with Hyprland's workspace events.

## Shortcuts

| Key | Action |
| --- | --- |
| `?` | Show or hide the shortcuts |
| `I` | Identify: show each monitor's name in a big banner on its own screen (about 3 s; `Esc` or a click closes it) |
| `R` / `F5` | Refresh the monitors |
| `Tab` / `Shift+Tab` | Select another monitor |
| Drag | Move a monitor (preview only) |
| Arrows | Move the selected monitor 10 px (`Shift` 100 px, `Ctrl` 1 px) |
| `Ctrl+Z` / `Ctrl+Shift+Z` | Undo / redo a move |
| `Esc` | Reset to the real positions (or cancel a drag) |
| `L` | Change the interface language: Auto, English, Español (live, saved) |
| `Q` | Quit (asks if there are unsaved changes) |

The layout also refreshes by itself when a monitor is added or removed or the Hyprland config reloads.

## Language

The top bar has a **Language** button (menu: Auto, English, Español); `L` cycles through them. The change is live: every text, including warnings, workspace notes, errors and the `check` output, switches without restarting. *Auto* (the default) follows Omarchy's language (`~/.config/omarchy/language`, read only, followed live); English and Español force it. The choice is saved in `~/.local/state/arjuy-displays/settings.json` (written atomically; a missing or corrupt file means Auto). Omarchy's own language setting is never touched.

Catalogs are `src/i18n/en.json` (plural forms) and `src/i18n/es.json`; the test suite fails if any `qsTr()`/`tr()` string has no Spanish entry, if a placeholder differs, or if a catalog has an entry nothing uses. Set `ARJUY_SETTINGS` / `ARJUY_OMARCHY_LANGUAGE` to use other files (the tests do).

## Command line

```sh
arjuy-displays check --json   # prints the monitors, versions, warnings, layout issues, workspaces and workspace notes; exit 1 on error
arjuy-displays open --lang es # forces the language for this run only (auto|en|es); nothing is saved. Works with check too
arjuy-displays identify       # shows the identify banners on every screen, then exits
```

Identify needs `layer-shell-qt` (overlay on top of everything, no focus stolen); without it the banners are normal windows.

Tested with Hyprland 0.56.x and Omarchy 4.5.x; other versions show a read-only warning.

## Build and test

```sh
bin/build     # qmake6 + make into build/arjuy-displays
bin/test      # Qt Test suite, offscreen, with a fake hyprctl
bin/install   # makepkg -fsi, adds the app to the launcher
```

Set `ARJUY_HYPRCTL` to use another `hyprctl` (the tests do).

MIT license.
