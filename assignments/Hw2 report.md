# Assignment 2 — Mesh Loading and Transformations

**Code:** [`nanorender/src/main.cpp`](../nanorender/src/main.cpp)

## Part 0 — GLM Demo

At startup, `glm_demo()` translates the point `(1,2,3)` by `+5` on the X axis using
`glm::translate` and a 4×4 matrix multiply, printing the result to the console to confirm GLM is
correctly linked and usable.

**What to capture (screenshot submitted separately):**

```
[GLM Demo] Translated (1,2,3) by +5 on X -> (6.0, 2.0, 3.0)
```
*(Copy-paste your actual console output here, or a screenshot of the terminal.)*

## Part 1 — OBJ Loading

`load_obj()` parses a simple Wavefront `.obj` file (`v` and `f` lines, with `f` supporting both
`f 1 2 3` and `f 1/1/1 2/2/2 3/3/3` formats, and fan-triangulating any quads/n-gons). If the file
can't be opened, a built-in fallback cube is used instead so the app never crashes on a missing
asset.

**What to capture (screenshot submitted separately):**

*(Screenshot: the "Mesh Info" panel showing the vertex/face count, alongside the wireframe
render of the loaded cube.)*

## Part 2 — Mesh Normalization

`normalize_mesh()` finds the mesh's bounding box, centers it at the origin, and uniformly
scales it so its largest dimension equals a target size — mapping any input model into a
comfortable, consistent size range regardless of the units it was authored in.

**What to capture (screenshot submitted separately):**

*(Screenshot: World Transform panel with all values at defaults/zero, showing the mesh centered
in the middle of the screen.)*

## Part 4 — Local/World Transform UI

Two separate panels — "Local Transform" and "World Transform" — each expose Translation,
Rotation (degrees), and Scale sliders on X/Y/Z, backed by independent state variables.

**What to capture (screenshot submitted separately):**

*(Two screenshots: the mesh visibly translated/rotated/scaled via the Local sliders in one, and
via the World sliders in the other, so the difference between the two is clear.)*

## Part 5 — Model Matrix Composition

The full Model matrix is composed as `World * Local`, where each of `World` and `Local` is
itself `Translation * Rotation * Scale`:

```cpp
glm::mat4 Local = Ltrans * Lrot * Lscale;
glm::mat4 World = Wtrans * Wrot * Wscale;
glm::mat4 M = World * Local;
```

**What to capture (screenshot submitted separately):**

*(Screenshot: both Local and World sliders set to non-default values simultaneously, showing
both transforms compose correctly.)*

## Part 6 — Arrow-Key World Translation

Holding the arrow keys nudges the mesh's world-space position every frame (`key_tx`, `key_ty`),
added on top of whatever the World Translation sliders are set to — shown live in the World
Transform panel as "Arrow keys offset: TX=... TY=...". A "Reset Key Offset" button zeroes it.

**What to capture (screenshot submitted separately):**

*(Screenshot: the mesh shifted away from center after holding an arrow key, with the "Arrow
keys offset" label visible showing non-zero values.)*
