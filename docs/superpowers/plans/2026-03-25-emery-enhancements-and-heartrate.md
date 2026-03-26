# Emery Enhancements & Heart Rate Display

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enhance the Emery layout with larger elements that better use the screen, and add resting/current heart rate indicators flanking the time display on screens wide enough to support them.

**Architecture:** Extend `compute_layout()` with a width threshold (`w > 144`) to select Emery-specific sizing (taller boxes, bigger curve). Add two new TextLayers for HR data positioned in the margins beside the time. HR data is read from Pebble Health API (`health_service_peek_current_value`) on the existing 30-minute update cycle. On Basalt (144px wide, no HR sensor), the HR layers are not created.

**Tech Stack:** Pebble SDK 4.x C API. Testing via `pebble install --emulator basalt` and `pebble install --emulator emery`.

---

## File Map

- **Modify:** `src/c/main.c` — all changes in this single file

## Design Reference

```
Emery (200x228):
  [  03/25  ]          [  80%   ]     <- taller boxes, GOTHIC_24_BOLD
     R62      11:44      84           <- HR resting (left), HR current (right)
  [  0.0h   ]          [  7500  ]     <- taller boxes, GOTHIC_24_BOLD
          ▁▃▅▇████▇▅▃▁               <- taller curve

Basalt (144x168):
  [03/25]    [80%]                    <- unchanged
       10:24                          <- no HR (no space, no sensor)
  [0.0h]     [7500]                   <- unchanged
       ▁▃▅▇█▇▅▃▁                     <- unchanged
```

Note: Pebble system fonts do not include Unicode heart symbols. HR uses ASCII-safe format: "R62" (resting, left-aligned) and "84" (current, right-aligned).

---

### Task 1: Add HR color to Theme and Emery-aware sizing to Layout

**Files:**
- Modify: `src/c/main.c:14-22` (Theme struct), `src/c/main.c:27-45` (update_theme_colors), `src/c/main.c:68-112` (Layout struct and compute_layout)

- [ ] **Step 1: Add `hr` color to Theme struct**

After line 21 (`GColor axis_dim;`), add:

```c
  GColor hr;
```

- [ ] **Step 2: Set HR colors in update_theme_colors()**

In the dark mode block, add after `s_theme.axis_dim`:
```c
    s_theme.hr = GColorRed;
```

In the light mode block, add after `s_theme.axis_dim`:
```c
    s_theme.hr = GColorDarkCandyAppleRed;
```

- [ ] **Step 3: Add HR frames and sizing flag to Layout struct**

After `int curve_h;` (line 81), add:

```c
  GRect hr_resting_frame;
  GRect hr_current_frame;
  bool show_hr;       // true on screens wide enough for HR flanking time
  bool is_large;      // true on larger screens (controls font selection)
```

- [ ] **Step 4: Update compute_layout() with Emery-aware sizing and HR frames**

Replace the entire `compute_layout` function body:

```c
static void compute_layout(GRect bounds) {
  int w = bounds.size.w;
  int h = bounds.size.h;
  bool large = w > 144;
  s_layout.is_large = large;
  s_layout.show_hr = large;

  int box_h = large ? 32 : 26;
  int text_h = large ? 28 : 24;
  int box_w = (w - 2 * LAYOUT_MARGIN - LAYOUT_GAP) / 2;
  int right_x = LAYOUT_MARGIN + box_w + LAYOUT_GAP;

  // Row 1: Top info boxes
  int row1_y = (h * 6) / 168;
  s_layout.date_box  = GRect(LAYOUT_MARGIN, row1_y, box_w, box_h);
  s_layout.batt_box  = GRect(right_x, row1_y, box_w, box_h);

  // Time: centered below row 1
  int time_y = (h * 32) / 168;
  s_layout.time_frame = GRect(0, time_y, w, 54);

  // Row 2: Stats boxes
  int row2_y = large ? (h * 95) / 168 : (h * 90) / 168;
  s_layout.sleep_box = GRect(LAYOUT_MARGIN, row2_y, box_w, box_h);
  s_layout.step_box  = GRect(right_x, row2_y, box_w, box_h);

  // Text insets (vertically centered in boxes)
  int text_inset = (box_h - text_h) / 2;
  s_layout.date_frame  = GRect(LAYOUT_MARGIN, row1_y + text_inset, box_w, text_h);
  s_layout.batt_frame  = GRect(right_x, row1_y + text_inset, box_w, text_h);
  s_layout.sleep_frame = GRect(LAYOUT_MARGIN, row2_y + text_inset, box_w, text_h);
  s_layout.step_frame  = GRect(right_x, row2_y + text_inset, box_w, text_h);

  // Bell curve
  s_layout.curve_w = (w * 100) / 144;
  s_layout.curve_x = (w - s_layout.curve_w) / 2;
  s_layout.curve_y = (h * 120) / 168;
  s_layout.curve_h = large ? (h * 38) / 168 : (h * 34) / 168;

  // HR frames: flanking the time, vertically centered with time row
  if (s_layout.show_hr) {
    int hr_w = 34;
    int hr_h = large ? 28 : 24;
    int hr_y = time_y + (54 - hr_h) / 2;
    s_layout.hr_resting_frame = GRect(2, hr_y, hr_w, hr_h);
    s_layout.hr_current_frame = GRect(w - hr_w - 2, hr_y, hr_w, hr_h);
  }
}
```

- [ ] **Step 5: Commit**

```bash
git add src/c/main.c
git commit -m "feat: add HR theme color, Emery-aware sizing, HR layout frames"
```

---

### Task 2: Add HR state variables and text layers

**Files:**
- Modify: `src/c/main.c:47-59` (globals), `src/c/main.c:288-340` (window_load), `src/c/main.c:342-349` (window_unload)

- [ ] **Step 1: Add HR state variables**

After `s_step_label` (line 53), add:

```c
static TextLayer *s_hr_resting_label;
static TextLayer *s_hr_current_label;
```

After `s_step_text[16]` (line 59), add:

```c
static int s_hr_resting = 0;
static int s_hr_current = 0;
static char s_hr_resting_text[8];
static char s_hr_current_text[8];
```

- [ ] **Step 2: Create HR text layers in window_load (conditionally)**

At the end of `window_load`, before the closing `}`, add:

```c
  // 6. Heart Rate (Emery+ only)
  if (s_layout.show_hr) {
    const char *hr_font_key = s_layout.is_large ?
      FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD;

    s_hr_resting_label = text_layer_create(s_layout.hr_resting_frame);
    text_layer_set_background_color(s_hr_resting_label, GColorClear);
    text_layer_set_text_color(s_hr_resting_label, s_theme.hr);
    text_layer_set_font(s_hr_resting_label, fonts_get_system_font(hr_font_key));
    text_layer_set_text_alignment(s_hr_resting_label, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_hr_resting_label));

    s_hr_current_label = text_layer_create(s_layout.hr_current_frame);
    text_layer_set_background_color(s_hr_current_label, GColorClear);
    text_layer_set_text_color(s_hr_current_label, s_theme.hr);
    text_layer_set_font(s_hr_current_label, fonts_get_system_font(hr_font_key));
    text_layer_set_text_alignment(s_hr_current_label, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_hr_current_label));
  }
```

- [ ] **Step 3: Destroy HR text layers in window_unload (conditionally)**

In `window_unload`, before `layer_destroy(s_canvas_layer);`, add:

```c
  if (s_hr_resting_label) text_layer_destroy(s_hr_resting_label);
  if (s_hr_current_label) text_layer_destroy(s_hr_current_label);
```

- [ ] **Step 4: Commit**

```bash
git add src/c/main.c
git commit -m "feat: add HR text layers, created conditionally on Emery"
```

---

### Task 3: Read HR data in update_health() and update theme switch

**Files:**
- Modify: `src/c/main.c:253-279` (update_health), `src/c/main.c:216-246` (update_time theme switch)

- [ ] **Step 1: Add HR reading to update_health()**

Inside the `#if defined(PBL_HEALTH)` block, after the sleep text update (after line 275), add:

```c
  // Heart rate (only update display if HR layers exist)
  if (s_layout.show_hr) {
    HealthServiceAccessibilityMask hr_mask = health_service_metric_accessible(
      HealthMetricHeartRateBPM, start, end);
    if (hr_mask & HealthServiceAccessibilityMaskAvailable) {
      s_hr_current = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
    }
    HealthServiceAccessibilityMask rhr_mask = health_service_metric_accessible(
      HealthMetricRestingHeartRateBPM, start, end);
    if (rhr_mask & HealthServiceAccessibilityMaskAvailable) {
      s_hr_resting = (int)health_service_peek_current_value(HealthMetricRestingHeartRateBPM);
    }

    if (s_hr_resting > 0) {
      snprintf(s_hr_resting_text, sizeof(s_hr_resting_text), "R%d", s_hr_resting);
    } else {
      snprintf(s_hr_resting_text, sizeof(s_hr_resting_text), "R--");
    }
    text_layer_set_text(s_hr_resting_label, s_hr_resting_text);

    if (s_hr_current > 0) {
      snprintf(s_hr_current_text, sizeof(s_hr_current_text), "%d", s_hr_current);
    } else {
      snprintf(s_hr_current_text, sizeof(s_hr_current_text), "--");
    }
    text_layer_set_text(s_hr_current_label, s_hr_current_text);
  }
```

Note: Pebble system fonts lack Unicode heart symbols. "R" prefix distinguishes resting HR from current.

- [ ] **Step 2: Update theme switch to include HR labels**

In `update_time()`, inside the `if (s_is_dark_mode != new_dark_state)` block, after `text_layer_set_text_color(s_step_label, s_theme.fill_steps);` (line 235), add:

```c
    if (s_layout.show_hr) {
      text_layer_set_text_color(s_hr_resting_label, s_theme.hr);
      text_layer_set_text_color(s_hr_current_label, s_theme.hr);
    }
```

- [ ] **Step 3: Commit**

```bash
git add src/c/main.c
git commit -m "feat: read HR data on 30-min cycle, update on theme switch"
```

---

### Task 4: Update box/label fonts for Emery sizing

**Files:**
- Modify: `src/c/main.c:288-340` (window_load)

- [ ] **Step 1: Use is_large flag to select fonts in window_load**

Near the top of `window_load`, after `compute_layout(bounds);`, add:

```c
  const char *info_font_key = s_layout.is_large ?
    FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD;
```

Then replace all four `fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD)` calls for date, battery, sleep, and step labels with:

```c
  text_layer_set_font(s_date_layer, fonts_get_system_font(info_font_key));
  // ... (same for s_bat_layer, s_sleep_label, s_step_label)
```

- [ ] **Step 2: Commit**

```bash
git add src/c/main.c
git commit -m "feat: use GOTHIC_24_BOLD for info labels on Emery"
```

---

### Task 5: Update canvas box drawing for new box heights

**Files:**
- Modify: `src/c/main.c:130-142` (canvas_update_proc)

No code changes needed here — `canvas_update_proc` already draws boxes using `s_layout.date_box`, `s_layout.batt_box`, etc. Since `compute_layout()` now produces taller boxes on Emery, the drawing automatically adapts.

- [ ] **Step 1: Verify no hardcoded box dimensions remain in canvas_update_proc**

Grep for `26` or `GRect(` in the canvas function. There should be none — all drawing uses `s_layout.*`.

- [ ] **Step 2: Mark complete (no code changes needed)**

---

### Task 6: Visual verification on both emulators

- [ ] **Step 1: Build**

```bash
pebble clean && pebble build
```

Expected: builds successfully for both basalt and emery targets.

- [ ] **Step 2: Test on Basalt emulator**

```bash
pebble install --emulator basalt
```

Verify:
- Layout identical to before (no HR labels, same box sizes, same fonts)
- No visual regressions

- [ ] **Step 3: Test on Emery emulator**

```bash
pebble install --emulator emery
```

Verify:
- Taller info boxes with GOTHIC_24_BOLD text
- HR resting value left of time, HR current value right of time (may show "--" or "0" in emulator)
- Taller bell curve
- ♡ symbol renders (if not, apply fallback from Task 3 note)
- All elements properly spaced, no overlaps

- [ ] **Step 4: Update screenshots in docs/**

- [ ] **Step 5: Final commit**

```bash
git add docs/ src/c/main.c
git commit -m "feat: Emery enhancements — larger elements, heart rate display"
```
