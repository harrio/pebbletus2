#include <pebble.h>

//Globals
static Window *window;
static TextLayer *phaseLayer;
static TextLayer *timeLayer;
static TextLayer *roundLayer;
static TextLayer *presetLayer;
static ActionBarLayer *actionBarLayer;

static GBitmap *button_image_play;
static GBitmap *button_image_pause;
static GBitmap *button_image_reset;

//Set of enumerated keys with names and values. Identical to Android app keys
enum {
  DATA_KEY = 0x0,
  SELECT_KEY = 0x0, // TUPLE_INTEGER
  UP_KEY = 0x01,
  DOWN_KEY = 0x02,
  INIT_KEY = 0x03,
  PHASE_KEY = 0x01,
  ROUND_KEY = 0x02,
  CHANGE_KEY = 0x03,
  TIME_KEY = 0x04,
  PRESET_KEY = 0x05,
  CHANGE_STARTED = 0x0,
  CHANGE_PAUSED = 0x01,
  CHANGE_STOPPED = 0x02,
  STATUS = 0x03,
  WORK = 0x04,
  REST = 0x05,
  FINISHED = 0x06,
  INIT = 0x07
};

enum State {
  STOPPED,
  PAUSED,
  RUNNING
};

enum State current_state = STOPPED;
int total_seconds = 0;
int current_seconds = 0;
int last_set_time = -1;

static const uint32_t const workSegments[] = { 100, 100, 100, 100, 100, 100, 100, 100, 100 };
VibePattern workPat = {
  .durations = workSegments,
  .num_segments = ARRAY_LENGTH(workSegments)
};

static const uint32_t const restSegments[] = { 400, 200, 400 };
VibePattern restPat = {
  .durations = restSegments,
  .num_segments = ARRAY_LENGTH(restSegments)
};

static const uint32_t const finSegments[] = { 600, 300, 600, 300, 600 };
VibePattern finPat = {
  .durations = finSegments,
  .num_segments = ARRAY_LENGTH(finSegments)
};

void update_time() {
  if (current_seconds == last_set_time) {
    return;
  }

  static char time_text[] = "00:00";
  if (current_seconds >= 0) {
    struct tm time;
    time.tm_min  = current_seconds / 60;
    time.tm_sec  = current_seconds - time.tm_min * 60;
    strftime(time_text, sizeof(time_text), "%M:%S", &time);
  }
  text_layer_set_text(timeLayer, time_text);
  last_set_time = current_seconds;
}

void set_running() {
  current_state = RUNNING;
  action_bar_layer_set_icon(actionBarLayer, BUTTON_ID_UP, button_image_pause);
} 

void set_paused() {
  current_state = PAUSED;
  action_bar_layer_set_icon(actionBarLayer, BUTTON_ID_UP, button_image_play);
} 

void set_stopped() {
  current_state = STOPPED;
  action_bar_layer_set_icon(actionBarLayer, BUTTON_ID_UP, button_image_play);
  app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL);
} 

void change_received(int change) {
  if (change == WORK) { 
      vibes_enqueue_custom_pattern(workPat);
      light_enable_interaction();
      set_running();
  } else if (change == REST) {
    vibes_enqueue_custom_pattern(restPat);
    light_enable_interaction();
    set_running();
  } else if (change == FINISHED) {
    vibes_enqueue_custom_pattern(finPat);
    light_enable_interaction();
    set_stopped();
  } else if (change == STATUS) {
    update_time();
  } else if (change == CHANGE_STOPPED) {
    vibes_double_pulse();
    set_stopped();
  } else if (change == CHANGE_STARTED) {
    vibes_double_pulse();    
    set_running();
  } else if (change == CHANGE_PAUSED) {
    vibes_double_pulse();    
    set_paused();
  }
}

void handle_second_counting_down() {
  current_seconds--;

  update_time();

  if (current_seconds == 0) {
    current_state = STOPPED;
  }
}

void handle_second_waiting() {
  current_seconds = total_seconds;
  update_time();
}

static void handle_second_tick(struct tm *tick_time, TimeUnits units_changed) {
  switch(current_state) {
  case PAUSED:
  case STOPPED:
    handle_second_waiting();
    break;
  case RUNNING:
    handle_second_counting_down();
    break;
  default:
    break;
  }
}

/**
  * Handler for AppMessage sent
  */
void out_sent_handler(DictionaryIterator *sent, void *context) {
}

/**
  * Handler for AppMessage send failed
  */
static void out_fail_handler(DictionaryIterator* failed, AppMessageResult reason, void* context) {
  //Notify the watch app user that the send operation failed
  text_layer_set_text(phaseLayer, "Offline");
}

/**
  * Handler for received AppMessage
  */
static void in_received_handler(DictionaryIterator* iter, void* context) {
  
  Tuple *change_tuple = dict_find(iter, CHANGE_KEY);
  if (change_tuple) {
    int change = change_tuple->value->uint32;
    change_received(change); 
  }

  Tuple *preset_tuple = dict_find(iter, PRESET_KEY);
  if (preset_tuple) {
    static char preset[11];
    strcpy(preset, preset_tuple->value->cstring);
    text_layer_set_text(presetLayer, preset);
  }

  Tuple *round_tuple = dict_find(iter, ROUND_KEY);
  if (round_tuple) {
    static char round[11];
    strcpy(round, round_tuple->value->cstring);
    text_layer_set_text(roundLayer, round);
  }

  Tuple *phase_tuple = dict_find(iter, PHASE_KEY);
  if (phase_tuple) {
    static char phase[11];
    strcpy(phase, phase_tuple->value->cstring);
    text_layer_set_text(phaseLayer, phase);
  }
  
  Tuple *time_tuple = dict_find(iter, TIME_KEY);
  if (time_tuple) {
    total_seconds = time_tuple->value->uint32;
    current_seconds = total_seconds;
  }  
}

/**
  * Handler for received message dropped
  */
static void in_drop_handler(AppMessageResult reason, void *context) {
  //Notify the watch app user that the recieved message was dropped
  //text_layer_set_text(&phaseLayer, "Connection failed");
}

/**
  * Function to send a key press using a pre-agreed key
  */
static void send_cmd(uint8_t cmd) { //uint8_t is an unsigned 8-bit int (0 - 255)
  //Create a key-value pair
  Tuplet value = TupletInteger(DATA_KEY, cmd);
  
  //Construct the dictionary
  DictionaryIterator *iter;
  app_message_outbox_begin(&iter);
  
  //If not constructed, do not send - return straight away
  if (iter == NULL)
    return;
  
  //Write the tuplet to the dictionary
  dict_write_tuplet(iter, &value);
  dict_write_end(iter);
  
  //Send the dictionary and release the buffer
  app_message_outbox_send();
}

/**
  * Handler for up click
  */
void up_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
 
  //Send the UP_KEY
  send_cmd(UP_KEY);
  app_comm_set_sniff_interval(SNIFF_INTERVAL_REDUCED);
}

/**
  * Handler for down click
  */
void down_single_click_handler(ClickRecognizerRef recognizer, void *context) {
  (void)recognizer;
 
  //Send the DOWN_KEY
  send_cmd(DOWN_KEY);
  app_comm_set_sniff_interval(SNIFF_INTERVAL_NORMAL);
}

/**
  * Handler for select click
  */
void select_single_click_handler(ClickRecognizerRef recognizer, void *context) {  
  //Send the SELECT_KEY
  //send_cmd(SELECT_KEY);
}

/**
  * Click config function
  */
void click_config_provider(void *context) {
 
  window_single_click_subscribe(BUTTON_ID_SELECT, select_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP, up_single_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_single_click_handler);
}

static void window_load(Window *window) {

}

void handle_main_appear(Window *window)
{
    // We need to add the action_bar when the main-window appears. If we do this in handle_init it picks up wrong window-bounds and the size doesn't fit.
    action_bar_layer_add_to_window(actionBarLayer, window);
}

void handle_main_disappear(Window *window)
{
    // Since we add the layer on each appear, we remove it on each disappear.
    action_bar_layer_remove_from_window(actionBarLayer);
}

/**
  * Resource initialisation handle function
  */
void handle_init(void) {
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "init");
  app_message_open(64, 16);
  app_message_register_outbox_sent(out_sent_handler);
  app_message_register_outbox_failed(out_fail_handler);
  app_message_register_inbox_received(in_received_handler);
  app_message_register_inbox_dropped(in_drop_handler);
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "msg done");
  tick_timer_service_subscribe(SECOND_UNIT, handle_second_tick);
   app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "tick done");
  //Init window
  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
        .appear = (WindowHandler)handle_main_appear,
        .disappear = (WindowHandler)handle_main_disappear
  });
   app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "wnd hand done");
  
   app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "push done");
  window_set_background_color(window, GColorBlack);
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "wnd done");
  ///Init resources
  //  resource_init_current_app(APP_RESOURCES);

  button_image_play = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BUTTON_PLAY);
  button_image_pause = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BUTTON_PAUSE);  
  button_image_reset = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BUTTON_RESET);  
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "bitmap done");
  actionBarLayer = action_bar_layer_create();
  action_bar_layer_set_click_config_provider(actionBarLayer, click_config_provider);
  action_bar_layer_set_icon(actionBarLayer, BUTTON_ID_UP, button_image_play);
  //action_bar_layer_set_icon(&actionBarLayer, BUTTON_ID_SELECT, &button_image_setup.bmp);
  action_bar_layer_set_icon(actionBarLayer, BUTTON_ID_DOWN, button_image_reset);
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "bar done");
  //Init TextLayers
  phaseLayer = text_layer_create(GRect(0, 0, 144, 38));
  text_layer_set_background_color(phaseLayer, GColorWhite);
  text_layer_set_text_color(phaseLayer, GColorBlack);
  text_layer_set_text_alignment(phaseLayer, GTextAlignmentCenter);
  text_layer_set_font(phaseLayer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));

  roundLayer = text_layer_create(GRect(0, 38, 144, 38));
  text_layer_set_background_color(roundLayer, GColorWhite);
  text_layer_set_text_color(roundLayer, GColorBlack);
  text_layer_set_text_alignment(roundLayer, GTextAlignmentCenter);
  text_layer_set_font(roundLayer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));

  timeLayer = text_layer_create(GRect(0, 76, 144, 38));
  text_layer_set_background_color(timeLayer, GColorBlack);
  text_layer_set_text_color(timeLayer, GColorWhite);
  text_layer_set_text_alignment(timeLayer, GTextAlignmentCenter);
  text_layer_set_font(timeLayer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));

  presetLayer = text_layer_create(GRect(0, 114, 144, 38));
  text_layer_set_background_color(presetLayer, GColorWhite);
  text_layer_set_text_color(presetLayer, GColorBlack);
  text_layer_set_text_alignment(presetLayer, GTextAlignmentCenter);
  text_layer_set_font(presetLayer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "text done");
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(phaseLayer));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(roundLayer));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(timeLayer));
  layer_add_child(window_get_root_layer(window), text_layer_get_layer(presetLayer));
  text_layer_set_text(phaseLayer, "Ready");
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "child done");
  //Setup button click handlers
  window_set_click_config_provider(window, click_config_provider);
  window_stack_push(window, true);
  send_cmd(INIT_KEY);
  app_log(APP_LOG_LEVEL_INFO, "main", 294, "%s", "init ready");
}

void handle_deinit(void)
{
    tick_timer_service_unsubscribe();

    action_bar_layer_destroy(actionBarLayer);
    text_layer_destroy(phaseLayer);
    text_layer_destroy(roundLayer);
    text_layer_destroy(timeLayer);
    text_layer_destroy(presetLayer);
    gbitmap_destroy(button_image_play);
    gbitmap_destroy(button_image_pause);
    gbitmap_destroy(button_image_reset);

    window_destroy(window);
}

/**
  * Main Pebble loop
  */
int main(void) {
  handle_init();
  app_event_loop();
  handle_deinit();
}
