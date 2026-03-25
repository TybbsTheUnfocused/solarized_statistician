# Emery Platform Adaptation

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the watchface render correctly on Emery (200x228) by replacing hardcoded Basalt (144x168) pixel values with a runtime layout computed from screen bounds.

**Architecture:** Introduce a `Layout` struct populated once in `window_load()` from `layer_get_bounds()`. All drawing and text layer creation uses this struct instead of `#define` constants. The bell curve draw loop scales to variable width by mapping pixel positions back to the existing 100-point LUT with integer interpolation, and scales heights proportionally.

**Tech Stack:** Pebble SDK 4.x C API. Testing via `pebble install --emulator basalt` and `pebble install --emulator emery`.

---

## File Map

- **Modify:** `src/c/main.c` — all changes are in this single file

## Current Basalt Layout (144x168)

| Element | X | Y | W | H |
|---------|---|---|---|---|
| Date box | 4 | 6 | 66 | 26 |
| Batt box | 74 | 6 | 66 | 26 |
| Time text | 0 | 32 | 144 | 54 |
| Sleep box | 4 | 90 | 66 | 26 |
| Step box | 74 | 90 | 66 | 26 |
| Bell curve | 22 | 120 | 100 | 34 |

**Layout formula (derived from Basalt):**
- Margin: 4px
- Gap between paired boxes: 4px
- Box width: `(screen_w - 2*margin - gap) / 2`
- Box right X: `margin + box_w + gap`
- Time Y: `~19% of screen_h`
- Stats Y: `~54% of screen_h`
- Curve Y: `~71% of screen_h`
- Curve width: `~69% of screen_w`
- Curve X: centered `(screen_w - curve_w) / 2`
- Curve height: `~20% of screen_h`

## Target Emery Layout (200x228)

| Element | X | Y | W | H |
|---------|---|---|---|---|
| Date box | 4 | 8 | 94 | 26 |
| Batt box | 102 | 8 | 94 | 26 |
| Time text | 0 | 43 | 200 | 54 |
| Sleep box | 4 | 122 | 94 | 26 |
| Step box | 102 | 122 | 94 | 26 |
| Bell curve | 31 | 162 | 138 | 46 |

---

### Task 1: Add Layout struct and compute from bounds

**Files:**
- Modify: `src/c/main.c:14-22` (add struct), `src/c/main.c:61-76` (replace defines)

- [ ] **Step 1: Define the Layout struct and static instance**

Add after the `Theme` struct (before the window/layer declarations). Replace the hardcoded `#define` layout props section.

```c
// --- Layout (computed from screen bounds in window_load) ---
#define BOX_RADIUS 4
#define LAYOUT_MARGIN 4
#define LAYOUT_GAP 4
#define CURVE_LUT_SIZE 100
#define CURVE_HEIGHT_LUT 30  // max value in s_curve_points lookup table

typedef struct {
  GRect date_box;
  GRect batt_box;
  GRect sleep_box;
  GRect step_box;
  GRect time_frame;
  GRect date_frame;   // text inset within date_box
  GRect batt_frame;   // text inset within batt_box
  GRect sleep_frame;  // text inset within sleep_box
  GRect step_frame;   // text inset within step_box
  int curve_x;
  int curve_y;
  int curve_w;
  int curve_h;
} Layout;

static Layout s_layout;
```

- [ ] **Step 2: Write `compute_layout()` function**

Add after the Layout struct. This derives all positions from screen width/height.

```c
static void compute_layout(GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;
  int box_w = (w - 2 * LAYOUT_MARGIN - LAYOUT_GAP) / 2;
  int right_x = LAYOUT_MARGIN + box_w + LAYOUT_GAP;

  // Row 1: Top info boxes
  int row1_y = (h * 6) / 168;  // ~3.6% from top
  s_layout.date_box  = GRect(LAYOUT_MARGIN, row1_y, box_w, 26);
  s_layout.batt_box  = GRect(right_x, row1_y, box_w, 26);

  // Time: centered below row 1
  int time_y = (h * 32) / 168;
  s_layout.time_frame = GRect(0, time_y, w, 54);

  // Row 2: Stats boxes
  int row2_y = (h * 90) / 168;
  s_layout.sleep_box = GRect(LAYOUT_MARGIN, row2_y, box_w, 26);
  s_layout.step_box  = GRect(right_x, row2_y, box_w, 26);

  // Text frames inset 2px vertically from box origins
  s_layout.date_frame  = GRect(LAYOUT_MARGIN, row1_y + 2, box_w, 24);
  s_layout.batt_frame  = GRect(right_x, row1_y + 2, box_w, 24);
  s_layout.sleep_frame = GRect(LAYOUT_MARGIN, row2_y + 2, box_w, 24);
  s_layout.step_frame  = GRect(right_x, row2_y + 2, box_w, 24);

  // Bell curve: ~69% of width, centered, bottom portion of screen
  s_layout.curve_w = (w * 100) / 144;
  s_layout.curve_x = (w - s_layout.curve_w) / 2;
  s_layout.curve_y = (h * 120) / 168;
  s_layout.curve_h = (h * 34) / 168;
}
```

- [ ] **Step 3: Remove old hardcoded `#define` layout props**

Delete these lines entirely:

```c
// DELETE:
#define DATE_RECT GRect(4, 6, 66, 26)
#define BATT_RECT GRect(74, 6, 66, 26)
#define SLEEP_RECT GRect(4, 90, 66, 26)
#define STEP_RECT  GRect(74, 90, 66, 26)
#define CURVE_WIDTH 100
#define CURVE_HEIGHT 34
#define CURVE_X 22
#define CURVE_Y 120
```

- [ ] **Step 4: Commit**

```bash
git add src/c/main.c
git commit -m "feat: add Layout struct computed from screen bounds"
```

---

### Task 2: Update `map_z_score()` to accept curve width

**Files:**
- Modify: `src/c/main.c:85-89`

- [ ] **Step 1: Update `map_z_score` to use variable curve width**

The current function hardcodes `50` as center and `16` as the scale factor. These derive from a 100px curve (center=50, scale=100/2/~3σ≈16). Generalize to any width.

```c
static int map_z_score(int value, int mean, int sd, int curve_w) {
  int center = curve_w / 2;
  int scale = curve_w * 16 / CURVE_LUT_SIZE;
  int diff = value - mean;
  int px_offset = (diff * scale) / sd;
  return center + px_offset;
}
```

- [ ] **Step 2: Update all call sites**

In `canvas_update_proc`, change:
```c
// old:
int step_x_local = map_z_score(s_step_count, STEPS_MEAN, STEPS_SD);
// new:
int step_x_local = map_z_score(s_step_count, STEPS_MEAN, STEPS_SD, s_layout.curve_w);
```

```c
// old:
int sleep_x_local = map_z_score(s_sleep_seconds, SLEEP_MEAN_SEC, SLEEP_SD_SEC);
// new:
int sleep_x_local = map_z_score(s_sleep_seconds, SLEEP_MEAN_SEC, SLEEP_SD_SEC, s_layout.curve_w);
```

- [ ] **Step 3: Commit**

```bash
git add src/c/main.c
git commit -m "feat: generalize map_z_score for variable curve width"
```

---

### Task 3: Update `canvas_update_proc` to use Layout

**Files:**
- Modify: `src/c/main.c:92-162`

- [ ] **Step 1: Update box drawing to use s_layout**

```c
graphics_draw_round_rect(ctx, s_layout.date_box, BOX_RADIUS);
graphics_draw_round_rect(ctx, s_layout.batt_box, BOX_RADIUS);
graphics_draw_round_rect(ctx, s_layout.sleep_box, BOX_RADIUS);
graphics_draw_round_rect(ctx, s_layout.step_box, BOX_RADIUS);
```

- [ ] **Step 2: Update bell curve drawing with LUT interpolation**

Replace the curve section. The key change: iterate over `s_layout.curve_w` pixels, map each back to the 100-point LUT, and scale heights by `s_layout.curve_h`.

```c
  // 2. Bell Curve (scaled from LUT)
  int bottom_y = s_layout.curve_y + s_layout.curve_h;
  int origin_x = s_layout.curve_x;
  int cw = s_layout.curve_w;

  int step_x_local = map_z_score(s_step_count, STEPS_MEAN, STEPS_SD, cw);
  if (step_x_local < 0) step_x_local = 0;
  if (step_x_local >= cw) step_x_local = cw - 1;

  // Draw filled portion (left of step marker)
  graphics_context_set_stroke_color(ctx, s_theme.fill_steps);
  for (int i = 0; i < step_x_local && i < cw; i++) {
    int lut_i = (i * CURVE_LUT_SIZE) / cw;
    int line_h = (s_curve_points[lut_i] * s_layout.curve_h) / CURVE_HEIGHT_LUT;
    if (line_h < 1) continue;
    graphics_draw_line(ctx,
      GPoint(origin_x + i, bottom_y),
      GPoint(origin_x + i, bottom_y - line_h));
  }

  // Draw unfilled portion (right of step marker)
  graphics_context_set_stroke_color(ctx, s_theme.axis_dim);
  for (int i = step_x_local; i < cw; i++) {
    int lut_i = (i * CURVE_LUT_SIZE) / cw;
    int line_h = (s_curve_points[lut_i] * s_layout.curve_h) / CURVE_HEIGHT_LUT;
    if (line_h < 1) continue;
    graphics_draw_line(ctx,
      GPoint(origin_x + i, bottom_y),
      GPoint(origin_x + i, bottom_y - line_h));
  }
```

- [ ] **Step 3: Update step end-bar drawing**

```c
  if (step_x_local > 0 && step_x_local < cw) {
    int lut_i = (step_x_local * CURVE_LUT_SIZE) / cw;
    int lut_i_prev = ((step_x_local - 1) * CURVE_LUT_SIZE) / cw;
    int h1 = (s_curve_points[lut_i] * s_layout.curve_h) / CURVE_HEIGHT_LUT;
    int h2 = (s_curve_points[lut_i_prev] * s_layout.curve_h) / CURVE_HEIGHT_LUT;
    graphics_context_set_stroke_color(ctx, s_theme.fill_steps);
    graphics_draw_line(ctx, GPoint(origin_x + step_x_local, bottom_y),
                            GPoint(origin_x + step_x_local, bottom_y - h1));
    graphics_draw_line(ctx, GPoint(origin_x + step_x_local - 1, bottom_y),
                            GPoint(origin_x + step_x_local - 1, bottom_y - h2));
  }
```

- [ ] **Step 4: Update sleep indicator**

```c
  if (s_sleep_seconds > 3600) {
    int sleep_x_local = map_z_score(s_sleep_seconds, SLEEP_MEAN_SEC, SLEEP_SD_SEC, cw);
    if (sleep_x_local >= 0 && sleep_x_local < cw) {
      graphics_context_set_fill_color(ctx, s_theme.line_sleep);
      int lut_i = (sleep_x_local * CURVE_LUT_SIZE) / cw;
      int curve_h = (s_curve_points[lut_i] * s_layout.curve_h) / CURVE_HEIGHT_LUT;
      int protrusion = (8 * s_layout.curve_h) / 34;  // scale protrusion proportionally
      int total_h = curve_h + protrusion;
      graphics_fill_rect(ctx,
        GRect(origin_x + sleep_x_local - 1, bottom_y - total_h, 2, total_h),
        0, GCornerNone);
    }
  }
```

- [ ] **Step 5: Update axis ticks to use proportional positions**

The ticks are at LUT positions 18, 34, 50, 66, 82 (the -2σ, -1σ, μ, +1σ, +2σ marks). Scale to actual curve width.

```c
  // 4. Axis Ticks (at sigma positions)
  graphics_context_set_stroke_color(ctx, s_theme.accent);
  graphics_draw_line(ctx, GPoint(origin_x, bottom_y), GPoint(origin_x + cw, bottom_y));

  int tick_mu  = cw / 2;               // center (μ)
  int tick_p1  = (cw * 66) / 100;      // +1σ
  int tick_m1  = (cw * 34) / 100;      // -1σ
  int tick_p2  = (cw * 82) / 100;      // +2σ
  int tick_m2  = (cw * 18) / 100;      // -2σ

  graphics_draw_line(ctx, GPoint(origin_x + tick_mu, bottom_y), GPoint(origin_x + tick_mu, bottom_y + 4));
  graphics_draw_line(ctx, GPoint(origin_x + tick_m1, bottom_y), GPoint(origin_x + tick_m1, bottom_y + 3));
  graphics_draw_line(ctx, GPoint(origin_x + tick_p1, bottom_y), GPoint(origin_x + tick_p1, bottom_y + 3));
  graphics_draw_line(ctx, GPoint(origin_x + tick_m2, bottom_y), GPoint(origin_x + tick_m2, bottom_y + 2));
  graphics_draw_line(ctx, GPoint(origin_x + tick_p2, bottom_y), GPoint(origin_x + tick_p2, bottom_y + 2));
```

- [ ] **Step 6: Commit**

```bash
git add src/c/main.c
git commit -m "feat: scale canvas drawing to Layout dimensions"
```

---

### Task 4: Update `window_load` to use Layout

**Files:**
- Modify: `src/c/main.c:236-288`

- [ ] **Step 1: Call `compute_layout()` and use Layout for text layers**

At the start of `window_load`, after getting bounds:

```c
static void window_load(Window *window) {
  update_theme_colors();
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  compute_layout(bounds);
  window_set_background_color(window, s_theme.bg);
```

Then replace all hardcoded GRect values in text layer creation:

```c
  s_time_layer = text_layer_create(s_layout.time_frame);
  // ...
  s_date_layer = text_layer_create(s_layout.date_frame);
  // ...
  s_bat_layer = text_layer_create(s_layout.batt_frame);
  // ...
  s_sleep_label = text_layer_create(s_layout.sleep_frame);
  // ...
  s_step_label = text_layer_create(s_layout.step_frame);
```

- [ ] **Step 2: Commit**

```bash
git add src/c/main.c
git commit -m "feat: window_load uses computed Layout for all text layers"
```

---

### Task 5: Visual verification on both emulators

- [ ] **Step 1: Build**

```bash
pebble clean && pebble build
```

Expected: builds successfully for both basalt and emery targets.

- [ ] **Step 2: Test on Basalt emulator**

```bash
pebble install --emulator basalt
```

Verify: layout matches the original screenshot — boxes, time, stats, bell curve all in correct positions.

- [ ] **Step 3: Test on Emery emulator**

```bash
pebble install --emulator emery
```

Verify: layout scales to fill the 200x228 screen — wider boxes, larger bell curve, proportional vertical spacing. No clipping, no dead space on the right.

- [ ] **Step 4: Take updated screenshots and save to docs/**

Replace `docs/emery.png` with the corrected Emery screenshot.

- [ ] **Step 5: Final commit**

```bash
git add docs/
git commit -m "docs: update emery screenshot after layout scaling"
```
