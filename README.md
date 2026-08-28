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

Registers two docks — **Sources X-Ray** and **Sources X-Ray (Preview)**. When the scene one of them
follows contains scene sources, it lists them recursively — each subscene's children indented one
level beneath it, to arbitrary depth — with the standard visibility, lock, and reorder controls on
every row.

Changes made in the dock apply to the underlying scene, and therefore show up everywhere that scene is
used. That is the intended behaviour, not a side effect.

## Status

Feature-complete for v1.1. Each dock renders the nested contents of its scene as a live
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

### Two docks, not one panel

**Sources X-Ray** follows program — `obs_frontend_get_current_scene()`, which is the scene on air in
studio mode and simply the selected scene out of it. **Sources X-Ray (Preview)** follows
`obs_frontend_get_current_preview_scene()`, which exists only while studio mode is on.

Two registrations rather than one panel that switches, or one split down the middle, because it
hands OBS the entire layout problem. Each dock gets its own Docks-menu entry, its own saved position,
size, visibility and floating state, and can be tabbed or stacked with anything else. An operator who
only wants program closes preview and never thinks about it again. Nothing here has to invent a
splitter, persist its own geometry, or decide what to do when program and preview happen to be the
same scene — which in studio mode they are most of the time.

It also keeps each dock as simple as the single dock was. Two panes inside one widget would have
meant merging and de-duplicating two sets of signal watches, and checking *both* lists for a running
drag before any rebuild — the row-deleted-under-its-own-event-handler hazard, doubled. Separate docks
share no state at all.

Out of studio mode the preview dock has nothing to follow, so it shows one line — *Studio Mode is
disabled.* — and hides its toolbar and list entirely. These two are likely to be docked beside or
above each other, and the idle one should not be taking room from the one in use. The toolbar is
hidden rather than greyed out: a disabled row of buttons still costs the height, and there is nothing
for them to act on.

`obs_frontend_add_dock_by_id` has no per-module limit; the only constraint is a unique id
(`OBSBasic::IsDockObjectNameUsed`). The program dock keeps the id it has always had, so existing
saved layouts survive — the preview dock is purely additive.

### What each dock shows

Two views, switched by the **All** toggle at the left of each dock's toolbar and remembered across
restarts in the user config: `[XRay] ShowAllSources` for the program dock, `ShowAllSourcesPreview`
for the preview one. Kept separate on purpose — the full mirror on one and the focused view on the
other is a reasonable way to work, and a shared setting would make each dock fight the other.

**Off (default) — pruned.** Pruning is asymmetric. Above a subscene, only items on a path to one
appear: an ordinary source at the top level of the program scene gets no row, since the stock
Sources dock already lists it, and a group earns a row only if a subscene turned up beneath it. At
and below a subscene, everything is listed, because seeing inside the subscene is the point.

**On — mirrored.** Nothing above a subscene is pruned either, so the dock shows everything the
Sources dock shows and then carries on down into the nested scenes. The cost is a duplicate of a
list you already have when the scene is flat, which is why it is not the only behaviour. The gain is
that a group holding no nested scene stops silently vanishing — which reads as a missing feature
rather than as deliberate pruning, and was the first thing a tester hit.

Both views share one walk; the difference is a single flag (`list_all` in `walk_scene`) that starts
true in the mirrored view and only becomes true on entering a subscene in the pruned one.

A scene referenced twice is drawn twice; there is no deduplication. A scene that appears on its own
ancestor path is marked `(recursive)` and not descended into.

Selection follows OBS: picking a row in the stock Sources dock highlights the matching row here and
scrolls it into view. Only rows that exist in both lists can respond — in the pruned view a plain
source selected in the Sources dock has no row here, so nothing highlights. Scrolling happens only when the
selection actually moves, never on an incidental rebuild — turning **All** on removes that
limitation, since every row the Sources dock has then exists here too. The reverse direction is not
wired up:
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

## Installing

Grab the build for your platform from a
[release](https://github.com/FourCourtJester/OBS-Plugin-XRay/releases), or from the Artifacts of a
[CI run](https://github.com/FourCourtJester/OBS-Plugin-XRay/actions) if you want a build of an
unreleased branch. **Close OBS before copying anything in** — the module is loaded at startup and a
running OBS holds the file open on Windows.

### Windows

The zip contains an `obs-xray` folder already in the right shape. Drop it whole into:

```
C:\ProgramData\obs-studio\plugins\
```

so that you end up with:

```
C:\ProgramData\obs-studio\plugins\obs-xray\bin\64bit\obs-xray.dll
C:\ProgramData\obs-studio\plugins\obs-xray\data\locale\en-US.ini
```

Paste `%ProgramData%\obs-studio\plugins` into the Explorer address bar to get there; create the
`plugins` folder if it is not already present. No admin rights are needed and OBS updates leave it
alone.

**It is not `%APPDATA%`.** That is worth stating plainly because a lot of community guides say it:
`%APPDATA%\obs-studio\plugins` does not exist as a search path on Windows. OBS builds this path from
`CSIDL_COMMON_APPDATA`, which is `C:\ProgramData` — see `AddExtraModulePaths()` in
`frontend/widgets/OBSBasic.cpp`. `%APPDATA%` holds your *settings*, not your plugins.

Two cases where that path is wrong:

- **Portable mode.** `AddExtraModulePaths()` returns early before adding it, so a portable install
  never scans `C:\ProgramData`. Use the install directory instead (below).
- **Installing next to OBS itself.** That location works, but takes a *different* layout — the files
  are split rather than kept in one folder, and it needs admin rights:

  ```
  C:\Program Files\obs-studio\obs-plugins\64bit\obs-xray.dll
  C:\Program Files\obs-studio\data\obs-plugins\obs-xray\locale\en-US.ini
  ```

### macOS

Copy the `obs-xray.plugin` bundle to:

```
~/Library/Application Support/obs-studio/plugins/
```

Builds are **not signed or notarized**, so Gatekeeper will refuse to load the bundle. Clear the
quarantine flag after copying:

```sh
xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-xray.plugin
```

### Linux

The Ubuntu artifact is a `.deb`:

```sh
sudo apt install ./obs-xray-1.0.1-x86_64.deb
```

To install by hand instead, the per-user path is `~/.config/obs-studio/plugins/obs-xray/`, with the
same `bin/64bit` and `data` layout Windows uses.

### First run

The dock is registered docked to the right, and OBS remembers wherever you move it after that.

If it ever turns up as a floating panel stuck in the top-left corner of the screen with no title bar
and no way to drag it, that is **Lock UI**, and it is worth knowing why because it affects every
plugin dock, not just this one. `obs_frontend_add_dock_by_id()` finishes with `setFloating(true)`, so
a dock OBS has no saved position for is first shown floating; `OBSBasic::AddDockWidget` then gives a
new dock `NoDockWidgetFeatures` if Lock UI is on, which strips the header and makes it immovable.

Turn Lock UI off in the **Docks** menu, put the dock where you want it, and turn it back on. A saved
position survives upgrades — but note that launching OBS once *without* the plugin installed drops it,
because `QMainWindow::saveState()` only records docks that exist at the time.

### Checking it loaded

Start OBS and look for **Docks → Sources X-Ray**. If it is missing, the OBS log (Help → Log Files →
View Current Log) will say why — search for `obs-xray`. On a successful load it logs
`dock registered as 'obs-xray-subscene-sources'`. A version that is too old logs `requires OBS
30.0.0 or later` and stops there.

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

### Checking the context menu against OBS

`XRayRow`'s context menu is a hand-built copy of `OBSBasic::CreateSourcePopupMenu`. It has to be:
`OBSBasic` lives in the OBS executable rather than a linkable library, nothing in
`obs-frontend-api` exposes the menu, and every handler behind it resolves its own target through
`ui->sources` — the stock Sources dock, which by construction can never hold a nested item.

That copy is duplicated data, so there is a script to measure the drift:

```sh
git clone --depth 1 --branch 31.1.1 https://github.com/obsproject/obs-studio ../obs-studio
tools/compare-menus.py --obs ../obs-studio
```

It reads both menus straight out of the C++ and reports entries either side is missing, order
differences, and separator placement. `--dump` prints both menus as trees, which is what you want in
front of you when comparing the real thing side by side; `--all` also lists the differences we keep
on purpose. Exit status is 0 when only the expected differences remain.

Deliberate omissions and additions live in `EXPECTED_MISSING` / `EXPECTED_EXTRA` at the top of the
script, each with its reason. An entry there that stops differing is reported as stale, so the
allowlist cannot quietly rot into hiding real drift.

The comparison is on display strings, not locale keys — the two sides have different locale files,
and X-Ray's strings are copied from OBS's precisely so the menus read the same. Runs built by
runtime enumeration (input types, transition types, monitors, colour swatches) have no list in the
source to read; they are compared as `<<placeholders>>` and their contents are not checked.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
