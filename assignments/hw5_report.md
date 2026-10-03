# Assignment 5 — Lighting and Shading (Phong Reflection Model)

**Code:** [`nanorender/src/main.cpp`](../nanorender/src/main.cpp)

All four stages below are built up incrementally in `compute_phong_color()`, and can be selected
live via the "Next Stage" button in the "HW5: Lighting (Phong)" panel — the current stage name is
shown right above the button.

## Part 1 — Ambient Lighting

`PointLight` and `Material` structs each hold separate ambient/diffuse/specular RGB properties.
With "Enable Lighting" on and the stage set to "Ambient Only", every pixel's color is simply
`light.ambient * material.ambient` — a flat, uniform tint completely independent of the surface's
orientation or the light's position.

**What to capture (screenshot submitted separately):**

*(Screenshot: stage set to "Ambient Only (Part 1)" — the whole object should look like one flat,
dim, evenly-lit color with no shading variation at all across its surface.)*

## Part 2 — Flat Diffuse Shading

Adds Lambert's cosine law: `diffuse = light.diffuse * material.diffuse * max(dot(N, L), 0)`,
where `N` is the face normal and `L` is the direction toward the light — computed once per face
(using the face center and face normal), not per pixel, so each triangle is a single flat shade.

**What to capture (screenshot submitted separately):**

*(Screenshot: stage set to "Flat: +Diffuse (Part 2)" — faces facing the light should be
noticeably brighter than faces angled away from it, with a hard edge between each flat-shaded
triangle.)*

## Part 3 — Flat Specular Shading + Debug Vectors

Adds the specular highlight: `R = reflect(-L, N)`, `specular = light.specular *
material.specular * pow(max(dot(R, V), 0), shininess)`, where `V` is the direction toward the
camera. A separate "Show Light/Reflection Vectors" toggle draws the incoming light vector
(yellow) and outgoing reflection vector (cyan) from a handful of face centers, for visual
verification of the reflection math.

**What to capture (screenshot submitted separately):**

*(Screenshot: stage set to "Flat: +Specular (Part 3)". The "Show Light/Reflection Vectors"
checkbox in the same panel draws the incoming light vector in yellow and the outgoing
reflection vector in cyan from a handful of face centers, for visual verification of the
reflection math — check that box for an additional screenshot if your report needs it.)*

## Part 4 — Phong (Per-Pixel) Shading

`rasterize_triangle_phong()` extends the Assignment 4 rasterizer: instead of one flat color per
triangle, every covered pixel interpolates its own world-space position and world-space normal
from the three vertices (using the same barycentric weights already computed for the Z-buffer
test), then runs the full lighting equation on that interpolated pair — producing smooth,
continuously-varying shading across each face instead of Part 2/3's flat triangles.

**Verification:** a standalone test confirmed a surface facing the light gets far more diffuse
light than one facing perpendicular to it, and specular highlights peak when the view direction
aligns with the reflection vector.

**What to capture (screenshot submitted separately):**

*(Screenshot: stage set to "Phong: Per-Pixel (Part 4)" — shading should look smooth across each
face, with a visible specular highlight that moves continuously rather than jumping between flat
triangle faces.)*
