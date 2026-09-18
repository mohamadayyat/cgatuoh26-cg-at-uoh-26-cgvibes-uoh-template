#include "MiniFB.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>
#include <random>

// GLM - Part 0 (Assignment 2)
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

extern "C" {
#include "microui.h"
}
#include "ui_bridge.h"
#include "ui_renderer.h"

#define WIDTH  1600
#define HEIGHT 1200

static uint32_t g_buffer[WIDTH * HEIGHT];

// -----------------------------------------------------------------------
// Assignment 1, Parts 1/3/5: creative background pattern state.
// File-scope (not local to main) because the char-input callback below is
// a plain function pointer (no captures allowed), so it can only reach
// state through globals/statics - the same reason g_pending_text in
// ui_bridge.h is file-scope.
// -----------------------------------------------------------------------
static float g_bg_phase = 0.0f;     // Part 5: driven by a UI slider
static float g_bg_ring_scale = 0.05f; // Part 5: driven by a UI slider

// -----------------------------------------------------------------------
// Part 0 (Assignment 2): GLM demo - runs once at startup, prints to console
// -----------------------------------------------------------------------
static void glm_demo() {
    glm::vec3 v(1.0f, 2.0f, 3.0f);
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    glm::vec4 result = T * glm::vec4(v, 1.0f);
    printf("[GLM Demo] Translated (1,2,3) by +5 on X -> (%.1f, %.1f, %.1f)\n",
           result.x, result.y, result.z);
}

// -----------------------------------------------------------------------
// Mesh data structures and OBJ loader (Assignment 2, Part 1)
// -----------------------------------------------------------------------
struct Face {
    int v[3]; // indices into vertices array (0-based)
};

struct Mesh {
    std::vector<glm::vec3> vertices;
    std::vector<Face>      faces;

    // Assignment 3, Part 4: per-face and per-vertex normals (local space)
    std::vector<glm::vec3> face_normals;
    std::vector<glm::vec3> face_centers;
    std::vector<glm::vec3> vertex_normals;

    // Assignment 4, Part 2: a random solid color per face
    std::vector<uint32_t> face_colors;
};

// Loads a simple .obj file (v and f lines only)
static bool load_obj(const char* path, Mesh& mesh) {
    std::ifstream file(path);
    if (!file.is_open()) {
        printf("[OBJ] Could not open: %s\n", path);
        return false;
    }
    mesh.vertices.clear();
    mesh.faces.clear();

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string token;
        ss >> token;

        if (token == "v") {
            float x, y, z;
            ss >> x >> y >> z;
            mesh.vertices.push_back({x, y, z});
        } else if (token == "f") {
            // faces can be "f 1 2 3" or "f 1/1/1 2/2/2 3/3/3"
            std::vector<int> idx;
            std::string word;
            while (ss >> word) {
                int vi = std::stoi(word.substr(0, word.find('/')));
                idx.push_back(vi - 1); // convert to 0-based
            }
            // triangulate (fan) in case of quads/ngons
            for (int i = 1; i + 1 < (int)idx.size(); i++) {
                mesh.faces.push_back({idx[0], idx[i], idx[i+1]});
            }
        }
    }
    printf("[OBJ] Loaded: %zu vertices, %zu faces from %s\n",
           mesh.vertices.size(), mesh.faces.size(), path);
    return true;
}

// -----------------------------------------------------------------------
// Assignment 2, Part 2: Normalize mesh into "world units" centered at the
// origin (instead of screen pixels, now that we have a real camera/
// projection pipeline).
//
// Algorithm:
//  1. Find bounding box (min/max x, y, z) in local/object space.
//  2. Compute center = (min + max) / 2.
//  3. Compute uniform scale = target_size / max(extent_x, extent_y, extent_z).
//  4. For each vertex: v' = (v - center) * scale.
//  This maps the mesh so its largest dimension is `target_size` world
//  units wide, centered on (0,0,0) - a comfortable size for our camera
//  which starts a few units back looking at the origin.
// -----------------------------------------------------------------------
static void normalize_mesh(Mesh& mesh, float target_size) {
    if (mesh.vertices.empty()) return;

    glm::vec3 mn( 1e9f), mx(-1e9f);
    for (auto& v : mesh.vertices) {
        mn = glm::min(mn, v);
        mx = glm::max(mx, v);
    }
    glm::vec3 center = (mn + mx) * 0.5f;
    glm::vec3 extent = mx - mn;
    float max_ext = std::max({extent.x, extent.y, extent.z, 1e-6f});
    float scale   = target_size / max_ext;

    for (auto& v : mesh.vertices) {
        v = (v - center) * scale;
    }
}

// -----------------------------------------------------------------------
// Assignment 3, Part 4: Compute face normals (cross product of edges) and
// vertex normals (average of adjacent face normals), all in local space.
// -----------------------------------------------------------------------
static void compute_normals(Mesh& mesh) {
    mesh.face_normals.assign(mesh.faces.size(), glm::vec3(0.0f));
    mesh.face_centers.assign(mesh.faces.size(), glm::vec3(0.0f));
    mesh.vertex_normals.assign(mesh.vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < mesh.faces.size(); i++) {
        const Face& f = mesh.faces[i];
        glm::vec3 p0 = mesh.vertices[f.v[0]];
        glm::vec3 p1 = mesh.vertices[f.v[1]];
        glm::vec3 p2 = mesh.vertices[f.v[2]];

        glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        float len = glm::length(n);
        if (len > 1e-8f) n /= len;

        mesh.face_normals[i] = n;
        mesh.face_centers[i] = (p0 + p1 + p2) / 3.0f;

        // Accumulate onto each vertex sharing this face; normalized after.
        mesh.vertex_normals[f.v[0]] += n;
        mesh.vertex_normals[f.v[1]] += n;
        mesh.vertex_normals[f.v[2]] += n;
    }

    for (auto& vn : mesh.vertex_normals) {
        float len = glm::length(vn);
        if (len > 1e-8f) vn /= len;
    }
}

// -----------------------------------------------------------------------
// Assignment 4, Part 2: assign each face a random solid color, once.
// Seeded RNG so colors stay consistent between runs (not reshuffled
// every frame or every restart).
// -----------------------------------------------------------------------
static void compute_face_colors(Mesh& mesh) {
    mesh.face_colors.resize(mesh.faces.size());
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> dist(60, 255);
    for (auto& c : mesh.face_colors) {
        uint8_t r = (uint8_t)dist(rng), g = (uint8_t)dist(rng), b = (uint8_t)dist(rng);
        c = MFB_RGB(r, g, b);
    }
}

// -----------------------------------------------------------------------
// Assignment 3, Part 1: Axis-aligned bounding box in local/object space.
// -----------------------------------------------------------------------
struct BBox { glm::vec3 mn, mx; };

static BBox compute_bbox(const Mesh& mesh) {
    BBox box{glm::vec3(1e9f), glm::vec3(-1e9f)};
    for (auto& v : mesh.vertices) {
        box.mn = glm::min(box.mn, v);
        box.mx = glm::max(box.mx, v);
    }
    return box;
}

// -----------------------------------------------------------------------
// draw_line into g_buffer (Bresenham - from Assignment 1)
// -----------------------------------------------------------------------
static void draw_line_gb(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx =  abs(x1-x0), sx = x0<x1 ? 1:-1;
    int dy = -abs(y1-y0), sy = y0<y1 ? 1:-1;
    int err = dx+dy;
    while (true) {
        if (x0>=0 && x0<WIDTH && y0>=0 && y0<HEIGHT)
            g_buffer[y0*WIDTH+x0] = color;
        if (x0==x1 && y0==y1) break;
        int e2 = 2*err;
        if (e2>=dy){ err+=dy; x0+=sx; }
        if (e2<=dx){ err+=dx; y0+=sy; }
    }
}

// -----------------------------------------------------------------------
// Assignment 3, Part 2: The Virtual Camera (View Matrix)
//
// The camera doesn't really exist - to simulate it, we build the camera's
// own transform in world space (where it sits, how it's rotated), and then
// invert that transform. Inverting brings every vertex in the scene into
// the camera's local coordinate space, which is exactly the "opposite"
// translation/rotation the assignment describes.
// -----------------------------------------------------------------------
struct Camera {
    glm::vec3 position{0.0f, 0.0f, 6.0f};
    glm::vec3 rotation{0.0f, 0.0f, 0.0f}; // pitch (X), yaw (Y), roll (Z), degrees
};

static glm::mat4 compute_view_matrix(const Camera& cam) {
    glm::mat4 cam_transform = glm::translate(glm::mat4(1.0f), cam.position);
    cam_transform = glm::rotate(cam_transform, glm::radians(cam.rotation.y), glm::vec3(0, 1, 0));
    cam_transform = glm::rotate(cam_transform, glm::radians(cam.rotation.x), glm::vec3(1, 0, 0));
    cam_transform = glm::rotate(cam_transform, glm::radians(cam.rotation.z), glm::vec3(0, 0, 1));
    return glm::inverse(cam_transform);
}

// -----------------------------------------------------------------------
// Assignment 3, Part 3: Perspective Projection (with an Orthographic mode
// to toggle back to, so the difference between the two is easy to see).
// -----------------------------------------------------------------------
static glm::mat4 compute_projection_matrix(bool perspective, float fov_deg,
                                            float aspect, float znear, float zfar,
                                            float ortho_half_height) {
    if (perspective) {
        return glm::perspective(glm::radians(fov_deg), aspect, znear, zfar);
    }
    float half_w = ortho_half_height * aspect;
    return glm::ortho(-half_w, half_w, -ortho_half_height, ortho_half_height, znear, zfar);
}

// -----------------------------------------------------------------------
// Pipeline helpers: local space -> (Model) -> world/clip space -> (View,
// Projection) -> clip space -> perspective divide -> NDC -> viewport
// transform -> screen pixels.
// -----------------------------------------------------------------------

// Applies the Model matrix only; keeps the point in "world" space so it can
// be reused for things like normal tips that are computed relative to an
// already-transformed point (see draw_normals).
static glm::vec3 to_world(const glm::vec3& local_pos, const glm::mat4& M) {
    return glm::vec3(M * glm::vec4(local_pos, 1.0f));
}

// Applies View+Projection to a world-space point, does the perspective
// divide, and maps the result into screen pixels. Returns false if the
// point is behind the camera / on the camera plane (w <= 0), so the caller
// can skip drawing edges that would otherwise wrap around incorrectly.
static bool project_world_to_screen(const glm::vec3& world_pos, const glm::mat4& VP,
                                     glm::vec2& out_screen) {
    glm::vec4 clip = VP * glm::vec4(world_pos, 1.0f);
    if (clip.w <= 1e-4f) return false;
    glm::vec3 ndc = glm::vec3(clip) / clip.w; // Perspective Divide -> [-1,1]
    out_screen.x = (ndc.x * 0.5f + 0.5f) * WIDTH;
    out_screen.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * HEIGHT; // flip Y for screen space
    return true;
}

// Convenience: local space straight to screen, going through the Model
// matrix first (P * V * M * v).
static bool project_local_to_screen(const glm::vec3& local_pos, const glm::mat4& M,
                                     const glm::mat4& VP, glm::vec2& out_screen) {
    return project_world_to_screen(to_world(local_pos, M), VP, out_screen);
}

// -----------------------------------------------------------------------
// Assignment 4, Part 3: like project_local_to_screen, but also returns a
// depth value for the Z-buffer - the point's distance from the camera
// (positive, larger = farther), computed in View space rather than after
// the projection matrix, so it means the same thing regardless of
// perspective/orthographic mode or FOV.
// -----------------------------------------------------------------------
static bool project_local_to_screen_depth(const glm::vec3& local_pos, const glm::mat4& M,
                                           const glm::mat4& View, const glm::mat4& VP,
                                           glm::vec2& out_screen, float& out_depth) {
    glm::vec3 world_pos = to_world(local_pos, M);
    glm::vec4 view_pos = View * glm::vec4(world_pos, 1.0f);
    out_depth = -view_pos.z; // camera looks down -Z in view space
    return project_world_to_screen(world_pos, VP, out_screen);
}

// -----------------------------------------------------------------------
// Assignment 2/3, Part 3: draw the wireframe mesh through the full
// P * V * M pipeline.
// -----------------------------------------------------------------------
static void draw_mesh(const Mesh& mesh, const glm::mat4& M, const glm::mat4& VP, uint32_t color) {
    for (auto& f : mesh.faces) {
        glm::vec2 s0, s1, s2;
        bool ok0 = project_local_to_screen(mesh.vertices[f.v[0]], M, VP, s0);
        bool ok1 = project_local_to_screen(mesh.vertices[f.v[1]], M, VP, s1);
        bool ok2 = project_local_to_screen(mesh.vertices[f.v[2]], M, VP, s2);

        if (ok0 && ok1) draw_line_gb((int)s0.x, (int)s0.y, (int)s1.x, (int)s1.y, color);
        if (ok1 && ok2) draw_line_gb((int)s1.x, (int)s1.y, (int)s2.x, (int)s2.y, color);
        if (ok2 && ok0) draw_line_gb((int)s2.x, (int)s2.y, (int)s0.x, (int)s0.y, color);
    }
}

// -----------------------------------------------------------------------
// Assignment 4, Part 1: naive bounding-box "rasterization" for debugging -
// just fills the whole 2D screen-space rectangle around a triangle with a
// solid color, no inside/outside test and no depth test. Used to sanity
// check the projection pipeline before adding the real triangle fill.
// -----------------------------------------------------------------------
static void draw_triangle_bbox_debug(const glm::vec2& p0, const glm::vec2& p1,
                                      const glm::vec2& p2, uint32_t color) {
    int min_x = std::max(0, (int)floorf(std::min({p0.x, p1.x, p2.x})));
    int max_x = std::min(WIDTH  - 1, (int)ceilf(std::max({p0.x, p1.x, p2.x})));
    int min_y = std::max(0, (int)floorf(std::min({p0.y, p1.y, p2.y})));
    int max_y = std::min(HEIGHT - 1, (int)ceilf(std::max({p0.y, p1.y, p2.y})));

    for (int y = min_y; y <= max_y; y++)
        for (int x = min_x; x <= max_x; x++)
            g_buffer[y * WIDTH + x] = color;
}

// Signed area of the triangle (a,b,c) times 2 - also used as an edge
// function: edge_function(a,b,p) is positive when p is to a consistent
// side of the directed edge a->b.
static float edge_function(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
}

// -----------------------------------------------------------------------
// Assignment 4, Parts 2 & 3: fill a triangle using Barycentric coordinates,
// with a per-pixel Z-buffer test so nearer triangles correctly occlude
// farther ones regardless of draw order.
//
// For every pixel in the triangle's bounding box, we compute barycentric
// weights (w0, w1, w2) via the edge-function trick: each w_i is the
// (normalized) signed area of the sub-triangle opposite vertex i. If all
// three are >= 0, the pixel is inside. The interpolated depth is simply
// w0*z0 + w1*z1 + w2*z2 (a plain weighted average - not perspective-
// correct, but matches the assignment's stated formula and is a
// reasonable approximation for a wireframe-scale model like ours).
// -----------------------------------------------------------------------
static void rasterize_triangle(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                                float z0, float z1, float z2,
                                uint32_t color, std::vector<float>& zbuffer) {
    int min_x = std::max(0, (int)floorf(std::min({p0.x, p1.x, p2.x})));
    int max_x = std::min(WIDTH  - 1, (int)ceilf(std::max({p0.x, p1.x, p2.x})));
    int min_y = std::max(0, (int)floorf(std::min({p0.y, p1.y, p2.y})));
    int max_y = std::min(HEIGHT - 1, (int)ceilf(std::max({p0.y, p1.y, p2.y})));
    if (min_x > max_x || min_y > max_y) return;

    float area = edge_function(p0, p1, p2);
    if (fabsf(area) < 1e-6f) return; // degenerate (zero-area) triangle

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            glm::vec2 p((float)x + 0.5f, (float)y + 0.5f); // sample pixel centers
            float w0 = edge_function(p1, p2, p) / area;
            float w1 = edge_function(p2, p0, p) / area;
            float w2 = edge_function(p0, p1, p) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                float depth = w0 * z0 + w1 * z1 + w2 * z2;
                int idx = y * WIDTH + x;
                if (depth < zbuffer[idx]) {
                    zbuffer[idx] = depth;
                    g_buffer[idx] = color;
                }
            }
        }
    }
}

// =========================================================================
// Assignment 5: Lighting, Materials, and Shading (Phong Reflection Model)
// =========================================================================

// Part 1: light and material properties.
struct PointLight {
    glm::vec3 position{2.5f, 3.0f, 4.0f};
    glm::vec3 ambient{0.15f, 0.15f, 0.15f};
    glm::vec3 diffuse{1.0f, 1.0f, 0.95f};
    glm::vec3 specular{1.0f, 1.0f, 1.0f};
};

struct Material {
    glm::vec3 ambient{0.75f, 0.35f, 0.15f};
    glm::vec3 diffuse{0.75f, 0.35f, 0.15f};
    glm::vec3 specular{1.0f, 1.0f, 1.0f};
    float shininess = 32.0f;
};

// Shading stages, built up incrementally exactly as the assignment's
// Parts 1-4 describe, so each one can be checked visually on its own.
enum LightingStage {
    STAGE_AMBIENT_ONLY  = 0, // Part 1
    STAGE_FLAT_DIFFUSE  = 1, // Part 2
    STAGE_FLAT_SPECULAR = 2, // Part 3
    STAGE_PHONG_PIXEL   = 3, // Part 4
};

static uint32_t color_from_vec3(const glm::vec3& c) {
    uint8_t r = (uint8_t)(std::max(0.0f, std::min(1.0f, c.r)) * 255.0f);
    uint8_t g = (uint8_t)(std::max(0.0f, std::min(1.0f, c.g)) * 255.0f);
    uint8_t b = (uint8_t)(std::max(0.0f, std::min(1.0f, c.b)) * 255.0f);
    return MFB_RGB(r, g, b);
}

// -----------------------------------------------------------------------
// Assignment 5, Parts 1-3: the full Ambient + Diffuse + Specular equation
// for one point (position + normal, both in world space). Used both for
// Flat Shading (called once per face, with the face center/normal) and,
// with per-pixel interpolated inputs, for Phong Shading (Part 4).
// -----------------------------------------------------------------------
static glm::vec3 compute_phong_color(const glm::vec3& pos_world, const glm::vec3& normal_world,
                                      const glm::vec3& view_pos_world, int stage,
                                      const PointLight& light, const Material& mat) {
    glm::vec3 ambient = light.ambient * mat.ambient; // Part 1

    if (stage == STAGE_AMBIENT_ONLY) return ambient;

    glm::vec3 N = glm::normalize(normal_world);
    glm::vec3 L = glm::normalize(light.position - pos_world);
    float diff = std::max(glm::dot(N, L), 0.0f); // Lambert's Cosine Law
    glm::vec3 diffuse = light.diffuse * mat.diffuse * diff; // Part 2

    if (stage == STAGE_FLAT_DIFFUSE) return ambient + diffuse;

    glm::vec3 V = glm::normalize(view_pos_world - pos_world);
    glm::vec3 R = glm::reflect(-L, N);
    float spec = (diff > 0.0f) ? powf(std::max(glm::dot(R, V), 0.0f), mat.shininess) : 0.0f;
    glm::vec3 specular = light.specular * mat.specular * spec; // Part 3

    return ambient + diffuse + specular;
}

// -----------------------------------------------------------------------
// Assignment 5, Part 4: Phong (per-pixel) Shading. Extends the Assignment
// 4 barycentric rasterizer: instead of one flat color for the whole
// triangle, every covered pixel interpolates its own world position and
// world normal from the three vertices (using the same barycentric
// weights already needed for the depth test), then runs the full
// lighting equation on that interpolated pair.
// -----------------------------------------------------------------------
static void rasterize_triangle_phong(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2,
                                      float z0, float z1, float z2,
                                      const glm::vec3& wp0, const glm::vec3& wp1, const glm::vec3& wp2,
                                      const glm::vec3& wn0, const glm::vec3& wn1, const glm::vec3& wn2,
                                      const glm::vec3& cam_pos_world,
                                      const PointLight& light, const Material& mat,
                                      std::vector<float>& zbuffer) {
    int min_x = std::max(0, (int)floorf(std::min({p0.x, p1.x, p2.x})));
    int max_x = std::min(WIDTH  - 1, (int)ceilf(std::max({p0.x, p1.x, p2.x})));
    int min_y = std::max(0, (int)floorf(std::min({p0.y, p1.y, p2.y})));
    int max_y = std::min(HEIGHT - 1, (int)ceilf(std::max({p0.y, p1.y, p2.y})));
    if (min_x > max_x || min_y > max_y) return;

    float area = edge_function(p0, p1, p2);
    if (fabsf(area) < 1e-6f) return;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            glm::vec2 p((float)x + 0.5f, (float)y + 0.5f);
            float w0 = edge_function(p1, p2, p) / area;
            float w1 = edge_function(p2, p0, p) / area;
            float w2 = edge_function(p0, p1, p) / area;

            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                float depth = w0 * z0 + w1 * z1 + w2 * z2;
                int idx = y * WIDTH + x;
                if (depth < zbuffer[idx]) {
                    // 1. Interpolate position; 2/3. interpolate + normalize normal.
                    glm::vec3 pos_world    = w0 * wp0 + w1 * wp1 + w2 * wp2;
                    glm::vec3 normal_world = glm::normalize(w0 * wn0 + w1 * wn1 + w2 * wn2);
                    // 4. Full lighting equation per pixel.
                    glm::vec3 lit = compute_phong_color(pos_world, normal_world, cam_pos_world,
                                                         STAGE_PHONG_PIXEL, light, mat);
                    zbuffer[idx] = depth;
                    g_buffer[idx] = color_from_vec3(lit);
                }
            }
        }
    }
}

// -----------------------------------------------------------------------
// Assignment 5, Part 3: debug visualization - draws the incoming Light
// Vector (yellow) and outgoing Reflection Vector (cyan) from the center
// of a handful of faces, so the reflection math can be visually verified
// (and screenshotted for the report).
// -----------------------------------------------------------------------
static void draw_lighting_debug_vectors(const Mesh& mesh, const glm::mat4& M, const glm::mat4& VP,
                                         const PointLight& light, float length, int max_faces) {
    glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(M)));
    int n = std::min((int)mesh.faces.size(), max_faces);

    for (int i = 0; i < n; i++) {
        glm::vec3 center_world = to_world(mesh.face_centers[i], M);
        glm::vec3 normal_world = glm::normalize(normal_mat * mesh.face_normals[i]);
        glm::vec3 L = glm::normalize(light.position - center_world);
        glm::vec3 R = glm::reflect(-L, normal_world);

        glm::vec3 light_tip = center_world + L * length;
        glm::vec3 refl_tip  = center_world + R * length;

        glm::vec2 s0, s1, s2;
        bool ok0 = project_world_to_screen(center_world, VP, s0);
        bool ok1 = project_world_to_screen(light_tip, VP, s1);
        bool ok2 = project_world_to_screen(refl_tip, VP, s2);
        if (ok0 && ok1) draw_line_gb((int)s0.x, (int)s0.y, (int)s1.x, (int)s1.y, MFB_RGB(255, 255, 0)); // Light vector
        if (ok0 && ok2) draw_line_gb((int)s0.x, (int)s0.y, (int)s2.x, (int)s2.y, MFB_RGB(0, 255, 255)); // Reflection vector
    }
}

// -----------------------------------------------------------------------
// Assignment 3, Part 1: draw the wireframe bounding box (8 corners, 12
// edges), transformed by the same Model matrix as the mesh so it hugs the
// object as it's moved/rotated/scaled.
// -----------------------------------------------------------------------
static void draw_bbox(const BBox& box, const glm::mat4& M, const glm::mat4& VP, uint32_t color) {
    glm::vec3 c[8] = {
        {box.mn.x, box.mn.y, box.mn.z}, {box.mx.x, box.mn.y, box.mn.z},
        {box.mx.x, box.mx.y, box.mn.z}, {box.mn.x, box.mx.y, box.mn.z},
        {box.mn.x, box.mn.y, box.mx.z}, {box.mx.x, box.mn.y, box.mx.z},
        {box.mx.x, box.mx.y, box.mx.z}, {box.mn.x, box.mx.y, box.mx.z},
    };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0}, // near face
        {4,5},{5,6},{6,7},{7,4}, // far face
        {0,4},{1,5},{2,6},{3,7}, // connecting edges
    };

    glm::vec2 s[8];
    bool ok[8];
    for (int i = 0; i < 8; i++) ok[i] = project_local_to_screen(c[i], M, VP, s[i]);

    for (auto& e : edges) {
        if (ok[e[0]] && ok[e[1]])
            draw_line_gb((int)s[e[0]].x, (int)s[e[0]].y, (int)s[e[1]].x, (int)s[e[1]].y, color);
    }
}

// -----------------------------------------------------------------------
// Assignment 3, Part 1: draw Local axes (at the model's center, rotating
// and moving with it) and World axes (fixed at the universe's origin).
// Red = X, Green = Y, Blue = Z.
// -----------------------------------------------------------------------
static void draw_axes_triplet(const glm::mat4& M, const glm::mat4& VP, float length,
                               uint32_t x_color, uint32_t y_color, uint32_t z_color) {
    glm::vec2 o, x, y, z;
    bool ok_o = project_local_to_screen({0,0,0},        M, VP, o);
    bool ok_x = project_local_to_screen({length,0,0},   M, VP, x);
    bool ok_y = project_local_to_screen({0,length,0},   M, VP, y);
    bool ok_z = project_local_to_screen({0,0,length},   M, VP, z);

    if (ok_o && ok_x) draw_line_gb((int)o.x,(int)o.y,(int)x.x,(int)x.y, x_color);
    if (ok_o && ok_y) draw_line_gb((int)o.x,(int)o.y,(int)y.x,(int)y.y, y_color);
    if (ok_o && ok_z) draw_line_gb((int)o.x,(int)o.y,(int)z.x,(int)z.y, z_color);
}

static void draw_axes(const glm::mat4& M, const glm::mat4& VP, float length) {
    // Local axes: travel with the object (Model matrix applied).
    draw_axes_triplet(M, VP, length,
                       MFB_RGB(255, 70, 70), MFB_RGB(70, 255, 70), MFB_RGB(90, 150, 255));
    // World axes: always fixed at the universe origin (identity model).
    static const glm::mat4 I(1.0f);
    draw_axes_triplet(I, VP, length,
                       MFB_RGB(140, 20, 20), MFB_RGB(20, 130, 20), MFB_RGB(30, 60, 140));
}

// -----------------------------------------------------------------------
// Assignment 3, Part 4: draw face normals (from each face's center) and
// vertex normals (from each vertex), transformed correctly under rotation
// via the Model matrix's normal matrix (transpose of the inverse of the
// upper-left 3x3), so they stay perpendicular to the surface even when the
// object is non-uniformly scaled.
// -----------------------------------------------------------------------
static void draw_normals(const Mesh& mesh, const glm::mat4& M, const glm::mat4& VP,
                          float length, uint32_t face_color, uint32_t vertex_color) {
    glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(M)));

    for (size_t i = 0; i < mesh.faces.size(); i++) {
        glm::vec3 center_world = to_world(mesh.face_centers[i], M);
        glm::vec3 n = mesh.face_normals[i];
        float nlen = glm::length(n);
        if (nlen < 1e-8f) continue;
        glm::vec3 n_world = glm::normalize(normal_mat * n);
        glm::vec3 tip_world = center_world + n_world * length;

        glm::vec2 s0, s1;
        bool ok0 = project_world_to_screen(center_world, VP, s0);
        bool ok1 = project_world_to_screen(tip_world, VP, s1);
        if (ok0 && ok1) draw_line_gb((int)s0.x, (int)s0.y, (int)s1.x, (int)s1.y, face_color);
    }

    for (size_t i = 0; i < mesh.vertices.size(); i++) {
        glm::vec3 n = mesh.vertex_normals[i];
        float nlen = glm::length(n);
        if (nlen < 1e-8f) continue;
        glm::vec3 origin_world = to_world(mesh.vertices[i], M);
        glm::vec3 n_world = glm::normalize(normal_mat * n);
        glm::vec3 tip_world = origin_world + n_world * length;

        glm::vec2 s0, s1;
        bool ok0 = project_world_to_screen(origin_world, VP, s0);
        bool ok1 = project_world_to_screen(tip_world, VP, s1);
        if (ok0 && ok1) draw_line_gb((int)s0.x, (int)s0.y, (int)s1.x, (int)s1.y, vertex_color);
    }
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------
int main() {
    // Part 0 (Assignment 2): GLM demo
    glm_demo();

    struct mfb_window* window =
        mfb_open_ex("Virtual Camera Viewer", WIDTH, HEIGHT, MFB_WF_RESIZABLE);
    if (!window) return 1;

    mu_Context* ctx = (mu_Context*)malloc(sizeof(mu_Context));
    mu_init(ctx);
    ctx->text_width  = [](mu_Font, const char* s, int l){ return (l<0?(int)strlen(s):l)*8; };
    ctx->text_height = [](mu_Font){ return 8; };

    UIRenderer renderer(WIDTH, HEIGHT);

    // -----------------------------------------------------------------------
    // Assignment 1, Part 3: character input callback, extended with a
    // custom visual effect trigger.
    //
    // Pressing 'r'/'R' reshuffles the background pattern's phase (see the
    // Background section below) instead of being typed anywhere - so we
    // *consume* it here (return without forwarding). Every other key is
    // passed along to ui_bridge_char_input as before, so text fields (like
    // MicroUI's textboxes) still receive normal typed characters.
    // -----------------------------------------------------------------------
    mfb_set_char_input_callback(
        [](struct mfb_window* w, unsigned int c){
            if (c == 'r' || c == 'R') {
                g_bg_phase = (float)(rand() % 1000) * 0.01f; // consumed, not forwarded
                return;
            }
            extern void ui_bridge_char_input(struct mfb_window*, unsigned int);
            ui_bridge_char_input(w, c);
        }, window);

    // -----------------------------------------------------------------------
    // Assignment 2, Part 1: Load mesh
    // -----------------------------------------------------------------------
    Mesh mesh;
    bool loaded = load_obj("cube.obj", mesh);
    if (!loaded || mesh.vertices.empty()) {
        // Fallback: build a cube in code
        mesh.vertices = {
            {-1,1,1},{1,1,1},{1,-1,1},{-1,-1,1},
            {-1,1,-1},{1,1,-1},{1,-1,-1},{-1,-1,-1}
        };
        mesh.faces = {
            {0,1,2},{0,2,3},{4,5,1},{4,1,0},
            {5,6,2},{5,2,1},{6,7,3},{6,3,2},
            {7,4,0},{7,0,3},{4,7,6},{4,6,5}
        };
        printf("[Mesh] Using built-in cube fallback.\n");
    }

    // Assignment 2, Part 2: normalize once into world units (was screen
    // pixels in Assignment 2; now centered at the origin for the camera).
    Mesh base_mesh = mesh;
    normalize_mesh(base_mesh, 2.5f);

    // Assignment 3, Part 1: bounding box, computed once in local space.
    BBox bbox = compute_bbox(base_mesh);

    // Assignment 3, Part 4: face/vertex normals, computed once in local space.
    compute_normals(base_mesh);

    // Assignment 4, Part 2: a random solid color per face, computed once.
    compute_face_colors(base_mesh);

    // Assignment 4, Part 3: Z-buffer, one float per pixel, same size as g_buffer.
    static std::vector<float> zbuffer(WIDTH * HEIGHT);

    // -----------------------------------------------------------------------
    // Assignment 2, Part 4 & 5: Transformation state variables (now in
    // world units instead of raw pixels).
    // -----------------------------------------------------------------------
    static float local_tx=0, local_ty=0, local_tz=0;
    static float local_rx=0, local_ry=0, local_rz=0;
    static float local_sx=1, local_sy=1, local_sz=1;

    static float world_tx=0, world_ty=0, world_tz=0;
    static float world_rx=0, world_ry=0, world_rz=0;
    static float world_sx=1, world_sy=1, world_sz=1;

    // Assignment 2, Part 6: keyboard input state for arrow keys -> world translation
    static float key_tx = 0.0f, key_ty = 0.0f;

    // -----------------------------------------------------------------------
    // Assignment 3, Part 2: Camera state
    // -----------------------------------------------------------------------
    static Camera camera;

    // Assignment 3, Part 3: Projection state
    static bool  use_perspective = true;
    static float fov_deg = 60.0f;
    static float near_plane = 0.1f;
    static float far_plane = 100.0f;
    static float ortho_half_height = 2.0f;

    // Assignment 3, Part 1 & 4: debug visualization toggles.
    // NOTE: these must be `int`, not `bool` - mu_checkbox() takes an `int*`
    // and writes 0/1 into it; a bool* would be the wrong size and corrupt
    // adjacent memory.
    static int show_axes    = 1;
    static int show_bbox    = 1;
    static int show_normals = 0;
    static float axis_length   = 0.8f;
    static float normal_length = 0.25f;

    // Assignment 4: rasterization toggles.
    static int show_wireframe   = 1; // keep the old wireframe outline by default
    static int show_bbox_debug  = 0; // Part 1: naive overlapping colored boxes
    static int show_solid_fill  = 1; // Parts 2/3: barycentric fill + Z-buffer
    static int show_zbuffer_view = 0; // Part 3: grayscale depth-map visualization

    // Assignment 5: lighting state.
    static PointLight light;
    static Material   material;
    static int   lighting_enabled = 0;
    static int   lighting_stage   = STAGE_PHONG_PIXEL;
    static int   show_light_debug = 0;
    static float light_debug_length = 0.6f;

    // -----------------------------------------------------------------------
    // Assignment 1, Part 6: Interactive Line Drawing Tool state.
    //
    // UX choice (per the assignment's "AI-Assisted UX Planning" task):
    // click-and-drag-and-release, not click-click. Reasoning: click-click
    // requires the tool to remember "we're mid-line" as separate hidden
    // state across frames with no visual anchor between the two clicks,
    // which is easy to leave dangling (e.g. if the user clicks a UI button
    // instead of a second point). Click-drag-release ties the "in
    // progress" state directly to the mouse button being held, so there's
    // no ambiguous in-between state, and the live preview line gives
    // immediate visual feedback of exactly what will be committed.
    // -----------------------------------------------------------------------
    struct LineSeg { int x0, y0, x1, y1; uint32_t color; };
    static std::vector<LineSeg> g_lines;
    static bool  g_dragging = false;
    static int   g_drawing_mode = 0; // int, not bool - mu_checkbox takes int*
    static int   g_drag_x0 = 0, g_drag_y0 = 0;
    static float line_r = 0.0f, line_g = 220.0f, line_b = 180.0f;

    // Assignment 1, Part 2: demo widget state (toggleable label)
    static int g_show_demo_label = 0;

    static bool quit_requested = false;

    char info_buf[128];
    snprintf(info_buf, sizeof(info_buf), "Vertices: %d  Faces: %d",
             (int)base_mesh.vertices.size(), (int)base_mesh.faces.size());

    while (mfb_update_events(window) != MFB_STATE_EXIT) {
        // ----------------------------------------------------------------
        // 1. Input
        // ----------------------------------------------------------------
        ui_bridge_input(ctx, window);

        // Assignment 2, Part 6: Arrow keys -> world translation
        const uint8_t* keys = mfb_get_key_buffer(window);
        if (keys[MFB_KB_KEY_LEFT])  key_tx -= 0.03f;
        if (keys[MFB_KB_KEY_RIGHT]) key_tx += 0.03f;
        if (keys[MFB_KB_KEY_UP])    key_ty += 0.03f;
        if (keys[MFB_KB_KEY_DOWN])  key_ty -= 0.03f;

        // ----------------------------------------------------------------
        // 2. Background
        //
        // Assignment 1, Part 1: a creative pattern using both x and y (not
        // a solid color or a 1D strip) - concentric rings around the
        // window center, colored by distance and angle, animated/tunable
        // via g_bg_phase and g_bg_ring_scale (Part 5: bound to UI sliders
        // below; Part 3: g_bg_phase is also reshuffled by pressing 'r').
        // ----------------------------------------------------------------
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            int x = i % WIDTH, y = i / WIDTH;
            float dx = (float)x - WIDTH * 0.5f;
            float dy = (float)y - HEIGHT * 0.5f;
            float dist = sqrtf(dx * dx + dy * dy);

            uint8_t r = (uint8_t)(20 + 25 * sinf(dist * g_bg_ring_scale + g_bg_phase));
            uint8_t g = (uint8_t)(18 + 20 * sinf(x * 0.008f - g_bg_phase * 0.7f));
            uint8_t b = (uint8_t)(32 + 28 * cosf(y * 0.008f + g_bg_phase * 1.3f));

            g_buffer[i] = MFB_RGB(r, g, b);
        }

        // ----------------------------------------------------------------
        // Assignment 2, Part 5: Build the Model matrix
        //   M = World_T * World_R * World_S * Local_T * Local_R * Local_S
        // ----------------------------------------------------------------
        glm::mat4 Lscale = glm::scale(glm::mat4(1.0f), glm::vec3(local_sx, local_sy, local_sz));
        glm::mat4 Lrot   = glm::rotate(glm::mat4(1.0f), glm::radians(local_rx), glm::vec3(1,0,0));
        Lrot = glm::rotate(Lrot, glm::radians(local_ry), glm::vec3(0,1,0));
        Lrot = glm::rotate(Lrot, glm::radians(local_rz), glm::vec3(0,0,1));
        glm::mat4 Ltrans = glm::translate(glm::mat4(1.0f), glm::vec3(local_tx, local_ty, local_tz));
        glm::mat4 Local  = Ltrans * Lrot * Lscale;

        glm::mat4 Wscale = glm::scale(glm::mat4(1.0f), glm::vec3(world_sx, world_sy, world_sz));
        glm::mat4 Wrot   = glm::rotate(glm::mat4(1.0f), glm::radians(world_rx), glm::vec3(1,0,0));
        Wrot = glm::rotate(Wrot, glm::radians(world_ry), glm::vec3(0,1,0));
        Wrot = glm::rotate(Wrot, glm::radians(world_rz), glm::vec3(0,0,1));
        glm::mat4 Wtrans = glm::translate(glm::mat4(1.0f),
                           glm::vec3(world_tx + key_tx, world_ty + key_ty, world_tz));
        glm::mat4 World  = Wtrans * Wrot * Wscale;

        glm::mat4 M = World * Local;

        // ----------------------------------------------------------------
        // Assignment 3, Part 2: View matrix from the Camera
        // ----------------------------------------------------------------
        glm::mat4 View = compute_view_matrix(camera);

        // ----------------------------------------------------------------
        // Assignment 3, Part 3: Projection matrix (Perspective/Orthographic)
        // ----------------------------------------------------------------
        float aspect = (float)WIDTH / (float)HEIGHT;
        glm::mat4 Proj = compute_projection_matrix(use_perspective, fov_deg, aspect,
                                                    near_plane, far_plane, ortho_half_height);
        glm::mat4 VP = Proj * View;

        // ----------------------------------------------------------------
        // Draw: mesh, then debug overlays (P * V * M * v pipeline throughout)
        // ----------------------------------------------------------------

        // Assignment 4, Part 1: naive bounding-box debug view. Mutually
        // exclusive with the real solid fill below - it exists purely to
        // sanity-check the projection pipeline before trusting the
        // barycentric test, so showing both at once would just be visual
        // noise.
        if (show_bbox_debug) {
            std::mt19937 dbg_rng(4321);
            std::uniform_int_distribution<int> dbg_dist(60, 255);
            for (auto& f : base_mesh.faces) {
                glm::vec2 s0, s1, s2;
                bool ok0 = project_local_to_screen(base_mesh.vertices[f.v[0]], M, VP, s0);
                bool ok1 = project_local_to_screen(base_mesh.vertices[f.v[1]], M, VP, s1);
                bool ok2 = project_local_to_screen(base_mesh.vertices[f.v[2]], M, VP, s2);
                if (ok0 && ok1 && ok2) {
                    uint32_t rc = MFB_RGB((uint8_t)dbg_dist(dbg_rng), (uint8_t)dbg_dist(dbg_rng),
                                           (uint8_t)dbg_dist(dbg_rng));
                    draw_triangle_bbox_debug(s0, s1, s2, rc);
                }
            }
        }

        // Assignment 4, Parts 2 & 3: solid barycentric fill with Z-buffer.
        // Needed whenever we want to *display* the solid render, or when we
        // only need the Z-buffer populated for the depth-map visualization
        // (in which case its color writes get discarded by that final
        // grayscale overwrite below anyway, so it's harmless to run even
        // if the bbox-debug view above also happened to be checked).
        //
        // Assignment 5: when lighting is enabled, the per-face random
        // colors from HW4 are replaced by Phong-lit colors instead -
        // either one flat color per face (Parts 1-3: Ambient / +Diffuse /
        // +Specular), or a genuinely different color per pixel via
        // rasterize_triangle_phong (Part 4).
        bool need_rasterize = show_solid_fill || show_zbuffer_view;
        if (need_rasterize) {
            std::fill(zbuffer.begin(), zbuffer.end(), std::numeric_limits<float>::max());
            glm::mat3 normal_mat = glm::transpose(glm::inverse(glm::mat3(M)));

            for (size_t i = 0; i < base_mesh.faces.size(); i++) {
                const Face& f = base_mesh.faces[i];
                glm::vec2 s0, s1, s2; float z0, z1, z2;
                bool ok0 = project_local_to_screen_depth(base_mesh.vertices[f.v[0]], M, View, VP, s0, z0);
                bool ok1 = project_local_to_screen_depth(base_mesh.vertices[f.v[1]], M, View, VP, s1, z1);
                bool ok2 = project_local_to_screen_depth(base_mesh.vertices[f.v[2]], M, View, VP, s2, z2);
                if (!(ok0 && ok1 && ok2)) continue;

                if (!lighting_enabled) {
                    rasterize_triangle(s0, s1, s2, z0, z1, z2, base_mesh.face_colors[i], zbuffer);
                } else if (lighting_stage != STAGE_PHONG_PIXEL) {
                    // Flat Shading (Parts 1-3): light once, using the face
                    // center and face normal, in world space.
                    glm::vec3 center_world = to_world(base_mesh.face_centers[i], M);
                    glm::vec3 normal_world = glm::normalize(normal_mat * base_mesh.face_normals[i]);
                    glm::vec3 lit = compute_phong_color(center_world, normal_world, camera.position,
                                                         lighting_stage, light, material);
                    rasterize_triangle(s0, s1, s2, z0, z1, z2, color_from_vec3(lit), zbuffer);
                } else {
                    // Phong Shading (Part 4): per-pixel, via world-space
                    // vertex positions and normals.
                    glm::vec3 wp0 = to_world(base_mesh.vertices[f.v[0]], M);
                    glm::vec3 wp1 = to_world(base_mesh.vertices[f.v[1]], M);
                    glm::vec3 wp2 = to_world(base_mesh.vertices[f.v[2]], M);
                    glm::vec3 wn0 = glm::normalize(normal_mat * base_mesh.vertex_normals[f.v[0]]);
                    glm::vec3 wn1 = glm::normalize(normal_mat * base_mesh.vertex_normals[f.v[1]]);
                    glm::vec3 wn2 = glm::normalize(normal_mat * base_mesh.vertex_normals[f.v[2]]);
                    rasterize_triangle_phong(s0, s1, s2, z0, z1, z2, wp0, wp1, wp2, wn0, wn1, wn2,
                                              camera.position, light, material, zbuffer);
                }
            }
        }

        if (show_wireframe) draw_mesh(base_mesh, M, VP, MFB_RGB(0, 220, 180));
        if (show_bbox)    draw_bbox(bbox, M, VP, MFB_RGB(255, 210, 60));
        if (show_axes)    draw_axes(M, VP, axis_length);
        if (show_normals) draw_normals(base_mesh, M, VP, normal_length,
                                        MFB_RGB(255, 120, 255), MFB_RGB(120, 220, 255));
        if (show_light_debug)
            draw_lighting_debug_vectors(base_mesh, M, VP, light, light_debug_length, 6);

        // ----------------------------------------------------------------
        // Assignment 1, Part 6: Interactive Line Drawing Tool.
        //
        // Only captures mouse input while Drawing Mode is on, so it never
        // fights with normal 3D-view interaction. Uses raw MiniFB mouse
        // state (not MicroUI's) since this is drawing on the canvas
        // itself, not on a UI widget.
        // ----------------------------------------------------------------
        if (g_drawing_mode) {
            const uint8_t* mbtn = mfb_get_mouse_button_buffer(window);
            int mx = mfb_get_mouse_x(window), my = mfb_get_mouse_y(window);
            static bool prev_down = false;
            bool down = mbtn[MFB_MOUSE_LEFT] != 0;

            if (down && !prev_down) {                 // press: start a new line
                g_dragging = true;
                g_drag_x0 = mx; g_drag_y0 = my;
            } else if (!down && prev_down && g_dragging) { // release: commit it
                uint32_t col = MFB_RGB((uint8_t)line_r, (uint8_t)line_g, (uint8_t)line_b);
                g_lines.push_back({g_drag_x0, g_drag_y0, mx, my, col});
                g_dragging = false;
            }
            prev_down = down;

            if (g_dragging) // live preview of the line currently being dragged
                draw_line_gb(g_drag_x0, g_drag_y0, mx, my, MFB_RGB(255, 255, 255));
        }
        // Permanent lines stay visible whether or not Drawing Mode is on.
        for (auto& l : g_lines) draw_line_gb(l.x0, l.y0, l.x1, l.y1, l.color);

        // ----------------------------------------------------------------
        // 3. UI Logic
        // ----------------------------------------------------------------
        mu_begin(ctx);

        // ---- Mesh Info window ----
        if (mu_begin_window(ctx, "Mesh Info", mu_rect(20, 20, 320, 80))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, info_buf);
            mu_end_window(ctx);
        }

        // ---- Local Transform window (Assignment 2, Part 4) ----
        if (mu_begin_window(ctx, "Local Transform", mu_rect(20, 120, 320, 360))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Local Translation --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TX:"); mu_slider(ctx, &local_tx, -5, 5);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TY:"); mu_slider(ctx, &local_ty, -5, 5);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TZ:"); mu_slider(ctx, &local_tz, -5, 5);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Local Rotation (deg) --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RX:"); mu_slider(ctx, &local_rx, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RY:"); mu_slider(ctx, &local_ry, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RZ:"); mu_slider(ctx, &local_rz, -180, 180);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Local Scale --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SX:"); mu_slider(ctx, &local_sx, 0.1f, 3.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SY:"); mu_slider(ctx, &local_sy, 0.1f, 3.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SZ:"); mu_slider(ctx, &local_sz, 0.1f, 3.0f);
            mu_end_window(ctx);
        }

        // ---- World Transform window (Assignment 2, Part 4) ----
        if (mu_begin_window(ctx, "World Transform", mu_rect(360, 120, 320, 400))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- World Translation --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TX:"); mu_slider(ctx, &world_tx, -5, 5);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TY:"); mu_slider(ctx, &world_ty, -5, 5);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TZ:"); mu_slider(ctx, &world_tz, -5, 5);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- World Rotation (deg) --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RX:"); mu_slider(ctx, &world_rx, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RY:"); mu_slider(ctx, &world_ry, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RZ:"); mu_slider(ctx, &world_rz, -180, 180);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- World Scale --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SX:"); mu_slider(ctx, &world_sx, 0.1f, 3.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SY:"); mu_slider(ctx, &world_sy, 0.1f, 3.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SZ:"); mu_slider(ctx, &world_sz, 0.1f, 3.0f);

            char key_info[64];
            snprintf(key_info, sizeof(key_info), "Arrow keys offset: TX=%.2f TY=%.2f", key_tx, key_ty);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, key_info);
            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Reset Key Offset")) { key_tx = 0; key_ty = 0; }
            mu_end_window(ctx);
        }

        // ---- Camera window (Assignment 3, Part 2) ----
        if (mu_begin_window(ctx, "Camera (View Matrix)", mu_rect(700, 120, 320, 320))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Position --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "X:"); mu_slider(ctx, &camera.position.x, -15, 15);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Y:"); mu_slider(ctx, &camera.position.y, -15, 15);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Z:"); mu_slider(ctx, &camera.position.z, -15, 15);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Rotation (deg) --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Pitch (X):"); mu_slider(ctx, &camera.rotation.x, -89, 89);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Yaw   (Y):"); mu_slider(ctx, &camera.rotation.y, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Roll  (Z):"); mu_slider(ctx, &camera.rotation.z, -180, 180);

            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Reset Camera")) {
                camera.position = glm::vec3(0.0f, 0.0f, 6.0f);
                camera.rotation = glm::vec3(0.0f);
            }
            mu_end_window(ctx);
        }

        // ---- Projection window (Assignment 3, Part 3) ----
        if (mu_begin_window(ctx, "Projection", mu_rect(1040, 120, 320, 280))) {
            int w[] = {-1};
            char mode_label[48];
            snprintf(mode_label, sizeof(mode_label), "Mode: %s (click to toggle)",
                     use_perspective ? "Perspective" : "Orthographic");
            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, mode_label)) use_perspective = !use_perspective;

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Field of View (deg):");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &fov_deg, 10.0f, 120.0f);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Ortho half-height:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &ortho_half_height, 0.5f, 10.0f);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Near plane:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &near_plane, 0.01f, 5.0f);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Far plane:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &far_plane, 10.0f, 500.0f);
            mu_end_window(ctx);
        }

        // ---- HW4: Rasterization window (Assignment 4, Parts 1, 2, 3) ----
        if (mu_begin_window(ctx, "HW4: Rasterization", mu_rect(1040, 420, 320, 300))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Part 1 --");
            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "BBox Rasterization (debug)", &show_bbox_debug);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Parts 2 & 3 --");
            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "Solid Fill (Barycentric + Z-buffer)", &show_solid_fill);
            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "Show Z-Buffer (grayscale depth map)", &show_zbuffer_view);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Other --");
            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "Show Wireframe Outline", &show_wireframe);
            mu_end_window(ctx);
        }

        // ---- HW5: Lighting window (Assignment 5, Parts 1-4) ----
        if (mu_begin_window(ctx, "HW5: Lighting (Phong)", mu_rect(1040, 740, 320, 440))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "Enable Lighting", &lighting_enabled);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Shading Stage --");
            char stage_label[64];
            const char* stage_names[] = {"Ambient Only (Part 1)", "Flat: +Diffuse (Part 2)",
                                          "Flat: +Specular (Part 3)", "Phong: Per-Pixel (Part 4)"};
            snprintf(stage_label, sizeof(stage_label), "Current: %s", stage_names[lighting_stage]);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, stage_label);
            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Next Stage")) lighting_stage = (lighting_stage + 1) % 4;

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Light Position --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "X:"); mu_slider(ctx, &light.position.x, -10, 10);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Y:"); mu_slider(ctx, &light.position.y, -10, 10);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Z:"); mu_slider(ctx, &light.position.z, -10, 10);

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Light Color (Diffuse/Specular) --");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &light.diffuse.r, 0, 1);
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &light.diffuse.g, 0, 1);
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &light.diffuse.b, 0, 1);
            light.specular = light.diffuse; // specular highlights match the light's color

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Ambient strength:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &light.ambient.r, 0, 0.5f);
            light.ambient.g = light.ambient.b = light.ambient.r;

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Material Color --");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &material.diffuse.r, 0, 1);
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &material.diffuse.g, 0, 1);
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &material.diffuse.b, 0, 1);
            material.ambient = material.diffuse;

            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Shininess:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &material.shininess, 2.0f, 128.0f);

            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "Show Light/Reflection Vectors", &show_light_debug);
            mu_end_window(ctx);
        }

        // ---- Debug Visualization window (Assignment 3, Parts 1 & 4) ----
        if (mu_begin_window(ctx, "Debug Visualization", mu_rect(700, 460, 320, 260))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0); mu_checkbox(ctx, "Show Coordinate Axes", &show_axes);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Axis length:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &axis_length, 0.1f, 2.0f);

            mu_layout_row(ctx, 1, w, 0); mu_checkbox(ctx, "Show Bounding Box", &show_bbox);

            mu_layout_row(ctx, 1, w, 0); mu_checkbox(ctx, "Show Normals", &show_normals);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Normal length:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &normal_length, 0.05f, 1.0f);

            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Quit")) quit_requested = true;
            mu_end_window(ctx);
        }

        // ---- HW1: Background & Drawing Tool window (Assignment 1, Parts 1,2,3,5,6) ----
        if (mu_begin_window(ctx, "HW1: Background & Drawing", mu_rect(360, 540, 320, 420))) {
            int w[] = {-1};

            // Part 1/5: sliders bound to the creative background pattern.
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Background Pattern (Part 1/5) --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Ring scale:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &g_bg_ring_scale, 0.01f, 0.2f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Phase:");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &g_bg_phase, 0.0f, 10.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "(Part 3: press 'R' to reshuffle phase)");

            // Part 2: a plain widget demo (button + toggled label).
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Widget Demo (Part 2) --");
            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Print Mesh Info to Console")) {
                printf("[Part 2 Demo] Mesh has %d vertices and %d faces.\n",
                       (int)base_mesh.vertices.size(), (int)base_mesh.faces.size());
            }
            mu_layout_row(ctx, 1, w, 0); mu_checkbox(ctx, "Show demo label", &g_show_demo_label);
            if (g_show_demo_label) {
                mu_layout_row(ctx, 1, w, 0);
                mu_label(ctx, "Hello from a MicroUI checkbox-bound label!");
            }

            // Part 6: interactive line drawing tool controls.
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "-- Drawing Tool (Part 6) --");
            mu_layout_row(ctx, 1, w, 0);
            mu_checkbox(ctx, "Drawing Mode (click+drag on canvas)", &g_drawing_mode);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "Line color (R,G,B):");
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &line_r, 0, 255);
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &line_g, 0, 255);
            mu_layout_row(ctx, 1, w, 0); mu_slider(ctx, &line_b, 0, 255);
            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Clear Lines")) g_lines.clear();

            mu_end_window(ctx);
        }

        mu_end(ctx);

        if (quit_requested) { mfb_close(window); break; }

        // ----------------------------------------------------------------
        // Assignment 4, Part 3: Z-buffer visualization. Overwrites the
        // whole color buffer with a grayscale depth map (closer = lighter)
        // so it can be screenshotted side-by-side with the normal color
        // render, per the assignment's report requirement. Runs after
        // everything else so the depth map is clean and undisturbed by
        // wireframe/axes/line-tool overlays.
        // ----------------------------------------------------------------
        if (show_zbuffer_view) {
            float range = far_plane - near_plane;
            for (int i = 0; i < WIDTH * HEIGHT; i++) {
                float d = zbuffer[i];
                if (d >= std::numeric_limits<float>::max()) {
                    g_buffer[i] = MFB_RGB(0, 0, 0); // nothing drawn there: background = black
                } else {
                    float t = (d - near_plane) / (range > 1e-6f ? range : 1.0f);
                    t = std::max(0.0f, std::min(1.0f, t));
                    uint8_t gray = (uint8_t)(255.0f * (1.0f - t)); // closer = lighter
                    g_buffer[i] = MFB_RGB(gray, gray, gray);
                }
            }
        }

        // ----------------------------------------------------------------
        // 4. UI Rendering
        // ----------------------------------------------------------------
        renderer.render(ctx, g_buffer);

        // ----------------------------------------------------------------
        // 5. Display
        // ----------------------------------------------------------------
        mfb_update_state state = mfb_update_ex(window, g_buffer, WIDTH, HEIGHT);
        if (state < 0) break;
        mfb_wait_sync(window);
    }

    mfb_close(window);
    free(ctx);
    return 0;
}
