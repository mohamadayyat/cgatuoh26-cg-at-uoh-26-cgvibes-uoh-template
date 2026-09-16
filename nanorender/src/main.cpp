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

    mfb_set_char_input_callback(
        [](struct mfb_window* w, unsigned int c){
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
        // ----------------------------------------------------------------
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            int x = i % WIDTH, y = i / WIDTH;
            uint8_t r = 15, g = 15, b = 25; // dark background
            if (x % 100 == 0 || y % 100 == 0) { r=30; g=30; b=45; } // subtle grid
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
        draw_mesh(base_mesh, M, VP, MFB_RGB(0, 220, 180));
        if (show_bbox)    draw_bbox(bbox, M, VP, MFB_RGB(255, 210, 60));
        if (show_axes)    draw_axes(M, VP, axis_length);
        if (show_normals) draw_normals(base_mesh, M, VP, normal_length,
                                        MFB_RGB(255, 120, 255), MFB_RGB(120, 220, 255));

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

        mu_end(ctx);

        if (quit_requested) { mfb_close(window); break; }

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