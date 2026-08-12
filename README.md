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

Registers a dock named **Sources X-Ray**. When the program scene contains scene sources, the dock
lists them recursively — each subscene's children indented one level beneath it, to arbitrary depth —
with the standard visibility, lock, and reorder controls on every row.

Changes made in the dock apply to the underlying scene, and therefore show up everywhere that scene is
used. That is the intended behaviour, not a side effect.

## Status

Feature-complete for v1.0. The dock renders the nested contents of the program scene as a live
indented list with visibility, lock, expand/collapse, inline rename and drag-to-reorder, using OBS's
own source icons and themed controls throughout. Branches start collapsed, with collapse-all and
expand-all in the dock's toolbar.

| Phase | | |
|---|---|---|
| 1 | Stub dock, registered on `FINISHED_LOADING` | done |
| 2 | Read-only recursive walk with pruning | done |
| 3 | Live updates from per-scene signals | done |
| 4 | Visibility and lock toggles | done |
| 5 | Within-scene reorder | done |
| 6 | Visual parity with the Sources dock | done |

### Theme integration

The rows ship no icon assets. Source icons are read from the `Q_PROPERTY` set that OBS's theme
populates on the main window — `cameraIcon`, `textIcon`, `sceneIcon` and friends — reached through
Qt's meta-object system rather than by linking `OBSBasic`. The eye and lock are plain `QCheckBox`es
carrying the same `class` properties OBS uses, `checkbox-icon indicator-visibility` and
`checkbox-icon indicator-lock`, which are Qt class selectors, so the active theme paints them.

Expand/collapse is the same again — a `.indicator-expand` checkbox in the row, which is how OBS does
it too. OBS's Sources list is flat, not a tree widget, and the themes carry no `QTreeView` rules at
all, so a real tree widget would have looked *less* like OBS rather than more.

Collapsed state is stored on the scene item's private settings under `xray_collapsed`, and
deliberately **not** under `collapsed`, which is OBS's own key for group collapse. Branches here
default to collapsed so a deep tree opens tidy, and defaulting through OBS's key would have
collapsed the operator's groups in their real Sources dock. The trade is that the two lists no
longer agree about group collapse; unset means collapsed, and once a row is toggled that choice
persists with the scene collection.

All of it follows the user's theme automatically, including themes that do not exist yet.

### What the dock shows

Pruning is asymmetric. Above a subscene, only items on a path to one appear — an ordinary source at
the top level of the program scene gets no row, since the stock Sources dock already lists it, and a
group earns a row only if a subscene turned up beneath it. At and below a subscene, everything is
listed, because seeing inside the subscene is the point.

A scene referenced twice is drawn twice; there is no deduplication. A scene that appears on its own
ancestor path is marked `(recursive)` and not descended into.

Selection follows OBS: picking a row in the stock Sources dock highlights the matching row here and
scrolls it into view. Only rows that exist in both lists can respond — a plain source selected in
the Sources dock is pruned out of this one, so nothing highlights. Scrolling happens only when the
selection actually moves, never on an incidental rebuild. The reverse direction is not wired up:
clicking a row here does not select it in OBS, since a nested selection cannot reach the preview.

Rows can be dragged to reorder, but only among rows sharing their owning scene — rows at different
depths belong to different scenes, so a position "between" them does not exist. Candidate drop points
are filtered to matching owners, so the drop indicator only ever appears where a release will
actually land.

### Dock registration timing

The dock is registered in `obs_module_post_load()`, not on `OBS_FRONTEND_EVENT_FINISHED_LOADING`.
That matters: in `OBSBasic::OBSInit` the order is modules loaded → `obs_post_load_modules()` →
`restoreState()` of the saved dock layout → and only much later `FINISHED_LOADING`. A dock created
on `FINISHED_LOADING` does not exist when the layout is restored, so Qt cannot place it and it comes
up hidden and floating on every launch. Registering in post-load lands before the restore, so
position and visibility persist.

Only registration happens there. No scene collection is loaded yet, so the contents are filled in on
`FINISHED_LOADING`.

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

## Tests

`SceneWalk` is the only Qt-free part of the plugin, and `tests/test_scene_walk.cpp` exercises it
against a stand-in libobs defined in the test file itself. No OBS installation is needed and nothing
has to be running — each case builds a scene graph by hand.

```sh
cmake --preset ubuntu-x86_64 -DENABLE_TESTS=ON
cmake --build --preset ubuntu-x86_64
ctest --test-dir build_x86_64 --output-on-failure
```

Off by default and not wired into CI. The harness supplies its own libobs symbols, which is fine
where those are plain functions but is at best warning-prone under MSVC, where `obs.h` marks them
`__declspec(dllexport)` unconditionally — untested there, so it stays opt-in rather than risking the
Windows build.

What it covers is the part that is easy to get quietly wrong and hard to spot in the UI: asymmetric
pruning, which containers get watched, the ownership of items inside groups, and the ordering
arithmetic behind a drag.

What it does not cover is whether the assumptions about libobs are right. Every assumption the
plugin makes is also baked into the stubs, so a wrong one passes here and fails in OBS — which is
exactly how the display-order bug survived until it was seen on screen. When a case disagrees with
OBS, the stub is what is wrong.

## CI

Workflows build Windows, macOS, and Ubuntu, and check formatting with `clang-format` and `gersemi`.
They run on pushes to `master`/`main`/`release/**`, on tags, on pull requests, and via manual
**workflow_dispatch** — pushing a topic branch on its own does not trigger a build. `check-format`
additionally only runs on `master`/`main` pushes, so a `release/**` push skips it; to exercise it on
another branch, dispatch the **Pull Request** workflow against that branch.

### Formatter versions

Match the versions CI pins, or you will chase differences that are not real:

| Tool | Version |
|---|---|
| `clang-format` | 19.1.1 |
| `gersemi` | 0.21.0 |

gersemi 0.28 in particular reformats argument lists differently and will flag files CI accepts.

The two format actions run `brew update` before installing. GitHub runners set
`HOMEBREW_NO_AUTO_UPDATE=1` and ship a Homebrew that can be weeks stale, and the `obsproject/tools`
formulae are written against current Homebrew — without the update, `brew install` fails to parse
the formula and the job dies with `unknown install step: run` before reading a single source file.
That failure is inherited from `obs-plugintemplate` and affects any plugin generated from it.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
