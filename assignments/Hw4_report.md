# Assignment 4 — Rasterization and Hidden Surface Removal

**Code:** [`nanorender/src/main.cpp`](../nanorender/src/main.cpp)

## Part 1 — Bounding-Box Rasterization (Debug)

Before implementing a proper inside/outside test, each triangle's 2D screen-space bounding
rectangle is filled solid with a random color per face (`draw_triangle_bbox_debug`), toggled via
the "BBox Rasterization (debug)" checkbox. This sanity-checks the projection pipeline — every
triangle should produce a plausible rectangle roughly where the object is on screen — before
trusting the real fill.

**What to capture (screenshot submitted separately):**

*(Screenshot: "BBox Rasterization (debug)" checked, showing the overlapping colored rectangles
described in the assignment.)*

## Part 2 — Barycentric Triangle Fill

`rasterize_triangle()` fills each triangle properly using the edge-function trick: for every
pixel in the triangle's bounding box, three barycentric weights are computed as normalized
signed areas of sub-triangles; the pixel is inside only if all three are non-negative. Each face
gets a fixed random color (seeded, so it's reproducible across runs), assigned once in
`compute_face_colors()`.

**What to capture (screenshot submitted separately):**

*(Screenshot: "Solid Fill (Barycentric + Z-buffer)" checked, "Show Z-Buffer" unchecked, showing
the object fully filled with per-face random colors.)*

## Part 3 — Z-Buffer

A `zbuffer` array (one `float` per pixel, reset to `+infinity` every frame) stores each pixel's
distance from the camera (computed in View space, so it means the same thing regardless of the
current projection mode). `rasterize_triangle` only writes a pixel's color if the new triangle's
interpolated depth at that pixel is closer than what's already there — solving the
draw-order-dependent overlap problem from Part 2 automatically. A "Show Z-Buffer" toggle
visualizes the depth buffer directly as a grayscale image (closer = lighter).

**Verification:** confirmed with a standalone test that draws a far triangle then a near
overlapping one (and the reverse order) — the near triangle wins the pixel in both cases.

**What to capture (screenshot submitted separately):**

*(Two screenshots of the exact same camera/object state: normal color render, then "Show
Z-Buffer" checked, showing the grayscale depth map side by side as the assignment requests.)*
