#include <pebble.h>

// --- Config ---
#define HOUR_DAY_START 7
#define HOUR_DAY_END 20

// Stats Settings
#define STEPS_MEAN 7500
#define STEPS_SD 2500
#define SLEEP_MEAN_SEC 27000 // 7.5 Hours
#define SLEEP_SD_SEC 5400    // 1.5 Hours

// --- Solarized Palette ---
typedef struct {
  GColor bg;
  GColor text;
  GColor accent;
  GColor border;
  GColor fill_steps;
  GColor line_sleep;
  GColor axis_dim;
  GColor hr;
} Theme;

static Theme s_theme;
static bool s_is_dark_mode = true;

static void update_theme_colors() {
  if (s_is_dark_mode) {
    s_theme.bg = GColorOxfordBlue;
    s_theme.text = GColorWhite;
    s_theme.accent = GColorCyan;
    s_theme.border = GColorCyan;
    s_theme.fill_steps = GColorChromeYellow;
    s_theme.line_sleep = GColorMagenta;
    s_theme.axis_dim = GColorDarkGray;
    s_theme.hr = GColorRed;
  } else {
    s_theme.bg = GColorWhite;
    s_theme.text = GColorOxfordBlue;
    s_theme.accent = GColorTiffanyBlue;
    s_theme.border = GColorOxfordBlue;
    s_theme.fill_steps = GColorOrange;
    s_theme.line_sleep = GColorPurple;
    s_theme.axis_dim = GColorLightGray;
    s_theme.hr = GColorDarkCandyAppleRed;
  }
}

static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_bat_layer;
static TextLayer *s_sleep_label;
static TextLayer *s_step_label;
static TextLayer *s_hr_resting_label;
static TextLayer *s_hr_current_label;

static int s_step_count = 0;
static int s_sleep_seconds = 0;
static char s_bat_buffer[8];
static char s_sleep_text[16];
static char s_step_text[16];
static int s_hr_resting = 0;
static int s_hr_current = 0;
static char s_hr_resting_text[8];
static char s_hr_current_text[8];

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
  GRect date_frame;
  GRect batt_frame;
  GRect sleep_frame;
  GRect step_frame;
  int curve_x;
  int curve_y;
  int curve_w;
  int curve_h;
  GRect hr_resting_frame;
  GRect hr_current_frame;
  bool show_hr;
  bool is_large;
} Layout;

static Layout s_layout;

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

  int row1_y = (h * 6) / 168;
  s_layout.date_box  = GRect(LAYOUT_MARGIN, row1_y, box_w, box_h);
  s_layout.batt_box  = GRect(right_x, row1_y, box_w, box_h);

  int time_y = (h * 32) / 168;
  s_layout.time_frame = GRect(0, time_y, w, 54);

  int row2_y = large ? (h * 95) / 168 : (h * 90) / 168;
  s_layout.sleep_box = GRect(LAYOUT_MARGIN, row2_y, box_w, box_h);
  s_layout.step_box  = GRect(right_x, row2_y, box_w, box_h);

  int text_inset = (box_h - text_h) / 2;
  s_layout.date_frame  = GRect(LAYOUT_MARGIN, row1_y + text_inset, box_w, text_h);
  s_layout.batt_frame  = GRect(right_x, row1_y + text_inset, box_w, text_h);
  s_layout.sleep_frame = GRect(LAYOUT_MARGIN, row2_y + text_inset, box_w, text_h);
  s_layout.step_frame  = GRect(right_x, row2_y + text_inset, box_w, text_h);

  s_layout.curve_w = (w * 100) / 144;
  s_layout.curve_x = (w - s_layout.curve_w) / 2;
  s_layout.curve_y = (h * 120) / 168;
  s_layout.curve_h = large ? (h * 38) / 168 : (h * 34) / 168;

  if (s_layout.show_hr) {
    int hr_w = 34;
    int hr_h = large ? 28 : 24;
    int hr_y = time_y + (54 - hr_h) / 2;
    s_layout.hr_resting_frame = GRect(2, hr_y, hr_w, hr_h);
    s_layout.hr_current_frame = GRect(w - hr_w - 2, hr_y, hr_w, hr_h);
  }
}

static const int8_t s_curve_points[] = {
  0,0,0,0,0,1,1,1,1,1,2,2,2,3,3,4,4,5,5,6,6,7,8,9,9,10,11,12,13,14,15,16,17,18,19,20,
  21,22,23,24,25,26,27,27,28,29,29,30,30,30,30,30,30,29,29,28,27,27,26,25,24,23,22,21,
  20,19,18,17,16,15,14,13,12,11,10,9,9,8,7,6,6,5,5,4,4,3,3,2,2,2,1,1,1,1,1,0,0,0,0,0
};

// --- Math Helper ---
static int map_z_score(int value, int mean, int sd, int curve_w) {
  int center = curve_w / 2;
  int scale = curve_w * 16 / CURVE_LUT_SIZE;
  int diff = value - mean;
  int px_offset = (diff * scale) / sd;
  return center + px_offset;
}

// --- Canvas Update ---
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  int cw = s_layout.curve_w;
  int origin_x = s_layout.curve_x;
  int bottom_y = s_layout.curve_y + s_layout.curve_h;

  // 1. Boxes (Top & Stats)
  graphics_context_set_stroke_color(ctx, s_theme.border);
  graphics_context_set_stroke_width(ctx, 1);

  graphics_draw_round_rect(ctx, s_layout.date_box, BOX_RADIUS);
  graphics_draw_round_rect(ctx, s_layout.batt_box, BOX_RADIUS);
  graphics_draw_round_rect(ctx, s_layout.sleep_box, BOX_RADIUS);
  graphics_draw_round_rect(ctx, s_layout.step_box, BOX_RADIUS);

  // 2. Bell Curve
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

  // Step End-Bar
  if (step_x_local > 0 && step_x_local < cw) {
    graphics_context_set_stroke_color(ctx, s_theme.fill_steps);
    int lut_i0 = (step_x_local * CURVE_LUT_SIZE) / cw;
    int lut_i1 = ((step_x_local - 1) * CURVE_LUT_SIZE) / cw;
    graphics_draw_line(ctx, GPoint(origin_x + step_x_local, bottom_y),
      GPoint(origin_x + step_x_local, bottom_y - (s_curve_points[lut_i0] * s_layout.curve_h) / CURVE_HEIGHT_LUT));
    graphics_draw_line(ctx, GPoint(origin_x + step_x_local - 1, bottom_y),
      GPoint(origin_x + step_x_local - 1, bottom_y - (s_curve_points[lut_i1] * s_layout.curve_h) / CURVE_HEIGHT_LUT));
  }

  // 3. Sleep Indicator (Dynamic Height + Protrusion)
  if (s_sleep_seconds > 3600) {
    int sleep_x_local = map_z_score(s_sleep_seconds, SLEEP_MEAN_SEC, SLEEP_SD_SEC, cw);
    if (sleep_x_local >= 0 && sleep_x_local < cw) {
      graphics_context_set_fill_color(ctx, s_theme.line_sleep);

      int lut_i = (sleep_x_local * CURVE_LUT_SIZE) / cw;
      int curve_h_at = (s_curve_points[lut_i] * s_layout.curve_h) / CURVE_HEIGHT_LUT;
      int protrusion = (8 * s_layout.curve_h) / 34;
      int total_h = curve_h_at + protrusion;

      graphics_fill_rect(ctx,
        GRect(origin_x + sleep_x_local - 1, bottom_y - total_h, 2, total_h),
        0, GCornerNone);
    }
  }

  // 4. Axis Ticks
  int tick_mu   = cw / 2;
  int tick_sd1r = (cw * 66) / 100;
  int tick_sd1l = (cw * 34) / 100;
  int tick_sd2r = (cw * 82) / 100;
  int tick_sd2l = (cw * 18) / 100;

  graphics_context_set_stroke_color(ctx, s_theme.accent);
  graphics_draw_line(ctx, GPoint(origin_x, bottom_y), GPoint(origin_x + cw, bottom_y));

  graphics_draw_line(ctx, GPoint(origin_x + tick_mu,   bottom_y), GPoint(origin_x + tick_mu,   bottom_y + 4));
  graphics_draw_line(ctx, GPoint(origin_x + tick_sd1l, bottom_y), GPoint(origin_x + tick_sd1l, bottom_y + 3));
  graphics_draw_line(ctx, GPoint(origin_x + tick_sd1r, bottom_y), GPoint(origin_x + tick_sd1r, bottom_y + 3));
  graphics_draw_line(ctx, GPoint(origin_x + tick_sd2l, bottom_y), GPoint(origin_x + tick_sd2l, bottom_y + 2));
  graphics_draw_line(ctx, GPoint(origin_x + tick_sd2r, bottom_y), GPoint(origin_x + tick_sd2r, bottom_y + 2));
}

static void update_time(struct tm *tick_time) {
  // Allow NULL for init() call where no tick_time is available
  time_t temp;
  if (!tick_time) {
    temp = time(NULL);
    tick_time = localtime(&temp);
  }

  bool is_day = (tick_time->tm_hour >= HOUR_DAY_START && tick_time->tm_hour < HOUR_DAY_END);
  bool new_dark_state = !is_day;

  if (s_is_dark_mode != new_dark_state) {
    s_is_dark_mode = new_dark_state;
    update_theme_colors();
    window_set_background_color(s_window, s_theme.bg);
    text_layer_set_text_color(s_time_layer, s_theme.text);
    text_layer_set_text_color(s_date_layer, s_theme.accent);
    text_layer_set_text_color(s_bat_layer, s_theme.text);
    text_layer_set_text_color(s_sleep_label, s_theme.line_sleep);
    text_layer_set_text_color(s_step_label, s_theme.fill_steps);
    if (s_layout.show_hr) {
      text_layer_set_text_color(s_hr_resting_label, s_theme.hr);
      text_layer_set_text_color(s_hr_current_label, s_theme.hr);
    }
    layer_mark_dirty(s_canvas_layer);
  }

  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  static char s_date_buffer[8];
  strftime(s_date_buffer, sizeof(s_date_buffer), "%m/%d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void update_battery(BatteryChargeState charge) {
  snprintf(s_bat_buffer, sizeof(s_bat_buffer), "%d%%", charge.charge_percent);
  text_layer_set_text(s_bat_layer, s_bat_buffer);
}

static void update_health() {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  HealthMetric metric = HealthMetricStepCount;
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_step_count = (int)health_service_sum_today(metric);
  }

  HealthServiceAccessibilityMask sleep_mask = health_service_metric_accessible(
    HealthMetricSleepSeconds, start, end);
  if (sleep_mask & HealthServiceAccessibilityMaskAvailable) {
    s_sleep_seconds = (int)health_service_sum_today(HealthMetricSleepSeconds);
  }

  int sleep_h_x10 = (s_sleep_seconds * 10) / 3600;
  snprintf(s_sleep_text, sizeof(s_sleep_text), "%d.%dh", sleep_h_x10 / 10, sleep_h_x10 % 10);
  text_layer_set_text(s_sleep_label, s_sleep_text);

  snprintf(s_step_text, sizeof(s_step_text), "%d", s_step_count);
  text_layer_set_text(s_step_label, s_step_text);

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

  layer_mark_dirty(s_canvas_layer);
#endif
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time(tick_time);
  if ((tick_time->tm_min % 30 == 0) || (units_changed & HOUR_UNIT)) {
    update_health();
  }
}

static void window_load(Window *window) {
  update_theme_colors();
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  compute_layout(bounds);
  window_set_background_color(window, s_theme.bg);

  const char *info_font_key = s_layout.is_large ?
    FONT_KEY_GOTHIC_24_BOLD : FONT_KEY_GOTHIC_18_BOLD;

  // 1. Canvas
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  // 2. Time
  s_time_layer = text_layer_create(s_layout.time_frame);
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_theme.text);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  // 3. Date (Box Row 1, Left)
  s_date_layer = text_layer_create(s_layout.date_frame);
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, s_theme.accent);
  text_layer_set_font(s_date_layer, fonts_get_system_font(info_font_key));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_date_layer));

  // 4. Battery (Box Row 1, Right)
  s_bat_layer = text_layer_create(s_layout.batt_frame);
  text_layer_set_background_color(s_bat_layer, GColorClear);
  text_layer_set_text_color(s_bat_layer, s_theme.text);
  text_layer_set_font(s_bat_layer, fonts_get_system_font(info_font_key));
  text_layer_set_text_alignment(s_bat_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_bat_layer));

  // 5. Stats Labels (Box Row 2)
  // Sleep (Left Box)
  s_sleep_label = text_layer_create(s_layout.sleep_frame);
  text_layer_set_background_color(s_sleep_label, GColorClear);
  text_layer_set_text_color(s_sleep_label, s_theme.line_sleep);
  text_layer_set_font(s_sleep_label, fonts_get_system_font(info_font_key));
  text_layer_set_text_alignment(s_sleep_label, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_sleep_label));

  // Steps (Right Box)
  s_step_label = text_layer_create(s_layout.step_frame);
  text_layer_set_background_color(s_step_label, GColorClear);
  text_layer_set_text_color(s_step_label, s_theme.fill_steps);
  text_layer_set_font(s_step_label, fonts_get_system_font(info_font_key));
  text_layer_set_text_alignment(s_step_label, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_step_label));

  // 6. Heart Rate (Emery+ only)
  if (s_layout.show_hr) {
    s_hr_resting_label = text_layer_create(s_layout.hr_resting_frame);
    text_layer_set_background_color(s_hr_resting_label, GColorClear);
    text_layer_set_text_color(s_hr_resting_label, s_theme.hr);
    text_layer_set_font(s_hr_resting_label, fonts_get_system_font(info_font_key));
    text_layer_set_text_alignment(s_hr_resting_label, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_hr_resting_label));

    s_hr_current_label = text_layer_create(s_layout.hr_current_frame);
    text_layer_set_background_color(s_hr_current_label, GColorClear);
    text_layer_set_text_color(s_hr_current_label, s_theme.hr);
    text_layer_set_font(s_hr_current_label, fonts_get_system_font(info_font_key));
    text_layer_set_text_alignment(s_hr_current_label, GTextAlignmentCenter);
    layer_add_child(root, text_layer_get_layer(s_hr_current_label));
  }
}

static void window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_bat_layer);
  text_layer_destroy(s_sleep_label);
  text_layer_destroy(s_step_label);
  if (s_hr_resting_label) text_layer_destroy(s_hr_resting_label);
  if (s_hr_current_label) text_layer_destroy(s_hr_current_label);
  layer_destroy(s_canvas_layer);
}

static void init() {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(update_battery);

  update_time(NULL);
  update_battery(battery_state_service_peek());
  update_health();
}

static void deinit() {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
