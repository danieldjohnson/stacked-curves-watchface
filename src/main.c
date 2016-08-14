#include <pebble.h>
#include "mathutil.h"

static Window *s_main_window;
static Layer *s_draw_layer;

struct tm last_time;
int64_t time_sec;
bool seconds_precision = true;
int seconds_precision_countdown = 0;

#define TICK_COLOR PBL_IF_COLOR_ELSE(GColorLightGray , GColorWhite)
#define MINUTE_COVER_COLOR GColorBlack
#define HOUR_SPIRAL_COLOR GColorWhite

#define NUM_POINTS 6
#define START_BOLD_POINT 2
#define NUM_BEHIND_ECHOES 15
#define NUM_AHEAD_ECHOES 15
#define FOLLOW_DISTANCE 8

#define SECONDS_PRECISION_MAX_COUNTDOWN 30

static void update_time() {
  // Get a tm structure
  time_t temp = time(NULL); 
  struct tm *tick_time = localtime(&temp);
  last_time = *tick_time;
  time_sec = temp - time_start_of_today();
}

static void update_graphics(){
  layer_mark_dirty(s_draw_layer);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed);

static void schedule_with_precision(bool should_use_seconds){
  if(should_use_seconds){
    tick_timer_service_subscribe(SECOND_UNIT, tick_handler);
    seconds_precision_countdown = SECONDS_PRECISION_MAX_COUNTDOWN;
  }else{
    tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  }
  seconds_precision = should_use_seconds;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  if(seconds_precision){
    seconds_precision_countdown--;
    if(seconds_precision_countdown <= 0){
      schedule_with_precision(false);
    }
  }
  update_time();
  update_graphics();
}

static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  schedule_with_precision(true);
}

static __inline__ GPoint get_follow(GPoint prev, GPoint follow) {
  GPoint vector = GPoint(follow.x-prev.x, follow.y-prev.y);
  int32_t distance = isqrt(vector.x*vector.x + vector.y*vector.y);
  if(distance == 0) return follow;
  vector.x = (vector.x*FOLLOW_DISTANCE) / distance;
  vector.y = (vector.y*FOLLOW_DISTANCE) / distance;
  GPoint result = GPoint(follow.x-vector.x, follow.y-vector.y);
//   APP_LOG(APP_LOG_LEVEL_DEBUG , "Follow from %d,%d to %d,%d -> distance %d -> %d,%d", prev.x, prev.y, follow.x, follow.y, (int)distance, result.x, result.y);
  return result;
}

static __inline__ GPoint get_unfollow(GPoint prev, GPoint cur) {
  GPoint vector = GPoint(cur.x-prev.x, cur.y-prev.y);
  int32_t distance = isqrt(vector.x*vector.x + vector.y*vector.y);
  if(distance == 0) return cur;
  vector.x = (vector.x*FOLLOW_DISTANCE) / distance;
  vector.y = (vector.y*FOLLOW_DISTANCE) / distance;
  GPoint result = GPoint(cur.x+vector.x, cur.y+vector.y);
//   APP_LOG(APP_LOG_LEVEL_DEBUG , "Unfollow from %d,%d to %d,%d -> distance %d -> %d,%d", prev.x, prev.y, cur.x, cur.y, (int)distance, result.x, result.y);
  return result;
}

#define INDEX_POINTS(pts,i) all_points[i+NUM_BEHIND_ECHOES]
static void draw_lines_and_echoes(GContext *ctx, GPoint points[NUM_POINTS], int point_limit){
  GPoint all_points[1+NUM_BEHIND_ECHOES+NUM_AHEAD_ECHOES][NUM_POINTS];
  int i,p;
          
  memcpy(INDEX_POINTS(all_points,0), points, sizeof(GPoint)*NUM_POINTS);
  
  for(i=1; i<=NUM_BEHIND_ECHOES; i++){
    GPoint *active_echo = INDEX_POINTS(all_points,-i);
    GPoint *follow_echo = INDEX_POINTS(all_points,-i+1);
    active_echo[0] = points[0];
    for(p=1; p<point_limit; p++){
      GPoint prev_pt = active_echo[p-1];
      GPoint cur_follow_pt = follow_echo[p];
      active_echo[p] = get_follow(prev_pt, cur_follow_pt);
    }
  }
  for(i=1; i<=NUM_AHEAD_ECHOES; i++){
    GPoint *active_echo = INDEX_POINTS(all_points,i);
    GPoint *unfollow_echo = INDEX_POINTS(all_points,i-1);
    active_echo[0] = points[0];
    for(p=1; p<point_limit; p++){
      GPoint unfollow_prev_pt = unfollow_echo[p-1];
      GPoint unfollow_cur_pt = unfollow_echo[p];
      active_echo[p] = get_unfollow(unfollow_prev_pt, unfollow_cur_pt);
    }
  }
  
  #if defined(PBL_COLOR)
  GColor line_colors[] = {GColorPurple, GColorRed, GColorChromeYellow, GColorGreen, GColorVividCerulean};
  #else
  GColor line_colors[] = {GColorWhite, GColorWhite, GColorWhite, GColorWhite, GColorWhite};
  #endif
  
  graphics_context_set_stroke_width(ctx, 1);
  for(int p=1; p<point_limit; p++){
    graphics_context_set_stroke_color(ctx, line_colors[p-1]);
    for(i=-NUM_BEHIND_ECHOES; i<=NUM_AHEAD_ECHOES; i++){
      GPoint *cpoints = INDEX_POINTS(all_points,i);
      graphics_draw_line(ctx, cpoints[p-1], cpoints[p]);
    }
  }
  
  graphics_context_set_stroke_width(ctx, 7);
  graphics_context_set_stroke_color(ctx, GColorBlack);
  for(int p=1; p<point_limit; p++){
    graphics_draw_line(ctx, points[p-1], INDEX_POINTS(all_points,0)[p]);
  }
  graphics_context_set_stroke_width(ctx, 3);
  for(int p=1; p<point_limit; p++){
    #if !defined(PBL_COLOR)
    if(p <= START_BOLD_POINT)
      graphics_context_set_stroke_width(ctx, 1);
    else
      graphics_context_set_stroke_width(ctx, 3);
    #endif
    graphics_context_set_stroke_color(ctx, line_colors[p-1]);
    graphics_draw_line(ctx, points[p-1], INDEX_POINTS(all_points,0)[p]);
  }
  
}

static GPoint get_angular_point(GPoint center, int r, int32_t angle) {
  return GPoint( center.x + (r * cos_lookup(angle)) / TRIG_MAX_RATIO,
                 center.y + (r * sin_lookup(angle)) / TRIG_MAX_RATIO);
}

static __inline__ int max(int a, int b){
  return a<b ? b : a;
}
static __inline__ int min(int a, int b){
  return a<b ? a : b;
}

#define CLOCK_ANGLE(x,outof) (((x)*TRIG_MAX_ANGLE)/(outof) - TRIG_MAX_ANGLE/4) 
#define YEAR_ANGLE(time) CLOCK_ANGLE(time.tm_yday, 365)
#define MONTH_ANGLE(time) CLOCK_ANGLE(time.tm_yday*12, 365)
#define HOUR_ANGLE(x) CLOCK_ANGLE(x,SECONDS_PER_HOUR*12)
#define MIN_ANGLE(x) CLOCK_ANGLE(x,SECONDS_PER_HOUR)
#define SEC_ANGLE(x) CLOCK_ANGLE(x,SECONDS_PER_MINUTE)
static void draw_update_proc(struct Layer *layer, GContext *ctx){
  GRect bounds = layer_get_bounds(layer);
    
  GPoint points[NUM_POINTS];
  points[2] = GPoint(bounds.size.w/2, bounds.size.h/2);
  
  points[1] = get_angular_point(points[2], bounds.size.w/3, MONTH_ANGLE(last_time) + TRIG_MAX_ANGLE/2);
  points[0] = get_angular_point(points[1], bounds.size.h + bounds.size.w, YEAR_ANGLE(last_time) + TRIG_MAX_ANGLE/2);
  
  points[3] = get_angular_point(points[2], bounds.size.w/4, HOUR_ANGLE(time_sec));
  points[4] = get_angular_point(points[3], bounds.size.w/6, MIN_ANGLE(time_sec));
  
  int sec_radius = bounds.size.w / 8;
  sec_radius = max(sec_radius, 0);
  points[5] = get_angular_point(points[4], sec_radius, SEC_ANGLE(time_sec));
  
  draw_lines_and_echoes(ctx, points, (seconds_precision ? NUM_POINTS : NUM_POINTS-1));
}

static void main_window_load(Window *window) {
  // Get information about the Window
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  
  // Create a drawing layer
  s_draw_layer = layer_create(bounds);
  layer_set_update_proc(s_draw_layer, draw_update_proc);
  
  update_graphics();

  // Add it as a child layer to the Window's root layer
  layer_add_child(window_layer, s_draw_layer);
}

static void main_window_unload(Window *window) {
  layer_destroy(s_draw_layer);
}

static void init() {
  // Create main Window element and assign to pointer
  s_main_window = window_create();

  // Set handlers to manage the elements inside the Window
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .unload = main_window_unload
  });
  
  window_set_background_color(s_main_window, GColorBlack);
  
  // Make sure the time is displayed from the start
  update_time();

  // Show the Window on the watch, with animated=true
  window_stack_push(s_main_window, true);
  
  // Subscribe to events
  schedule_with_precision(true);
  accel_tap_service_subscribe(accel_tap_handler);
}

static void deinit() {
  // Destroy Window
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}