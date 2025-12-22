#include <pebble.h>

// --- Config ---
#define HOUR_DAY_START 7
#define HOUR_DAY_END 20

// Stats Settings
#define STEPS_MEAN 7500
#define STEPS_SD 1000
#define SLEEP_MEAN_SEC 27000 // 7.5 Hours
#define SLEEP_SD_SEC 3600    // 1.0 Hour

// --- Solarized Palette ---
typedef struct {
  GColor bg;
  GColor text;
  GColor accent;
  GColor border;
  GColor fill_steps; // Step Fill Color
  GColor line_sleep; // Sleep Indicator Line
  GColor axis_dim;   // Dim axis ticks
} Theme;

static Theme s_theme;
static bool s_is_dark_mode = true;

static void update_theme_colors() {
  if (s_is_dark_mode) {
    s_theme.bg = GColorOxfordBlue;       // Base03
    s_theme.text = GColorWhite;           // High Contrast
    s_theme.accent = GColorTiffanyBlue;   // Cyan
    s_theme.border = GColorTiffanyBlue;
    s_theme.fill_steps = GColorJaegerGreen; // Green for activity
    s_theme.line_sleep = GColorMagenta;     // Magenta for sleep (Contrasts cyan/green)
    s_theme.axis_dim = GColorDarkGray;      // Base01
  } else {
    s_theme.bg = GColorWhite;             // Base3
    s_theme.text = GColorOxfordBlue;      // Base00
    s_theme.accent = GColorTiffanyBlue;
    s_theme.border = GColorOxfordBlue;
    s_theme.fill_steps = GColorMayGreen;
    s_theme.line_sleep = GColorPurple; // Darker purple for visibility on light
    s_theme.axis_dim = GColorLightGray;   // Base2
  }
}

static Window *s_window;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_bat_layer;

static int s_battery_level = 100;
static int s_step_count = 0;
static int s_sleep_seconds = 0;
static char s_bat_buffer[8];

// --- Layout Props ---
#define BOX_RADIUS 4
#define DATE_RECT GRect(4, 8, 66, 26)
// Top Right is now Battery
#define BATT_RECT GRect(74, 8, 66, 26) 

// Refined Bell Curve (Smaller, room for annotations)
#define CURVE_WIDTH 100
#define CURVE_HEIGHT 34
#define CURVE_X 22
#define CURVE_Y 115

// Hardcoded Gaussian Curve (Width 100, Sigma ~16)
static const int8_t s_curve_points[] = {
  0,0,0,0,0,1,1,1,1,1,2,2,2,3,3,4,4,5,5,6,6,7,8,9,9,10,11,12,13,14,15,16,17,18,19,20,
  21,22,23,24,25,26,27,27,28,29,29,30,30,30,30,30,30,29,29,28,27,27,26,25,24,23,22,21,
  20,19,18,17,16,15,14,13,12,11,10,9,9,8,7,6,6,5,5,4,4,3,3,2,2,2,1,1,1,1,1,0,0,0,0,0
};

// --- Math Helper: Z-Score to Pixel ---
// Center = 50px offset. 1 SD = 16px wide.
static int map_z_score(int value, int mean, int sd) {
  int diff = value - mean;
  // (diff / sd) * 16px scale
  int px_offset = (diff * 16) / sd; 
  return 50 + px_offset; // 50 is center of 100px width
}

// --- Canvas Update ---
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  // 1. Draw Top Boxes
  graphics_context_set_stroke_color(ctx, s_theme.border);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_round_rect(ctx, DATE_RECT, BOX_RADIUS);
  graphics_draw_round_rect(ctx, BATT_RECT, BOX_RADIUS);

  // 2. Draw Curve with Hybrid Data
  int bottom_y = CURVE_Y + CURVE_HEIGHT;
  int origin_x = CURVE_X;

  // A. Calculate Fill Width based on Steps Z-Score
  // Left bound (x=0) corresponds to roughly -3 SD (4500 steps)
  // Fill up to the current Z-score
  int step_x_local = map_z_score(s_step_count, STEPS_MEAN, STEPS_SD);
  if (step_x_local < 0) step_x_local = 0;
  if (step_x_local > CURVE_WIDTH) step_x_local = CURVE_WIDTH;

  for (int i = 0; i < CURVE_WIDTH; i++) {
    int line_h = s_curve_points[i];
    if (line_h < 1) continue; 

    // Fill Logic: Steps
    if (i < step_x_local) {
       graphics_context_set_stroke_color(ctx, s_theme.fill_steps);
    } else {
       graphics_context_set_stroke_color(ctx, s_theme.axis_dim); // Empty part dim
    }
    
    graphics_draw_line(ctx, 
      GPoint(origin_x + i, bottom_y), 
      GPoint(origin_x + i, bottom_y - line_h)
    );
  }
  
  // 3. Sleep Indicator (Static Line)
  // Only draw if we have valid sleep data (> 1 hour to avoid zeros)
  if (s_sleep_seconds > 3600) {
    int sleep_x_local = map_z_score(s_sleep_seconds, SLEEP_MEAN_SEC, SLEEP_SD_SEC);
    if (sleep_x_local >= 0 && sleep_x_local < CURVE_WIDTH) {
       graphics_context_set_stroke_color(ctx, s_theme.line_sleep);
       // Draw a taller, distinct line
       int curve_h_at_x = s_curve_points[sleep_x_local];
       graphics_draw_line(ctx, 
         GPoint(origin_x + sleep_x_local, bottom_y), 
         GPoint(origin_x + sleep_x_local, bottom_y - curve_h_at_x - 4) // Poke out top
       );
    }
  }

  // 4. Axis Decorations (Ticks)
  graphics_context_set_stroke_color(ctx, s_theme.accent);
  // Baseline
  graphics_draw_line(ctx, GPoint(origin_x, bottom_y), GPoint(origin_x + CURVE_WIDTH, bottom_y));
  
  // Mean Tick (Center)
  graphics_draw_line(ctx, GPoint(origin_x + 50, bottom_y), GPoint(origin_x + 50, bottom_y + 4));
  // -1 SD Tick (50 - 16 = 34)
  graphics_draw_line(ctx, GPoint(origin_x + 34, bottom_y), GPoint(origin_x + 34, bottom_y + 3));
  // +1 SD Tick (50 + 16 = 66)
  graphics_draw_line(ctx, GPoint(origin_x + 66, bottom_y), GPoint(origin_x + 66, bottom_y + 3));
}

// --- Updates ---
static void update_time() {
  time_t temp = time(NULL);
  struct tm *tick_time = localtime(&temp);

  // Auto Theme Switch
  bool is_day = (tick_time->tm_hour >= HOUR_DAY_START && tick_time->tm_hour < HOUR_DAY_END);
  bool new_dark_state = !is_day;
  
  if (s_is_dark_mode != new_dark_state) {
    s_is_dark_mode = new_dark_state;
    update_theme_colors();
    window_set_background_color(s_window, s_theme.bg);
    text_layer_set_text_color(s_time_layer, s_theme.text);
    text_layer_set_text_color(s_date_layer, s_theme.accent);
    text_layer_set_text_color(s_bat_layer, s_theme.text);
    layer_mark_dirty(s_canvas_layer);
  }

  // Time
  static char s_time_buffer[8];
  strftime(s_time_buffer, sizeof(s_time_buffer), clock_is_24h_style() ? "%H:%M" : "%I:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buffer);

  // Date
  static char s_date_buffer[8];
  strftime(s_date_buffer, sizeof(s_date_buffer), "%m/%d", tick_time);
  text_layer_set_text(s_date_layer, s_date_buffer);
}

static void update_battery(BatteryChargeState charge) {
  s_battery_level = charge.charge_percent;
  snprintf(s_bat_buffer, sizeof(s_bat_buffer), "%d%%", s_battery_level);
  text_layer_set_text(s_bat_layer, s_bat_buffer);
}

static void update_health() {
#if defined(PBL_HEALTH)
  time_t start = time_start_of_today();
  time_t end = time(NULL);

  // Steps
  HealthMetric metric = HealthMetricStepCount;
  HealthServiceAccessibilityMask mask = health_service_metric_accessible(metric, start, end);
  if (mask & HealthServiceAccessibilityMaskAvailable) {
    s_step_count = (int)health_service_sum_today(metric);
  }
  
  // Sleep (Sum of SleepSeconds for today)
  // Simplified: Asking for sleep sum today might mostly be 0 if 'today' reset at midnight.
  // Better: Sleep logic often looks at 'last night', but let's stick to 'SleepSeconds' sum for now or 
  // assume the user wants 'Sleep RestfulSeconds' etc. 
  // Let's use generic SleepSeconds.
  s_sleep_seconds = (int)health_service_sum_today(HealthMetricSleepSeconds);
  
  layer_mark_dirty(s_canvas_layer);
#endif
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_time();
  // Update health only hourly to save battery
  if (units_changed & HOUR_UNIT) {
    update_health();
  }
}

static void window_load(Window *window) {
  update_theme_colors(); 
  
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  window_set_background_color(window, s_theme.bg);

  // 1. Canvas
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(root, s_canvas_layer);

  // 2. Time
  s_time_layer = text_layer_create(GRect(0, 42, bounds.size.w, 60));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_theme.text);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));

  // 3. Date
  s_date_layer = text_layer_create(GRect(4, 11, 66, 24));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, s_theme.accent);
  text_layer_set_font(s_date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_date_layer));
  
  // 4. Battery (Top Right)
  s_bat_layer = text_layer_create(GRect(74, 11, 66, 24));
  text_layer_set_background_color(s_bat_layer, GColorClear);
  text_layer_set_text_color(s_bat_layer, s_theme.text);
  text_layer_set_font(s_bat_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_bat_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_bat_layer));
}

static void window_unload(Window *window) {
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_bat_layer);
  layer_destroy(s_canvas_layer);
}

static void init() {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT | HOUR_UNIT, tick_handler);
  battery_state_service_subscribe(update_battery);
  
  update_time();
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
