# XRay

An OBS Studio dock that shows you what is inside the scenes nested in your current scene.

## The problem

OBS gives no way to see inside a scene used as a source within another scene. If Scene A contains
Scene B as a source, the Sources dock shows one row labelled "Scene B" and you have to trust that its
children are configured and visible as intended. Verifying it means leaving the current scene, opening
Scene B, checking, and coming back — during a live show.

OBS already does this for groups, which expand inline in the Sources dock. Groups are scenes
internally; scene sources just do not get the same treatment. XRay adds it.

## What it does

Registers a dock named **SubScene Sources**. When the program scene contains scene sources, the dock
lists them recursively — each subscene's children indented one level beneath it, to arbitrary depth —
with the standard visibility, lock, and reorder controls on every row.

Changes made in the dock apply to the underlying scene, and therefore show up everywhere that scene is
used. That is the intended behaviour, not a side effect.

## Status

Early. The dock registers under **View → Docks** and renders the nested contents of the program
scene as a live indented list — it follows adds, removals, reorders, renames and scene cuts as they
happen. Nothing in it is interactive yet: the rows are display only.

| Phase | | |
|---|---|---|
| 1 | Stub dock, registered on `FINISHED_LOADING` | done |
| 2 | Read-only recursive walk with pruning | done |
| 3 | Live updates from per-scene signals | done |
| 4 | Visibility and lock toggles | next |
| 5 | Within-scene reorder | |
| 6 | Visual parity with the Sources dock | |

### What the dock shows

Pruning is asymmetric. Above a subscene, only items on a path to one appear — an ordinary source at
the top level of the program scene gets no row, since the stock Sources dock already lists it, and a
group earns a row only if a subscene turned up beneath it. At and below a subscene, everything is
listed, because seeing inside the subscene is the point.

A scene referenced twice is drawn twice; there is no deduplication. A scene that appears on its own
ancestor path is marked `(recursive)` and not descended into.

## Requirements

- OBS Studio 30.0.0 or later. `obs_frontend_add_dock_by_id()` does not exist before 30.0, so the module
  refuses to load below that and logs why.
- Built against OBS 31.1.1 headers and Qt 6.8.3 (obs-deps `2025-07-11`).

Note that OBS and the plugin share one Qt instance in-process, so a Qt ABI mismatch is undefined
behaviour that cannot be detected at runtime — only prevented at build time. Expect per-OBS-major
builds rather than one universal binary.

## Building

| Platform | Toolchain |
|---|---|
| Windows | Visual Studio 17 2022, CMake 3.30.5 |
| macOS | Xcode 16.0, CMake 3.30.5 |
| Ubuntu 24.04 | CMake 3.28.3, `ninja-build`, `pkg-config`, `build-essential` |

The build follows [`obs-plugintemplate`](https://github.com/obsproject/obs-plugintemplate) — see its
[wiki](https://github.com/obsproject/obs-plugintemplate/wiki) for environment setup. `ENABLE_QT` and
`ENABLE_FRONTEND_API` are both `ON`; this is a dock plugin and neither is optional.

## CI

Workflows build Windows, macOS, and Ubuntu, and check formatting with `clang-format` and `gersemi`.
They run on pushes to `master`/`main`/`release/**`, on tags, on pull requests, and via manual
**workflow_dispatch** — pushing a topic branch on its own does not trigger a build.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
