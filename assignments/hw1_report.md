# Assignment 1 — Wireframe Engine Foundations (Interactive UI & Input Basics)

**Code:** [`nanorender/src/main.cpp`](../nanorender/src/main.cpp), [`nanorender/src/ui_renderer.cpp`](../nanorender/src/ui_renderer.cpp)

## Part 1 — Creative Background Pattern

The background is filled every frame with a pattern based on both `x` and `y` (not a solid
color, not a 1D strip): concentric rings around the window center, colored by distance from
center combined with separate sine terms on `x` and `y`, so the result has genuine 2D structure
rather than repeating along only one axis.

```cpp
float dist = sqrtf(dx * dx + dy * dy);
uint8_t r = (uint8_t)(20 + 25 * sinf(dist * g_bg_ring_scale + g_bg_phase));
uint8_t g = (uint8_t)(18 + 20 * sinf(x * 0.008f - g_bg_phase * 0.7f));
uint8_t b = (uint8_t)(32 + 28 * cosf(y * 0.008f + g_bg_phase * 1.3f));
```

**What to capture (screenshot submitted separately):**

*(Screenshot: the app at startup, before enabling any 3D debug overlays, showing the rings clearly.)*

## Part 2 — MicroUI Widget Demo

A small widget demo in the "HW1: Background & Drawing" panel: a button that prints mesh info to
the console when clicked, and a checkbox that toggles a label on/off — demonstrating declaring
and reading back state from a MicroUI widget.

**What to capture (screenshot submitted separately):**

*(Screenshot: the panel with the "Show demo label" checkbox checked, showing the label, plus
a screenshot or copy-paste of the console output after clicking "Print Mesh Info to Console".)*

## Part 3 — Custom Keyboard Input Callback

The character-input callback was extended to intercept `r`/`R` specifically: instead of being
forwarded to the UI (where it would type into a text field), it reshuffles the background
pattern's phase (`g_bg_phase`) to a new random value. Every other key still passes through to
`ui_bridge_char_input` unchanged.

```cpp
if (c == 'r' || c == 'R') {
    g_bg_phase = (float)(rand() % 1000) * 0.01f; // consumed, not forwarded
    return;
}
```

**What to capture (screenshot submitted separately):**

*(Two screenshots: the background pattern before and after pressing R, showing it visibly changed.)*

## Part 4 — Wave Visual Offset

Implemented in `ui_renderer.cpp`: every pixel written by `draw_rect` (and the matching text) is
displaced horizontally by `sin(y * 0.05) * 6`, giving the UI panels a subtle horizontal wave
effect per row.

**What to capture (screenshot submitted separately):**

*(Screenshot: any UI window, zoomed in enough to see the wavy horizontal displacement on its
edges/text.)*

## Part 5 — Binding UI State to the Background Pattern

Two sliders — "Ring scale" and "Phase" — in the "HW1: Background & Drawing" panel are bound
directly to `g_bg_ring_scale` and `g_bg_phase`, the same variables the Part 1 background loop
reads every frame. Moving either slider visibly changes the pattern live.

**What to capture (screenshot submitted separately):**

*(Two screenshots: the background at a low vs. high "Ring scale" slider value, same phase, to
show the slider's effect clearly.)*

## Part 6 — Interactive Line Drawing Tool

A full click-drag-release line tool, toggled via the "Drawing Mode" checkbox: pressing the
mouse button starts a line at that point, dragging shows a live white preview line, and
releasing commits it permanently (in the color set by the R/G/B sliders) into a list that's
redrawn every frame. A "Clear Lines" button empties the list.

**Design choice:** click-drag-release rather than click-click, because it ties "a line is in
progress" directly to whether the mouse button is physically held down — there's no ambiguous
in-between state to accidentally leave dangling (see the comment above the state declaration in
`main.cpp` for the full reasoning).

**What to capture (screenshot submitted separately):**

*(Screenshot: a few drawn lines in different colors, ideally mid-drag showing the white preview
line too.)*
