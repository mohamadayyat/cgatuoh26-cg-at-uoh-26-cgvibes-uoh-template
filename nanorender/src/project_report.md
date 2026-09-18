# Final Project — Procedural Terrain Generation

## Topic and why it fits the syllabus

The syllabus lists these topics for the semester: intro, transformations 1&2, 3D modeling,
subdivision curves/surfaces, splines, intro to color theory, hidden surface removal, lighting
and shading, textures and procedural textures, procedural modeling, and OpenGL.

The three homework assignments (wireframe engine → full lit models → OpenGL) cover
transformations, hidden surface removal (Z-buffer), and lighting/shading directly. That leaves
**procedural modeling**, **procedural textures**, and **intro to color theory** essentially
untouched — so this project targets exactly those three topics with one cohesive piece of work:
**procedural terrain generation using Perlin noise, fractal Brownian motion (fBm), and
height-based procedural coloring.**

Rather than a standalone demo, the terrain is generated as an ordinary `Mesh` — the very same
struct used for the loaded cube — so it flows through the entire renderer built up over HW1–HW5
completely unmodified: normalization, bounding box, normals, the camera/view/projection
pipeline, Z-buffered rasterization, and Phong lighting (flat and per-pixel) all just work on it.
That's the actual point of the project: it demonstrates that the engine generalizes to
genuinely new content, not just the one cube it was built and tested against all semester.

## What was implemented

**Perlin noise (2D), from scratch.** A reimplementation of Ken Perlin's 1985 gradient-noise
algorithm from the public-domain reference method (permutation table, quintic fade curve,
gradient dot products) — not copied from any specific library or codebase.

**Fractal Brownian Motion (fBm).** Several octaves of Perlin noise are summed at increasing
frequency (`lacunarity`) and decreasing amplitude (`persistence`), which is what turns smooth
Perlin "blobs" into terrain with both large rolling hills and small rocky detail.

**Terrain mesh generation.** An N×N grid of vertices is built, with each vertex's height driven
by `fbm()`, then triangulated into a regular grid (two triangles per quad). This reuses the
existing `compute_normals()` from Assignment 3 unmodified — the terrain's smoothly-varying
vertex normals for Phong shading come from the exact same averaging code as the cube's.

**Procedural coloring (color theory).** A fixed palette — deep water, sand, grass, rock, snow —
is linearly blended by normalized height into a smooth gradient, stored as a color per vertex.
This plugs into the lighting pipeline as a stand-in for texture mapping: the flat-shading modes
use each face's average color as its material color, and the Phong per-pixel mode
(`rasterize_triangle_phong_textured`) interpolates the color per pixel with the same barycentric
weights already used for position/normal/depth — the same technique Assignment 5's optional
texture-mapping extension describes, just sampling a procedural function instead of a bitmap.

## Controls

A new **"Final Project: Terrain"** panel lets you:
- Toggle **Terrain Mode** on/off (off instantly restores the original cube — nothing about the
  homework assignments' grading is affected by this feature existing).
- Toggle **Procedural Coloring** on/off, to compare colored vs. flat-gray terrain.
- Adjust **Seed**, **Resolution**, **World Size**, **Height Scale**, **Noise Scale**, **Octaves**,
  **Persistence**, and **Lacunarity**, then click **Regenerate Terrain**.

Everything else in the renderer — camera controls, Local/World transforms, wireframe/solid/
Z-buffer view toggles, all four lighting stages, the light position/color sliders — works on the
terrain exactly as it does on the cube, since it's just another mesh to the rest of the code.

## Verification

Every previous assignment in this codebase was validated with standalone tests before being
committed, and this project follows the same standard:

- **Continuity** — two points 0.001 units apart differ by ~0.0004 in noise value (no
  discontinuities).
- **Bounded range** — 10,000 samples stayed within roughly [-1.5, 1.5].
- **Determinism** — the same seed reproduces bit-identical output.
- **Seed sensitivity** — different seeds produce different terrain.
- **fBm formula correctness** — the function's output was checked term-by-term against a manual
  sum of `amplitude × perlin2(x×frequency, y×frequency)` across 5 octaves; they matched exactly.
- **Mesh generation correctness** — for a 10×10 grid: exactly 100 vertices and 162 faces (2×9×9)
  as the grid math predicts, every vertex within the requested world-size/height-scale bounds,
  every face index valid, and zero degenerate (duplicate-index) triangles.
- **Color banding continuity** — sampling just below and just above a band boundary (0.2999 vs.
  0.3001) gives nearly identical colors, confirming no hard seams in the gradient.
- **Full build** — configured and compiled from a clean `build/` directory with CMake fetching
  MiniFB, MicroUI, and GLM, with zero warnings and zero errors under `-Wall -Wextra`.

## Suggested report screenshots

1. Terrain Mode off (the cube) vs. on (terrain), same camera angle.
2. The same terrain with Procedural Coloring on vs. off.
3. Low octave count (e.g. 1) vs. high (e.g. 7) at the same seed — smooth rolling hills vs.
   detailed, rocky terrain.
4. A few different seeds side by side, to show the generator produces varied results.
5. The terrain in each of the four lighting stages (Ambient / Flat Diffuse / Flat Specular /
   Phong per-pixel), to show the procedural color surviving through the full lighting pipeline.
