# Assignment 3 — Virtual Camera and Projection

**Code:** [`nanorender/src/main.cpp`](../nanorender/src/main.cpp)

## Part 1 — Coordinate Axes and Bounding Box

Two independent debug overlays, each with its own checkbox in "Debug Visualization":

* **Local axes** (bright red/green/blue) travel with the object — drawn through the full Model
  matrix, from the object's local origin along its local X/Y/Z.
* **World axes** (dim red/green/blue) stay fixed at the universe origin — drawn through the
  identity matrix instead of `M`, so they don't move when the object does.
* **Bounding box**: the mesh's local-space min/max corners, transformed by the same Model matrix
  as the mesh, drawn as a 12-edge wireframe box that hugs the object as it moves/rotates/scales.

**What to capture (screenshot submitted separately):**

*(Two screenshots: default transform, and after rotating/translating the object with the Local
or World sliders — showing the local axes and bbox follow the object while the world axes stay
fixed at the origin.)*

## Part 2 — View Matrix / Virtual Camera

The `Camera` struct holds a `position` and `rotation` (pitch/yaw/roll). `compute_view_matrix()`
builds the camera's own transform in world space and inverts it — bringing the whole scene into
the camera's local coordinate space:

```cpp
glm::mat4 cam_transform = glm::translate(glm::mat4(1.0f), cam.position);
cam_transform = glm::rotate(cam_transform, glm::radians(cam.rotation.y), {0,1,0});
cam_transform = glm::rotate(cam_transform, glm::radians(cam.rotation.x), {1,0,0});
cam_transform = glm::rotate(cam_transform, glm::radians(cam.rotation.z), {0,0,1});
return glm::inverse(cam_transform);
```

**Verification:** moving the camera left shifts the object right on screen (confirmed both in
code with a standalone test and visually in the app).

**What to capture (screenshot submitted separately):**

*(Two screenshots at the same object transform: default camera position, then camera X moved
negative — showing the object shift right on screen.)*

## Part 3 — Perspective Projection

`compute_projection_matrix()` builds either `glm::perspective` (FOV/aspect/near/far) or
`glm::ortho`, selected by the "Toggle Projection" button in the Projection panel. Screen
coordinates go through the full pipeline: Model → View → Projection → perspective divide → NDC
→ viewport transform, replacing Assignment 2's simpler "drop Z" approach.

**What to capture (screenshot submitted separately):**

*(Two screenshots of the same scene/camera angle: Perspective mode vs. Orthographic mode, to
show the visible difference — e.g. parallel edges converging vs. staying parallel.)*

## Part 4 — Normals

`compute_normals()` computes each face's normal (cross product of two edges, normalized) and
each vertex's normal (average of adjacent face normals, re-normalized), both in local space.
`draw_normals()` visualizes them as short lines from face centers and vertices, transformed by
the Model matrix's *normal matrix* (`transpose(inverse(mat3(M)))`) so they stay correct under
rotation.

**What to capture (screenshot submitted separately):**

*(Screenshot: "Show Normals" checked, showing the magenta face-normal lines and cyan
vertex-normal lines radiating from the mesh's surface.)*
