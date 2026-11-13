# UI gallery (#215)

A Storybook-style gallery for the launcher. Each story is one launcher screen
or component in a fixed state. It is drawn by the real launcher code
(`HubWindow::Impl` / `Hub` in `launcher_gui.cpp`), not a copy of it. The same
stories feed three things:

- **`ui_gallery`**: an interactive window laid out like Storybook (see
  [The window](#the-window)).
- **`ui_gallery --capture OUT`**: a headless render of every story at every
  viewport into `OUT/shots/<story>/<viewport>.png`, with a contact sheet
  (`OUT/index.html`) and a lint report (`OUT/lint.md`).
- **The `ui_gallery_test` ctest**: capture, then compare with the checked-in
  `references/`, then lint. It fails on a picture or layout change, or on a
  new lint error.

Nothing needs a GPU or a display. Frames are drawn by a small CPU rasterizer
for ImGui draw lists (`soft_raster.cpp`, checked by `ui_gallery_raster_test`),
so the test runs the same on every CI runner. The gallery is fork-only.

## Build and run

```sh
cmake -B build -DPIKMIN_BUILD_LAUNCHER=ON -DPIKMIN_BUILD_TESTS=ON
cmake --build build --target ui_gallery
build/bin/ui_gallery                     # the window
build/bin/ui_gallery --list              # story names
build/bin/ui_gallery --capture /tmp/ui   # every story, every size
build/bin/ui_gallery --capture /tmp/ui --story 'settings/*' --viewport '800x600'
ctest --test-dir build -L ui-gallery --output-on-failure
```

The gallery never reads your real home folder. `HOME`, the XDG folders,
`USERPROFILE` and `APPDATA` point at a sandbox (`OUT/home`, or a temp folder
for the window), so no real saves, settings or downloads show up or get
touched. Buttons never fire. Pointer clicks are released off-screen, Enter and
Space are not forwarded, and dialogs that would block are shown as message
modals.

## The window

![The ui_gallery window](https://raw.githubusercontent.com/knakamura13/native-macos-pikmin/pr-assets/ui-gallery/ui-gallery/storybook-window.png)

The window's chrome lives in `tool/` (`storybook.cpp`, `widgets.cpp`,
`theme.cpp`, and Noto Sans in `tool/fonts/`, under the OFL). It is kept apart
from the stories and is never part of a capture. Only the tool is styled;
stories keep the launcher's own look.

- **Sidebar**: *Find components* filters stories as you type (press `/` to
  jump to it, `Esc` to clear it). The tree groups Screens and Components by
  area (Home, Move Install, Settings, Install, Dialogs, Components). Click a
  story, or use Up / Down, to open it. **Run story** / **Run all** at the
  bottom run what the ctest checks (layout lint, pointer and focus lint at
  1180x780, and the reference comparison) and show pass / fail / known
  counts. Each story that has been run gets a green or red dot.
- **Toolbar**: reload, zoom in / out / reset (fit; `Ctrl` + wheel zooms too),
  the window size (the capture matrix and the two phone screens), 1x / 2x
  display scale, the input the story gets (Mouse, Pad / keys, Touch), the
  canvas background (light, dark, checkerboard), *Outline and measure*
  (every widget box from the layout data, overlapping clickables in red, and
  the size of the box under the pointer), full screen (`Esc` leaves it) and
  **Save PNG** (full resolution, to the current folder).
- **Addon panel** (drag its top edge to resize it, or use the chevron to
  hide it):
  - **Controls**: the story's args. Edits re-render the story live.
    **Reset controls** goes back to the defaults. Tests and references always
    use the defaults.
  - **Visual tests**: the reference, the current picture and the difference,
    at the selected size. **Accept as new reference** writes the picture and
    layout into `references/` in the source tree (with the default controls
    only).
  - **Layout**: lint results for the frame on screen. The count on the tab
    turns red for problems and orange for known issues. Click a result to
    show its boxes on the canvas.
  - **Actions**: pointer downs, clicks (never run), focus changes and keys.
    Hover can be logged too.

Every tool control shows the hand cursor. Text fields show the text cursor,
and the splitters show resize cursors. The chrome is drawn with SDL_Renderer
at the screen's pixel density. On Windows and Linux screens above 120 DPI the
chrome scales up (`--ui-scale N` overrides this).

```sh
build/bin/ui_gallery --story 'hub/update-notice' --viewport 1440x900@2x
# A scripted screenshot of the window (used for the docs):
build/bin/ui_gallery --size 1440x900 --story hub/home --tab controls --screenshot window.png
```

### Story controls

A story with args gets controls. Defaults are what captures use:

```cpp
addArgs("dialog/replace-save", "Dialogs", "Replace a save",
        { textArg("Title", "Replace save?"), boolArg("Error", false),
          numberArg("Height", 44, 32, 72), choiceArg("Width", { "Small", "Medium" }, 1) },
        [](const Args& a) { Fixture f = /* ... */; f.messageTitle = argText(a, "Title"); return f; });
```

## Viewports

| name | window | notes |
|---|---|---|
| `800x600`, `1180x780`, `1280x720`, `1920x1080`, `2560x1440` | desktop, 1x | `1180x780` is the design size |
| `1180x780@2x`, `1440x900@2x` | desktop, 2x framebuffer | Retina |
| `phone-2400x1080`, `phone-1600x720` | Android scaling | landscape, 2.625 / 2.0 px per dp |

Component stories render at their own canvas size, at `1x` and `2x`.

## Adding a story

Add an entry to `makeStories()` in `stories.cpp`:

```cpp
add("settings/my-state", "Screens", "What this shows", [] {
    return withToast(settingsPage(1 /* Graphics */), "Something happened", 3.0f);
});
addComponent("components/my-row", "One row alone", 640, 120, [](ComponentCanvas& c) {
    c.at(24, 40);
    c.standardButton("Install", 1, 44);
});
```

A `Fixture` sets the hub state (installed games, page, tab, settings JSON,
release check, toast, modal, and so on). It is applied to the real `Hub`
before the first frame. Component stories draw through a `ComponentCanvas`,
which calls the launcher's own button and label code on a canvas of the given
size.

Then accept the new pictures:

```sh
build/bin/ui_gallery --capture /tmp/ui --references games/pikmin1/pc_port/launcher/ui_gallery/references \
    --update-references --known-issues games/pikmin1/pc_port/launcher/ui_gallery/known_issues.txt
```

## References and comparison

`references/<story>/<viewport>.png` is a 256 px wide thumbnail of each
capture. `.layout.txt` lists every clickable, label, toast, notice and the
logo, with rects rounded to whole points. The test fails when:

- more than 0.5% of thumbnail pixels change by more than 40 (of 255) in any
  channel, or
- a layout line changes kind, label or layer, or a coordinate moves by more
  than one point.

On failure, `OUT/failures/` gets `<key>.before.png`, `.after.png` and
`.diff.png` (changed pixels in red), plus both layout files. If the change is
intended, rerun with `--update-references` and commit the result. Only files
that really changed are rewritten.

## Layout lint

The lint runs on every capture. **Errors** fail the run unless they are
listed in `known_issues.txt`:

| rule | what |
|---|---|
| `overlap` | two clickables on the same layer overlap |
| `covers-logo` | a clickable on the hub overlaps the logo |
| `covered-by-toast` | a toast or notice sits on an enabled clickable |
| `truncated-toast` | toast text did not fit |
| `off-screen` | a clickable is partly outside the window (and does not scroll) |
| `clipped` | a clickable is cut by its window's clip rect (and does not scroll) |
| `truncated-label` | a button label did not fit, even when shrunk |
| `hover-blocked` | pointing at a control's centre hovers nothing (1180x780 only) |
| `no-hand-cursor` | an enabled control shows the arrow cursor (1180x780 only) |

**Notes** never fail. They are listed for the #214 audit:
`shrunk-label`, `shortened-path`, `small-touch-target` (under 48 dp on
phones), `focus-unreachable`, `no-focus-indicator` and `low-res` (a texture
drawn at under 0.95 texels per pixel, so it looks soft).

### Known issues

`known_issues.txt` lists real problems the gallery finds today, so the test
stays green until #214 fixes them:

```
rule | story glob | viewport glob | detail glob | note
covers-logo | * | * | 'What's new' covers the logo by * | #214: update pills overlap the logo
```

`lint.md` lists matched findings under "Known problems". Entries that no
longer match anything are listed under "Known issues that no longer occur",
so delete the line once the fix lands.

## Limits

- Only the desktop build of the launcher is compiled. `__ANDROID__` branches
  are not, so phone viewports use Android's scaling rule on the desktop
  layout.
- There is no network, cover art or slideshow art (covers show the
  placeholder). Native file pickers and folder openers do nothing.
- Settings pages use a real `nectar --settings-dump` from Pikmin 1
  (`fixtures/pikmin1-settings.json`), not a running game.
