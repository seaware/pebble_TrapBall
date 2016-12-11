#include "pebble.h"
#include <math.h>
	
// Prototypes.....
static void show_text(char *result_text);

#define MATH_PI 3.141592653589793238462
#define NUM_MAX_DISCS 30
#define NUM_MAX_TRAPS 30
#define DISC_DENSITY 0.25
#define ACCEL_RATIO 0.05
#define GRAVITY_CONSTANT 0.01
#define GAME_PLAY_TIME 600			// number of seconds per game * 10
#define BALL_STARTING_MARGIN 10
	
// Next is the list of keys from JavaScript
#define TB_KEY_player_id 1
#define TB_KEY_player_name 2
#define TB_KEY_n 3
#define TB_KEY_mean 4
#define TB_KEY_min_score 5
#define TB_KEY_max_score 6
#define TB_KEY_max_level 7
#define TB_KEY_variance 8
#define TB_KEY_post_score 9
#define TB_KEY_post_level 10
#define TB_KEY_summary 11
	
typedef struct Vec2d {
  double x;
  double y;
} Vec2d;

typedef struct Disc {
  Vec2d pos;
  Vec2d vel;
  double mass;
  double radius;
  int captured;
  int id;			// IDs of disc 0 is 1, etc.
} Disc;

static Disc discs[NUM_MAX_DISCS];
static Disc traps[NUM_MAX_TRAPS];

static double next_trap_radius = 10;
static double next_disk_radius = 5;
static int num_discs = 1;
static int num_traps = 1;
static int countdown_timer = GAME_PLAY_TIME;		
static int level = 1;
static int score = 0;
static int show_result = false;

// Stat's from web messages
static int n = 0;
static int mean = 0;
static int min_score = 0;
static int max_score = 0;
static int max_level = 0;

// Vibe pattern: ON for 25ms
static const uint32_t const veryShortVibSegments[] = { 25 };
VibePattern veryShortVib = {
.durations = veryShortVibSegments,
.num_segments = ARRAY_LENGTH(veryShortVibSegments),
};

// Vibe pattern: 4 countdown vibs. ON for 25ms every second
static const uint32_t const finalVibSegments[] = { 25, 975, 25, 975, 25, 975, 25 };
VibePattern finalVib = {
.durations = finalVibSegments,
.num_segments = ARRAY_LENGTH(finalVibSegments),
};

static Window *window;
static GRect window_frame;
static Layer *disc_layer;
static Layer *trap_layer;
static Layer *score_layer;
static TextLayer *text_layer;
static ScrollLayer *result_layer;

// Here's our result text to display.
static char result_text[5000];
static char result_text_template[] = "Congratulations %s! You got to level %d and scored %d.\n\n%s";
static char player_name[33];
static const int vert_scroll_text_padding = 4;

static AppTimer *timer;

static double disc_calc_mass(Disc *disc) {
  return MATH_PI * disc->radius * disc->radius * DISC_DENSITY;
}

// This is a poor man's version of sqrt. A built-in function would take less power I believe.
static double root(double x) {
	double y = x/2;				// First guess
	for (int i=0; i<6; i++) {
		y = (x/y + y )/ 2;		// Newton step, repeating increases accuracy
	}	
    return y;
}

// Return true if the two discs are touching
static bool touches(Disc *disc1, Disc *disc2)
{
	double x_dist = disc1->pos.x - disc2->pos.x;
	double y_dist = disc1->pos.y - disc2->pos.y;
	double r_dist = root((x_dist * x_dist) + (y_dist * y_dist));
	return (r_dist < disc1->radius + disc2->radius);
}

static void disc_init(Disc *disc) {
  GRect frame = window_frame;
  disc->pos.x = frame.size.w/2;
  disc->pos.y = frame.size.h/2;
  disc->vel.x = 0;
  disc->vel.y = 0;
  disc->captured = 0;
  disc->radius = next_disk_radius;
  disc->mass = disc_calc_mass(disc);
  next_disk_radius += 0.5;
}

static void trap_init(Disc *disc) {
  GRect frame = window_frame;
  disc->pos.x = next_trap_radius + (rand() % (int)(frame.size.w - (next_trap_radius * 2)));
  disc->pos.y = next_trap_radius + (rand() % (int)(frame.size.h - (next_trap_radius * 2)));
  disc->vel.x = 0;
  disc->vel.y = 0;
  disc->captured = 0;
  disc->radius = next_trap_radius;
  disc->mass = disc_calc_mass(disc);
  //next_trap_radius += 0.5;
}

// Simple level reset for now. Traps are put in random spots
static void next_level(int target_level)
{
   //vibes_short_pulse();
	vibes_enqueue_custom_pattern( veryShortVib );
	
  num_traps = num_discs = target_level;
  if (num_traps > NUM_MAX_TRAPS) num_traps = NUM_MAX_TRAPS;
  if (num_discs > NUM_MAX_DISCS) num_discs = NUM_MAX_DISCS;
  next_disk_radius = 5;
	
  for (int i = 0; i < num_traps; i++) 
  {
	  trap_init(&traps[i]);
	  if (i > 0) for (int j = 0; j < i; j++)
	  {
		  if (touches(&traps[i], &traps[j]))	// ensure traps don't touch each other
		  	do {trap_init(&traps[i]);}
		  	while (touches(&traps[i], &traps[j]));
	  }
	  traps[i].id = i+1;
  }

  int ball_x_offset = ((window_frame.size.w)/2) - ((((num_discs - 1) * BALL_STARTING_MARGIN) + (num_discs * (int) next_disk_radius)) / 2);
  for (int i = 0; i < num_discs; i++) {
	  disc_init(&discs[i]);
	  discs[i].id = i+1;
	  discs[i].pos.x = ball_x_offset;
	  ball_x_offset += (int) next_disk_radius + BALL_STARTING_MARGIN;
  }
}

static void disc_apply_force(Disc *disc, Vec2d force) {
  disc->vel.x += force.x / disc->mass;
  disc->vel.y += force.y / disc->mass;
}

static void trap_apply_force(Disc *disc) {
  for (int i = 0; i < num_traps; i++) {
	  double x_dist = traps[i].pos.x - disc->pos.x;
	  double y_dist = traps[i].pos.y - disc->pos.y;
	  double r_dist = root((x_dist * x_dist) + (y_dist * y_dist));
	  
	  // Make balls stick in center of trap if we get there
	  if ((r_dist < traps[i].radius && traps[i].captured == false) || (traps[i].captured == disc->id))
	  {
		  if (disc->captured == false)
		  {
			  //APP_LOG(APP_LOG_LEVEL_DEBUG, "Ball %d captured into trap %d", disc->id, i);
			  traps[i].captured = disc->id;
			  score += 200;
		  }
		  disc->vel.x = 0;
		  disc->vel.y = 0;
		  disc->pos.x = traps[i].pos.x;
		  disc->pos.y = traps[i].pos.y;
		  disc->captured = true;
	  }
	  else if (disc->captured == false)
	  {
		  double gForce = GRAVITY_CONSTANT * traps[i].mass * disc->mass / (r_dist * r_dist);
		  disc->vel.x += gForce * (x_dist>0) ? 1 : -1;
		  disc->vel.y += gForce * (y_dist>0) ? 1 : -1;		  
	  }
  }
}

static void disc_apply_accel(Disc *disc, AccelData accel) {
  Vec2d force;
  force.x = accel.x * ACCEL_RATIO;
  force.y = -accel.y * ACCEL_RATIO;
  disc_apply_force(disc, force);
  trap_apply_force(disc);
}

static void disc_update(Disc *disc) {
  const GRect frame = window_frame;
  double e = 0.5;
  if ((disc->pos.x - disc->radius < 0 && disc->vel.x < 0)
    || (disc->pos.x + disc->radius > frame.size.w && disc->vel.x > 0)) {
    disc->vel.x = -disc->vel.x * e;
  }
  if ((disc->pos.y - disc->radius < 0 && disc->vel.y < 0)
    || (disc->pos.y + disc->radius > frame.size.h && disc->vel.y > 0)) {
    disc->vel.y = -disc->vel.y * e;
  }
  disc->pos.x += disc->vel.x;
  disc->pos.y += disc->vel.y;
}

static void trap_draw(GContext *ctx, Disc *disc) {
  //graphics_context_set_fill_color(ctx, GColorWhite);
	graphics_context_set_stroke_color(ctx, GColorWhite);
	graphics_draw_circle(ctx, GPoint(disc->pos.x, disc->pos.y), disc->radius);
}

static void disc_draw(GContext *ctx, Disc *disc) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_circle(ctx, GPoint(disc->pos.x, disc->pos.y), disc->radius);
}

static void disc_layer_update_callback(Layer *me, GContext *ctx) {
  for (int i = 0; i < num_discs; i++) {
    disc_draw(ctx, &discs[i]);
  }
}

static void trap_layer_update_callback(Layer *me, GContext *ctx) {
  for (int i = 0; i < num_traps; i++) {
    trap_draw(ctx, &traps[i]);
  }
}

static void score_layer_update_callback(Layer *layer, GContext *ctx) {
     GRect bounds = layer_get_bounds(layer);
     GRect frame = GRect(5, 5, bounds.size.w, 18 + 2);
	 char cbuf[20];
     graphics_context_set_text_color(ctx, GColorWhite);
	 graphics_context_set_fill_color(ctx, GColorBlack);
	 snprintf(cbuf, 20, "%d.%d", countdown_timer/10, countdown_timer % 10);
     graphics_draw_text(ctx, cbuf,
         fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
         frame,
         GTextOverflowModeTrailingEllipsis,
         GTextAlignmentLeft,
         NULL);
	 snprintf(cbuf, 20, "Level %d   ", level);
     graphics_draw_text(ctx, cbuf,
         fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
         frame,
         GTextOverflowModeTrailingEllipsis,
         GTextAlignmentRight,
         NULL);
	 snprintf(cbuf, 20, "Score %d   ", score);
     graphics_draw_text(ctx, cbuf,
         fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
         GRect(5, bounds.size.h-18-2, bounds.size.w, 18 + 2),
         GTextOverflowModeTrailingEllipsis,
         GTextAlignmentRight,
         NULL);
 }

// Format a message to the phone to add our score to the global web results
static void post_score(void) {
	
	Tuplet score_tuple = TupletInteger(TB_KEY_post_score, score);
	Tuplet level_tuple = TupletInteger(TB_KEY_post_level, level);
	
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);
	if (iter == NULL) {
       return;
	}
	dict_write_tuplet(iter, &score_tuple);
	dict_write_tuplet(iter, &level_tuple);
	dict_write_end(iter);
	app_message_outbox_send();
}

// Timer ---------------------------------------------------
// This is called every 100ms to update the balls, to fill the traps, and update the score
static void timer_callback(void *data) {
	AccelData accel = { 0, 0, 0, 0, 0 };
	int captured_count = 0;
	
	// Out of time. Stop game & show results
	if (countdown_timer == 0)
	{
		vibes_short_pulse();
		post_score();
		snprintf(result_text, ARRAY_LENGTH(result_text), result_text_template, player_name, level, score, "" );
		show_text(result_text);
		return;
	}
	
	// Final vib warning before game stops
	if (countdown_timer == 40)
	{
		vibes_cancel();
		vibes_enqueue_custom_pattern(finalVib);
	}
	
	if (countdown_timer > 0) countdown_timer--;
	
	// Get current accelerometer data
	accel_service_peek(&accel);
	if (accel.x > 500 || accel.y > 500) score += 3;

	// Update balls
	for (int i = 0; i < num_discs; i++) {
		Disc *disc = &discs[i];
		disc_apply_accel(disc, accel);
		disc_update(disc);
		if (disc->captured) captured_count++;
	}
	
	if (captured_count == num_discs)
	  next_level(++level);
	
	//layer_mark_dirty(disc_layer);	// rerendered implicitly because they are children of the score layer
	//layer_mark_dirty(trap_layer);
	layer_mark_dirty(score_layer);	

	timer = app_timer_register(100 /* milliseconds */, timer_callback, NULL);
}

static void handle_accel(AccelData *accel_data, uint32_t num_samples) {
  // do nothing
}

// Scroll Layer Handler -----------------------------------------------------
void show_text(char *result_text)
{
	text_layer_set_text(text_layer, result_text);
	// Trim text layer and scroll content to fit text box
	GSize max_size = text_layer_get_content_size(text_layer);
	text_layer_set_size(text_layer, max_size);
	scroll_layer_set_content_size(result_layer, GSize(window_frame.size.w, max_size.h + vert_scroll_text_padding));
	scroll_layer_set_content_offset(result_layer, GPoint(0,0), false);	// Scroll to top of text
	layer_set_hidden(score_layer, true);
	layer_set_hidden(scroll_layer_get_layer(result_layer), false);	
	show_result = true;
}

void scrollLayerChanged(struct ScrollLayer *scroll_layer, void *context)
{
	//do nothing
}

// This is called when select is pressed which resets the game to the beginning 
void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {
	if (show_result)
	{
		level = 1;
		num_discs = 1;
		num_traps = 1;
		countdown_timer = GAME_PLAY_TIME;		
		score = 0;
		layer_set_hidden(scroll_layer_get_layer(result_layer), true);
		layer_set_hidden(score_layer, false);
		show_result = false;
		timer = app_timer_register(100 /* milliseconds */, timer_callback, NULL);
		next_level(level);
	}
 }

void scrollLayerClickSetup(void *context)
{
	 window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler );
}

ScrollLayerCallbacks selectButtonCallback = {
.click_config_provider = scrollLayerClickSetup,
.content_offset_changed_handler = scrollLayerChanged
};

// Setup Window Layers ---------------------------------------------------------------------------
static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect frame = window_frame = layer_get_frame(window_layer);
  GRect max_text_bounds = GRect(0, 0, frame.size.w, 2000);

  // Setup disc Layer -> trap Layer -> score Layer -> Window Layer
  score_layer = layer_create(frame);
  layer_set_update_proc(score_layer, score_layer_update_callback);
  layer_add_child(window_layer, score_layer);
	
  trap_layer = layer_create(frame);
  layer_set_update_proc(trap_layer, trap_layer_update_callback);
  layer_add_child(score_layer, trap_layer);

  disc_layer = layer_create(frame);
  layer_set_update_proc(disc_layer, disc_layer_update_callback);
  layer_add_child(trap_layer, disc_layer);
	
  // Setup result text Layer -> result Layer that scrolls -> Window Layer
  result_layer = scroll_layer_create(frame);
  scroll_layer_set_click_config_onto_window(result_layer, window);
  //window_set_click_config_provider(window, (ClickConfigProvider) config_provider);  // NOTE: Using this disconnects the built-in scroll button handler in SDK 2 !
  scroll_layer_set_callbacks(result_layer, selectButtonCallback);	// But connecting select button this way works
	
  text_layer = text_layer_create(max_text_bounds);
  
  text_layer_set_font(text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_background_color(text_layer, GColorBlack);
  text_layer_set_text_color(text_layer, GColorWhite);
  scroll_layer_add_child(result_layer, text_layer_get_layer(text_layer));

  layer_add_child(window_layer, scroll_layer_get_layer(result_layer));
  layer_set_hidden(scroll_layer_get_layer(result_layer), true);
	
  srand(time(NULL));	// This ensures traps are placed randomly every time app starts
  //next_level(level);
  show_text("You have 60 seconds to trap as many balls as you can!!  Press Select to start!");  //Your best level was X.
}

// When the phone sends us data, save the settings in persistant storage
static void in_received_handler(DictionaryIterator *iter, void *context) {
	
  Tuple *player_id_tuple = dict_find(iter, TB_KEY_player_id);
  Tuple *player_name_tuple = dict_find(iter, TB_KEY_player_name);
  Tuple *summary_tuple = dict_find(iter, TB_KEY_summary);
	
  if (player_name_tuple) {
      snprintf( player_name, ARRAY_LENGTH(player_name), "%s", player_name_tuple->value->cstring);
	  if (player_name[0] == '\0' || player_name[0] == '0')
	  {
		  player_name[0] = '\0';
		  if (player_id_tuple)
			  snprintf( player_name, ARRAY_LENGTH(player_name), "Player %s", player_id_tuple->value->cstring);
	  }
  }
  if (summary_tuple)
  {
		snprintf(result_text, ARRAY_LENGTH(result_text), result_text_template, player_name, level, score, summary_tuple->value->cstring );
		show_text(result_text);
  }
}

static void in_dropped_handler(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Dropped!");
}

// If got error while sending
static void out_failed_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "App Message Failed to Send!");
}


// Register message handlers
static void app_message_init(void) {
	// Register callbacks
	app_message_register_inbox_received(in_received_handler);
	app_message_register_inbox_dropped(in_dropped_handler);
	app_message_register_outbox_failed(out_failed_handler);
	// Init buffers
	app_message_open(app_message_inbox_size_maximum(), 64);
}

static void window_unload(Window *window) {
    layer_destroy(disc_layer);
	layer_destroy(trap_layer);
	layer_destroy(score_layer);
	text_layer_destroy(text_layer);
	scroll_layer_destroy(result_layer);
	app_timer_cancel(timer);
	app_message_deregister_callbacks();
}

static void init(void) {
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  window_stack_push(window, true /* Animated */);
  window_set_background_color(window, GColorBlack);

  accel_data_service_subscribe(0, handle_accel);

  //timer = app_timer_register(100 /* milliseconds */, timer_callback, NULL);	// Select button from scroll layer is the only one to start the timer now
	
  	// Setup communication to/from phone
    app_message_init();
}

static void deinit(void) {
  accel_data_service_unsubscribe();

  window_destroy(window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
