# arjuy-displays

A desktop app for Omarchy (Hyprland) to see and, in later phases, arrange your monitors like in Windows.

**This version is a preview.** It draws your monitors to scale, lets you drag them around (with magnetic edges, no overlaps and no gaps) and warns about layout problems, but it never applies anything: no configuration is written and Hyprland is not reloaded. Applying the changes comes in a later phase.

## Arranging (preview only)

- Drag a monitor on the map. Its edges snap to the other monitors' edges (side by side, top to bottom, and aligned edges, within about 10 px of the map). Guides and the coordinates show while dragging.
- On release the monitor is pushed to the nearest valid spot: no overlap, touching another monitor, whole-number positions.
- Moved monitors show a thin outline where they really are, a blue banner *Unsaved changes — not applied* lists old and new positions, and the details panel shows the current and the pending position. `Esc` or *Reset* goes back to the real positions; `Ctrl+Z` / `Ctrl+Shift+Z` undo and redo.
- Layout checks run live, also while dragging: gaps between monitors (with the likely cause, for example a scaled monitor that is narrower than its pixel width), overlaps, monitors the cursor cannot reach, separate groups, and, as information only, edges that touch only partially (different heights). Gaps and overlaps are marked on the map.
- Quitting (`Q` or closing the window) asks first when there are unsaved changes.

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
| `Q` | Quit (asks if there are unsaved changes) |

The layout also refreshes by itself when a monitor is added or removed or the Hyprland config reloads.

## Command line

```sh
arjuy-displays check --json   # prints the monitors, versions, warnings and layout issues; exit 1 on error
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
