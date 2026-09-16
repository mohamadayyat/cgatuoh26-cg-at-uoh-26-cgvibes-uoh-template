#include "MiniFB.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <limits>

// GLM - Part 0
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
// Part 0: GLM demo – runs once at startup, prints to console
// -----------------------------------------------------------------------
static void glm_demo() {
    glm::vec3 v(1.0f, 2.0f, 3.0f);
    glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    glm::vec4 result = T * glm::vec4(v, 1.0f);
    printf("[GLM Demo] Translated (1,2,3) by +5 on X -> (%.1f, %.1f, %.1f)\n",
           result.x, result.y, result.z);
}

// -----------------------------------------------------------------------
// Part 1: Mesh data structures and OBJ loader
// -----------------------------------------------------------------------
struct Face {
    int v[3]; // indices into vertices array (0-based)
};

struct Mesh {
    std::vector<glm::vec3> vertices;
    std::vector<Face>      faces;
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
// Part 2: Normalize mesh so it fits in the window
//
// Algorithm:
//  1. Find bounding box (min/max x, y, z).
//  2. Compute center = (min + max) / 2.
//  3. Compute uniform scale = target_size / max(extent_x, extent_y, extent_z).
//  4. For each vertex: v' = (v - center) * scale + window_center.
//  This maps the mesh so its largest dimension fills ~80% of the window.
// -----------------------------------------------------------------------
static void normalize_mesh(Mesh& mesh, float win_w, float win_h) {
    if (mesh.vertices.empty()) return;

    glm::vec3 mn( 1e9f), mx(-1e9f);
    for (auto& v : mesh.vertices) {
        mn = glm::min(mn, v);
        mx = glm::max(mx, v);
    }
    glm::vec3 center = (mn + mx) * 0.5f;
    glm::vec3 extent = mx - mn;
    float max_ext = std::max({extent.x, extent.y, extent.z, 1e-6f});
    float target  = std::min(win_w, win_h) * 0.7f;
    float scale   = target / max_ext;

    glm::vec3 win_center(win_w * 0.5f, win_h * 0.5f, 0.0f);
    for (auto& v : mesh.vertices) {
        v = (v - center) * scale + win_center;
    }
}

// -----------------------------------------------------------------------
// draw_line into g_buffer (Bresenham – from Assignment 1)
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
// Part 3 + 5: Project + draw wireframe applying transformation matrix
// -----------------------------------------------------------------------
static void draw_mesh(const Mesh& mesh, const glm::mat4& M, uint32_t color) {
    for (auto& f : mesh.faces) {
        // Apply model matrix then orthographic project (drop Z)
        glm::vec4 p0 = M * glm::vec4(mesh.vertices[f.v[0]], 1.0f);
        glm::vec4 p1 = M * glm::vec4(mesh.vertices[f.v[1]], 1.0f);
        glm::vec4 p2 = M * glm::vec4(mesh.vertices[f.v[2]], 1.0f);

        draw_line_gb((int)p0.x,(int)p0.y,(int)p1.x,(int)p1.y, color);
        draw_line_gb((int)p1.x,(int)p1.y,(int)p2.x,(int)p2.y, color);
        draw_line_gb((int)p2.x,(int)p2.y,(int)p0.x,(int)p0.y, color);
    }
}

// -----------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------
int main() {
    // Part 0: GLM demo
    glm_demo();

    struct mfb_window* window =
        mfb_open_ex("Wireframe Viewer", WIDTH, HEIGHT, MFB_WF_RESIZABLE);
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
    // Part 1: Load mesh
    // -----------------------------------------------------------------------
    Mesh mesh;
    // Try to load cube.obj from current directory
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

    // Part 2: normalize once
    Mesh base_mesh = mesh;
    normalize_mesh(base_mesh, (float)WIDTH, (float)HEIGHT);

    // -----------------------------------------------------------------------
    // Part 4 & 5: Transformation state variables
    // -----------------------------------------------------------------------
    // Local (model-space) transforms
    static float local_tx=0, local_ty=0, local_tz=0;
    static float local_rx=0, local_ry=0, local_rz=0;
    static float local_sx=1, local_sy=1, local_sz=1;

    // World transforms
    static float world_tx=0, world_ty=0, world_tz=0;
    static float world_rx=0, world_ry=0, world_rz=0;
    static float world_sx=1, world_sy=1, world_sz=1;

    // Part 6: keyboard input state for arrow keys → world translation
    static float key_tx = 0.0f, key_ty = 0.0f;

    static bool quit_requested = false;

    // Info string for Part 1 display
    char info_buf[128];
    snprintf(info_buf, sizeof(info_buf), "Vertices: %d  Faces: %d",
             (int)base_mesh.vertices.size(), (int)base_mesh.faces.size());

    while (mfb_update_events(window) != MFB_STATE_EXIT) {
        // ----------------------------------------------------------------
        // 1. Input
        // ----------------------------------------------------------------
        ui_bridge_input(ctx, window);

        // Part 6: Arrow keys → world translation (direct keyboard input)
        const uint8_t* keys = mfb_get_key_buffer(window);
        if (keys[MFB_KB_KEY_LEFT])  key_tx -= 2.0f;
        if (keys[MFB_KB_KEY_RIGHT]) key_tx += 2.0f;
        if (keys[MFB_KB_KEY_UP])    key_ty -= 2.0f;
        if (keys[MFB_KB_KEY_DOWN])  key_ty += 2.0f;

        // ----------------------------------------------------------------
        // 2. Background
        // ----------------------------------------------------------------
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            int x = i % WIDTH, y = i / WIDTH;
            uint8_t r = 15, g = 15, b = 25; // dark background
            // subtle grid
            if (x % 100 == 0 || y % 100 == 0) { r=30; g=30; b=45; }
            g_buffer[i] = MFB_RGB(r, g, b);
        }

        // ----------------------------------------------------------------
        // Part 5: Build transformation matrices
        //
        // Order (local first, then world):
        //   M = World_T * World_R * World_S * Local_T * Local_R * Local_S
        //
        // Local transforms: object rotates/scales around its own center,
        //   then moves to its local offset.
        // World transforms: then the whole object is repositioned/rotated
        //   in world space (around the world origin).
        // ----------------------------------------------------------------

        // Local matrix
        glm::mat4 Lscale = glm::scale(glm::mat4(1.0f),
                           glm::vec3(local_sx, local_sy, local_sz));
        glm::mat4 Lrot   = glm::rotate(glm::mat4(1.0f), glm::radians(local_rx), glm::vec3(1,0,0));
        Lrot = glm::rotate(Lrot, glm::radians(local_ry), glm::vec3(0,1,0));
        Lrot = glm::rotate(Lrot, glm::radians(local_rz), glm::vec3(0,0,1));
        glm::mat4 Ltrans = glm::translate(glm::mat4(1.0f),
                           glm::vec3(local_tx, local_ty, local_tz));
        glm::mat4 Local  = Ltrans * Lrot * Lscale;

        // World matrix (key_tx/key_ty from Part 6 added to world_tx/ty)
        glm::mat4 Wscale = glm::scale(glm::mat4(1.0f),
                           glm::vec3(world_sx, world_sy, world_sz));
        glm::mat4 Wrot   = glm::rotate(glm::mat4(1.0f), glm::radians(world_rx), glm::vec3(1,0,0));
        Wrot = glm::rotate(Wrot, glm::radians(world_ry), glm::vec3(0,1,0));
        Wrot = glm::rotate(Wrot, glm::radians(world_rz), glm::vec3(0,0,1));
        glm::mat4 Wtrans = glm::translate(glm::mat4(1.0f),
                           glm::vec3(world_tx + key_tx, world_ty + key_ty, world_tz));
        glm::mat4 World  = Wtrans * Wrot * Wscale;

        glm::mat4 M = World * Local;

        // Part 3: draw wireframe
        draw_mesh(base_mesh, M, MFB_RGB(0, 220, 180));

        // ----------------------------------------------------------------
        // 3. UI Logic – Parts 1, 4, 5, 6
        // ----------------------------------------------------------------
        mu_begin(ctx);

        // ---- Mesh Info window (Part 1) ----
        if (mu_begin_window(ctx, "Mesh Info", mu_rect(20, 20, 340, 80))) {
            int w[] = {-1};
            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, info_buf);
            mu_end_window(ctx);
        }

        // ---- Local Transforms window (Part 4) ----
        if (mu_begin_window(ctx, "Local Transform", mu_rect(20, 120, 340, 380))) {
            int w[] = {-1};

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- Local Translation --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TX:"); mu_slider(ctx, &local_tx, -500, 500);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TY:"); mu_slider(ctx, &local_ty, -500, 500);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TZ:"); mu_slider(ctx, &local_tz, -500, 500);

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- Local Rotation (deg) --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RX:"); mu_slider(ctx, &local_rx, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RY:"); mu_slider(ctx, &local_ry, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RZ:"); mu_slider(ctx, &local_rz, -180, 180);

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- Local Scale --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SX:"); mu_slider(ctx, &local_sx, 0.1f, 5.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SY:"); mu_slider(ctx, &local_sy, 0.1f, 5.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SZ:"); mu_slider(ctx, &local_sz, 0.1f, 5.0f);

            mu_end_window(ctx);
        }

        // ---- World Transforms window (Part 4) ----
        if (mu_begin_window(ctx, "World Transform", mu_rect(380, 120, 340, 420))) {
            int w[] = {-1};

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- World Translation --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TX:"); mu_slider(ctx, &world_tx, -500, 500);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TY:"); mu_slider(ctx, &world_ty, -500, 500);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "TZ:"); mu_slider(ctx, &world_tz, -500, 500);

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- World Rotation (deg) --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RX:"); mu_slider(ctx, &world_rx, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RY:"); mu_slider(ctx, &world_ry, -180, 180);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "RZ:"); mu_slider(ctx, &world_rz, -180, 180);

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- World Scale --");
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SX:"); mu_slider(ctx, &world_sx, 0.1f, 5.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SY:"); mu_slider(ctx, &world_sy, 0.1f, 5.0f);
            mu_layout_row(ctx, 1, w, 0); mu_label(ctx, "SZ:"); mu_slider(ctx, &world_sz, 0.1f, 5.0f);

            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, "-- Arrow Keys: World TX/TY --");
            char key_info[64];
            snprintf(key_info, sizeof(key_info), "Key offset: TX=%.0f TY=%.0f", key_tx, key_ty);
            mu_layout_row(ctx, 1, w, 0);
            mu_label(ctx, key_info);
            mu_layout_row(ctx, 1, w, 0);
            if (mu_button(ctx, "Reset Key Offset")) {
                key_tx = 0; key_ty = 0;
            }

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
