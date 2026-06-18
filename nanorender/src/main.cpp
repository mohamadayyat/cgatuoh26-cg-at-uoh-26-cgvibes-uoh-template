#include "MiniFB.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

extern "C" {
#include "microui.h"
}
#include "ui_bridge.h"
#include "ui_renderer.h"

#define WIDTH  1600
#define HEIGHT 1200

static uint32_t g_buffer[WIDTH * HEIGHT];

// -----------------------------------------------------------------------
// Part 3: Global state toggled by the keyboard callback
// -----------------------------------------------------------------------
static bool g_invert_colors = false;   // toggled by pressing 'I' / 'i'

// -----------------------------------------------------------------------
// Part 6: Persistent line storage
// -----------------------------------------------------------------------
struct Line {
    int x0, y0, x1, y1;
    uint8_t r, g, b;
};
static Line  g_lines[4096];
static int   g_line_count = 0;

// Drawing interaction state (click-drag-release UX – see UX discussion below)
static bool  g_drawing      = false;
static int   g_start_x      = 0;
static int   g_start_y      = 0;

// -----------------------------------------------------------------------
// Part 1 + Part 5 app-state variables
// -----------------------------------------------------------------------
static float g_freq   = 5.0f;   // controls wave frequency in the background
static float g_speed  = 0.0f;   // animation phase (unused unless you want animation)
static int   g_bg_mode = 0;     // 0 = wave pattern, 1 = concentric rings (toggled by Part 3)

// -----------------------------------------------------------------------
// draw_line into g_buffer  (Bresenham – Part 6)
// -----------------------------------------------------------------------
static void draw_line_gb(int x0, int y0, int x1, int y1, uint32_t color)
{
    // Classic Bresenham – handles all 8 octants without code duplication (DRY)
    int dx =  abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        if (x0 >= 0 && x0 < WIDTH && y0 >= 0 && y0 < HEIGHT)
            g_buffer[y0 * WIDTH + x0] = color;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

int main()
{
    struct mfb_window *window =
        mfb_open_ex("MiniGUI Platform", WIDTH, HEIGHT, MFB_WF_RESIZABLE);
    if (!window) return 1;

    mu_Context *ctx = (mu_Context *)malloc(sizeof(mu_Context));
    mu_init(ctx);

    ctx->text_width  = [](mu_Font, const char *str, int len) {
        return (len < 0 ? (int)strlen(str) : len) * 8;
    };
    ctx->text_height = [](mu_Font) { return 8; };

    UIRenderer renderer(WIDTH, HEIGHT);

    // -----------------------------------------------------------------------
    // Part 3: intercept keyboard input BEFORE the UI bridge sees it.
    // Pressing 'I'/'i' toggles the invert-color / bg-mode flag.
    // The event is NOT forwarded to g_pending_text so text widgets are
    // unaffected, but you could also pass it through by removing the return.
    // -----------------------------------------------------------------------
    mfb_set_char_input_callback(
        [](struct mfb_window *w, unsigned int c) {
            if (c == 'i' || c == 'I') {
                // toggle background mode (consume the key – do not forward)
                g_bg_mode = (g_bg_mode == 0) ? 1 : 0;
                return;
            }
            // all other keys are forwarded to MicroUI normally
            extern void ui_bridge_char_input(struct mfb_window *, unsigned int);
            ui_bridge_char_input(w, c);
        },
        window);

    // -----------------------------------------------------------------------
    // Part 5 & 6: per-frame state variables bound to UI widgets
    // -----------------------------------------------------------------------
    static float slider_val  = 50.0f;
    static float line_r      = 255.0f;
    static float line_g      =   0.0f;
    static float line_b      = 128.0f;
    static int   clear_flag  = 0;
    static bool  quit_requested = false;

    // Part 5 variables bound to the background pattern
    static float freq_val    =  5.0f;   // wave frequency

    // Part 2: simple toggle label demo
    static bool  hello_toggled = false;

    static int   checkbox_a    = 0;
    static int   checkbox_b    = 1;
    static char  textbox_buf[128] = "edit me";
    static float number_val    = 3.14f;

    while (mfb_update_events(window) != MFB_STATE_EXIT)
    {
        // ----------------------------------------------------------------
        // 1. Input
        // ----------------------------------------------------------------
        ui_bridge_input(ctx, window);

        // ----------------------------------------------------------------
        // 2. Scene Rendering (Background)  –  Part 1 + Part 5
        //
        // UX conversation summary (Part 6 AI planning):
        //  Option A – Two-click (set start, set end): simple but gives no
        //             visual feedback while positioning the second point.
        //  Option B – Click-drag-release: the user sees a live preview line
        //             while dragging; on release it becomes permanent.
        //             This is the most intuitive interaction for drawing
        //             tools and mirrors what applications like Paint use.
        //  → We chose Option B.  State machine:
        //     mouse-down  → record start point, set g_drawing = true
        //     mouse-move  → if g_drawing, render preview line each frame
        //     mouse-up    → commit line to g_lines[], g_drawing = false
        // ----------------------------------------------------------------
        for (int i = 0; i < WIDTH * HEIGHT; i++) {
            int x = i % WIDTH;
            int y = i / WIDTH;

            uint8_t r, g, b;

            if (g_bg_mode == 0) {
                // Part 1 + Part 5: interference wave pattern
                // freq_val (bound to slider) controls the number of rings
                float fx = (float)x / WIDTH  - 0.5f;
                float fy = (float)y / HEIGHT - 0.5f;
                float dist = sqrtf(fx*fx + fy*fy);
                float wave = sinf(dist * freq_val * 20.0f);
                float wave2 = sinf((fx - fy) * freq_val * 15.0f);
                r = (uint8_t)((wave  + 1.0f) * 0.5f * 200.0f) + 20;
                g = (uint8_t)((wave2 + 1.0f) * 0.5f * 180.0f) + 20;
                b = (uint8_t)(((float)x / WIDTH) * 120.0f + 60.0f);
            } else {
                // Part 3 toggle: concentric rings (pressed 'I')
                float fx = (float)x / WIDTH  - 0.5f;
                float fy = (float)y / HEIGHT - 0.5f;
                float dist = sqrtf(fx*fx + fy*fy) * 20.0f;
                int band = (int)dist % 3;
                r = band == 0 ? 220 : 30;
                g = band == 1 ? 200 : 30;
                b = band == 2 ? 240 : 30;
            }

            g_buffer[i] = MFB_RGB(r, g, b);
        }

        // ----------------------------------------------------------------
        // Draw all permanent lines
        // ----------------------------------------------------------------
        for (int k = 0; k < g_line_count; k++) {
            Line &L = g_lines[k];
            draw_line_gb(L.x0, L.y0, L.x1, L.y1, MFB_RGB(L.r, L.g, L.b));
        }

        // ----------------------------------------------------------------
        // Live preview line while dragging (Part 6)
        // ----------------------------------------------------------------
        int mx = mfb_get_mouse_x(window);
        int my = mfb_get_mouse_y(window);
        const uint8_t *mouse_btn = mfb_get_mouse_button_buffer(window);
        bool lmb = mouse_btn[MFB_MOUSE_LEFT] != 0;

        if (lmb && !g_drawing) {
            // Mouse just pressed – record start
            g_drawing = true;
            g_start_x = mx;
            g_start_y = my;
        }
        if (!lmb && g_drawing) {
            // Mouse released – commit line
            g_drawing = false;
            if (g_line_count < 4096) {
                g_lines[g_line_count++] = {
                    g_start_x, g_start_y, mx, my,
                    (uint8_t)(int)line_r,
                    (uint8_t)(int)line_g,
                    (uint8_t)(int)line_b
                };
            }
        }
        if (g_drawing) {
            // Draw translucent preview by drawing the line with current color
            draw_line_gb(g_start_x, g_start_y, mx, my,
                         MFB_RGB((uint8_t)line_r, (uint8_t)line_g, (uint8_t)line_b));
        }

        // Clear screen button
        if (clear_flag) {
            g_line_count = 0;
            clear_flag   = 0;
        }

        // ----------------------------------------------------------------
        // 3. UI Logic  –  Parts 2, 4, 5, 6
        // ----------------------------------------------------------------
        mu_begin(ctx);

        // ---- Main tool window ----
        if (mu_begin_window(ctx, "Drawing Tools", mu_rect(20, 20, 320, 560))) {
            int w1[] = {-1};

            // Part 2: simple interactive widget (toggle label)
            mu_layout_row(ctx, 1, w1, 0);
            if (mu_button(ctx, "Toggle Hello")) {
                hello_toggled = !hello_toggled;
                printf("[Part 2] Button clicked – toggled to %s\n",
                       hello_toggled ? "ON" : "OFF");
            }
            if (hello_toggled) {
                mu_label(ctx, "Hello from MicroUI!");
            }

            // Divider label
            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "--- Line Color ---");

            // Part 5: sliders bound to line_r, line_g, line_b
            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "Red:");
            mu_slider(ctx, &line_r, 0, 255);

            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "Green:");
            mu_slider(ctx, &line_g, 0, 255);

            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "Blue:");
            mu_slider(ctx, &line_b, 0, 255);

            // Part 5: slider bound to background wave frequency
            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "--- Background ---");
            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "Wave Freq:");
            mu_slider(ctx, &freq_val, 1, 20);
            g_freq = freq_val;  // write back to rendering variable

            // Part 2: checkbox demo
            mu_layout_row(ctx, 1, w1, 0);
            mu_checkbox(ctx, "Checkbox A", &checkbox_a);
            mu_checkbox(ctx, "Checkbox B (on)", &checkbox_b);

            // Part 2: textbox demo
            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "Textbox:");
            mu_textbox(ctx, textbox_buf, sizeof(textbox_buf));

            // Clear button
            mu_layout_row(ctx, 1, w1, 0);
            if (mu_button(ctx, "Clear Canvas")) {
                clear_flag = 1;
                printf("[Part 6] Canvas cleared.\n");
            }

            // Hint label
            mu_layout_row(ctx, 1, w1, 0);
            mu_label(ctx, "Press 'I' to toggle background");

            // Quit
            mu_layout_row(ctx, 1, w1, 0);
            if (mu_button(ctx, "Quit")) {
                quit_requested = true;
            }

            mu_end_window(ctx);
        }

        // ---- Original Widgets demo window (Part 2 showcase) ----
        if (mu_begin_window(ctx, "Widgets Demo", mu_rect(360, 20, 340, 300))) {
            int w2[] = {-1};
            mu_layout_row(ctx, 1, w2, 0);
            mu_label(ctx, "mu_label: plain static text");
            mu_text(ctx, "mu_text: word-wrapped longer text that will "
                         "reflow inside the window width automatically.");
            mu_layout_row(ctx, 1, w2, 0);
            if (mu_button(ctx, "mu_button: click me")) {
                printf("[Part 2] Demo button clicked!\n");
            }
            mu_layout_row(ctx, 1, w2, 0);
            mu_label(ctx, "mu_slider (0-100):");
            mu_slider(ctx, &slider_val, 0, 100);
            mu_layout_row(ctx, 1, w2, 0);
            mu_label(ctx, "mu_number (step 0.1):");
            mu_number(ctx, &number_val, 0.1f);
            mu_end_window(ctx);
        }

        mu_end(ctx);

        if (quit_requested) {
            mfb_close(window);
            break;
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
