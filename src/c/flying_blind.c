/**
 * BURR — a Pebble watch face that tells the time by vibration.
 *
 * The screen stays blank; flick your wrist to briefly reveal the time.
 * Inspired by the DURR watch (a buzz every few minutes to keep you oriented
 * in time without looking).
 *
 * Original watchface by Matthew Congrove, 2013 (http://github.com/mcongrove).
 * DURR-style 5-minute vibration and the 2026 SDK modernization/revitalization
 * by PK.
 *
 * Provided under the Apache 2.0 license — see LICENSE.md.
 */
#include <pebble.h>

// ---------------------------------------------------------------------------
// Settings — defaults here; overridden by persisted values and Clay config.
// (Stored under the stable PERSIST_* keys defined further down.)
// ---------------------------------------------------------------------------
static char s_theme[8]          = "dark";    // "dark" | "light"
static char s_interval_style[8] = "normal";  // "subtle" | "normal" | "strong"
static int  s_vibrate           = 0;         // 0 hourly only / 1 ½h / 2 ¼h / 3 5m
static int  s_reveal_secs       = 3;         // shake-to-reveal duration (2..10)
static int  s_accent_hex        = 0xFFFFFF;  // time colour on colour watches
static bool s_hourly_enabled    = true;      // long buzz on the hour
static bool s_quiet_enabled     = false;     // suppress all buzzing overnight
static int  s_quiet_start       = 22;        // quiet-hours start (0..23)
static int  s_quiet_end         = 7;         // quiet-hours end   (0..23)
static bool s_sys_quiet_time    = true;      // respect the watch's system Quiet Time
static bool s_bt_alert          = false;     // buzz on Bluetooth disconnect
static bool s_battery_show      = false;     // show battery % on reveal
static bool s_steps_show        = false;     // show today's step count on reveal
static bool s_heart_show        = false;     // show last heart rate (BPM) on reveal

// ---------------------------------------------------------------------------
// UI state
// ---------------------------------------------------------------------------
static Window    *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_status_layer;
static AppTimer  *s_reveal_timer;
static char       s_time_text[8]    = "00:00";
static char       s_status_text[32] = "";

// ---------------------------------------------------------------------------
// Theme helpers
// ---------------------------------------------------------------------------
static bool is_light_theme(void) {
	return strcmp(s_theme, "light") == 0;
}

static GColor theme_bg(void) {
	return is_light_theme() ? GColorWhite : GColorBlack;
}

static GColor contrast_fg(void) {
	return is_light_theme() ? GColorBlack : GColorWhite;
}

// The revealed time uses the accent colour on colour watches; on black/white
// watches it always uses the theme's contrast colour so it stays readable.
static GColor time_fg(void) {
#if defined(PBL_COLOR)
	GColor accent = GColorFromHEX(s_accent_hex);
	// Fall back to the contrast colour when the accent matches the background
	// (e.g. the default white accent on the white light theme) so the time is
	// never rendered invisibly.
	return gcolor_equal(accent, theme_bg()) ? contrast_fg() : accent;
#else
	return is_light_theme() ? GColorBlack : GColorWhite;
#endif
}

static void apply_theme(void) {
	if (!s_window) {
		return;
	}
	window_set_background_color(s_window, theme_bg());
	if (s_time_layer) {
		text_layer_set_text_color(s_time_layer, time_fg());
	}
	if (s_status_layer) {
		text_layer_set_text_color(s_status_layer, contrast_fg());
	}
}

// ---------------------------------------------------------------------------
// Quiet hours — true when `hour` falls inside the configured window, handling
// the case where the window wraps past midnight (e.g. 22:00 -> 07:00).
// ---------------------------------------------------------------------------
static bool in_quiet_hours(int hour) {
	if (!s_quiet_enabled || s_quiet_start == s_quiet_end) {
		return false;
	}
	if (s_quiet_start < s_quiet_end) {
		return hour >= s_quiet_start && hour < s_quiet_end;
	}
	return hour >= s_quiet_start || hour < s_quiet_end;
}

// True when buzzing should be suppressed for the given local `hour`: either the
// watch's system Quiet Time is active and the user chose to respect it, or the
// hour falls inside BURR's own quiet-hours window. The shake-to-reveal display
// is intentionally unaffected — only vibration is silenced, which is what Quiet
// Time is meant to do. (On aplite quiet_time_is_active() is a stub for `false`.)
static bool buzzing_silenced(int hour) {
	if (s_sys_quiet_time && quiet_time_is_active()) {
		return true;
	}
	return in_quiet_hours(hour);
}

// ---------------------------------------------------------------------------
// Time display
// ---------------------------------------------------------------------------
static void update_time(struct tm *t) {
	strftime(s_time_text, sizeof(s_time_text),
	         clock_is_24h_style() ? "%H:%M" : "%I:%M", t);
	if (s_time_layer) {
		text_layer_set_text(s_time_layer, s_time_text);
	}
}

static void hide_reveal(void *data) {
	s_reveal_timer = NULL;
	layer_set_hidden(text_layer_get_layer(s_time_layer), true);
	layer_set_hidden(text_layer_get_layer(s_status_layer), true);
}

// Separator between status metrics: " · " (a middot, UTF-8 0xC2 0xB7, which the
// Pebble system fonts render). Written as explicit bytes so it doesn't depend on
// the compiler's execution charset.
#define STATUS_SEP " \xc2\xb7 "

// Reveal the time for s_reveal_secs. `status` is an optional override line
// (e.g. "BT lost"); when NULL the enabled metrics — today's steps, last heart
// rate, and battery, in that order — are composed onto the single status line,
// joined by STATUS_SEP. Only metrics the user enabled (and that the watch can
// actually report) are shown; if none apply the status line stays hidden.
static void reveal(const char *status) {
	if (status) {
		snprintf(s_status_text, sizeof(s_status_text), "%s", status);
	} else {
		s_status_text[0] = '\0';
		const size_t cap = sizeof(s_status_text);
		size_t len = 0;

#if defined(PBL_HEALTH)
		// Steps are reported on every health-capable platform; the value is the
		// total since the start of the local day (0 if unavailable).
		if (s_steps_show && len < cap) {
			int steps = (int)health_service_sum_today(HealthMetricStepCount);
			int n = snprintf(s_status_text + len, cap - len, "%s%d", len ? STATUS_SEP : "", steps);
			if (n > 0) { len += n; }
		}
		// Heart rate only appears on watches with an HRM sensor that have a recent
		// reading: gate on both the accessibility mask and a positive value so
		// non-HRM models (and stale/empty readings) show nothing rather than 0.
		if (s_heart_show && len < cap) {
			HealthServiceAccessibilityMask acc =
				health_service_metric_accessible(HealthMetricHeartRateBPM, time(NULL), time(NULL));
			int bpm = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
			if ((acc & HealthServiceAccessibilityMaskAvailable) && bpm > 0) {
				int n = snprintf(s_status_text + len, cap - len, "%s%d", len ? STATUS_SEP : "", bpm);
				if (n > 0) { len += n; }
			}
		}
#endif

		if (s_battery_show && len < cap) {
			BatteryChargeState batt = battery_state_service_peek();
			int n = snprintf(s_status_text + len, cap - len, "%s%d%%", len ? STATUS_SEP : "", batt.charge_percent);
			if (n > 0) { len += n; }
		}
	}

	layer_set_hidden(text_layer_get_layer(s_status_layer), s_status_text[0] == '\0');
	text_layer_set_text(s_status_layer, s_status_text);
	layer_set_hidden(text_layer_get_layer(s_time_layer), false);

	if (s_reveal_timer) {
		app_timer_cancel(s_reveal_timer);
	}
	s_reveal_timer = app_timer_register(s_reveal_secs * 1000, hide_reveal, NULL);
}

// ---------------------------------------------------------------------------
// Vibration — the hourly chime is a single long pulse; between-hour buzzes use
// a distinct pattern (whose intensity is user-selectable) so the two are
// distinguishable by feel.
// ---------------------------------------------------------------------------
static void interval_vibe(void) {
	if (strcmp(s_interval_style, "subtle") == 0) {
		static const uint32_t segs[] = { 60 };
		vibes_enqueue_custom_pattern((VibePattern){ .durations = segs, .num_segments = ARRAY_LENGTH(segs) });
	} else if (strcmp(s_interval_style, "strong") == 0) {
		static const uint32_t segs[] = { 250, 120, 250 };
		vibes_enqueue_custom_pattern((VibePattern){ .durations = segs, .num_segments = ARRAY_LENGTH(segs) });
	} else { // "normal"
		static const uint32_t segs[] = { 100, 100, 100 };
		vibes_enqueue_custom_pattern((VibePattern){ .durations = segs, .num_segments = ARRAY_LENGTH(segs) });
	}
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------
static void handle_minute_tick(struct tm *t, TimeUnits units_changed) {
	update_time(t);

	if (buzzing_silenced(t->tm_hour)) {
		return;
	}

	if (t->tm_min == 0) {
		if (s_hourly_enabled) {
			vibes_long_pulse();
		}
		return;
	}

	bool buzz = (s_vibrate == 1 && t->tm_min == 30)
	         || (s_vibrate == 2 && t->tm_min % 15 == 0)
	         || (s_vibrate == 3 && t->tm_min % 5 == 0);
	if (buzz) {
		interval_vibe();
	}
}

static void handle_tap(AccelAxisType axis, int32_t direction) {
	reveal(NULL);
}

static void handle_connection(bool connected) {
	if (connected || !s_bt_alert) {
		return;
	}
	time_t now = time(NULL);
	if (buzzing_silenced(localtime(&now)->tm_hour)) {
		return;
	}
	vibes_double_pulse();
	reveal("BT lost");
}

// ---------------------------------------------------------------------------
// Settings persistence and inbound config messages
// ---------------------------------------------------------------------------
// Stable persistent-storage keys — deliberately decoupled from the build-time
// MESSAGE_KEY_* integers (which can shift if the messageKeys list is reordered
// or added to) so that saved settings always read back correctly across
// updates.
enum {
	PERSIST_THEME = 1,
	PERSIST_INTERVAL_STYLE,
	PERSIST_VIBRATE,
	PERSIST_REVEAL_SECS,
	PERSIST_ACCENT_COLOR,
	PERSIST_HOURLY_ENABLED,
	PERSIST_QUIET_ENABLED,
	PERSIST_QUIET_START,
	PERSIST_QUIET_END,
	PERSIST_BT_ALERT,
	PERSIST_BATTERY_SHOW,
	PERSIST_STEPS_SHOW,
	PERSIST_HEART_SHOW,
	PERSIST_SYS_QUIET_TIME,
};

static void load_settings(void) {
	if (persist_exists(PERSIST_THEME)) {
		persist_read_string(PERSIST_THEME, s_theme, sizeof(s_theme));
	}
	if (persist_exists(PERSIST_INTERVAL_STYLE)) {
		persist_read_string(PERSIST_INTERVAL_STYLE, s_interval_style, sizeof(s_interval_style));
	}
	if (persist_exists(PERSIST_VIBRATE))        { s_vibrate        = persist_read_int(PERSIST_VIBRATE); }
	if (persist_exists(PERSIST_REVEAL_SECS))    { s_reveal_secs    = persist_read_int(PERSIST_REVEAL_SECS); }
	if (persist_exists(PERSIST_ACCENT_COLOR))   { s_accent_hex     = persist_read_int(PERSIST_ACCENT_COLOR); }
	if (persist_exists(PERSIST_HOURLY_ENABLED)) { s_hourly_enabled = persist_read_bool(PERSIST_HOURLY_ENABLED); }
	if (persist_exists(PERSIST_QUIET_ENABLED))  { s_quiet_enabled  = persist_read_bool(PERSIST_QUIET_ENABLED); }
	if (persist_exists(PERSIST_QUIET_START))    { s_quiet_start    = persist_read_int(PERSIST_QUIET_START); }
	if (persist_exists(PERSIST_QUIET_END))      { s_quiet_end      = persist_read_int(PERSIST_QUIET_END); }
	if (persist_exists(PERSIST_BT_ALERT))       { s_bt_alert       = persist_read_bool(PERSIST_BT_ALERT); }
	if (persist_exists(PERSIST_BATTERY_SHOW))   { s_battery_show   = persist_read_bool(PERSIST_BATTERY_SHOW); }
	if (persist_exists(PERSIST_STEPS_SHOW))     { s_steps_show     = persist_read_bool(PERSIST_STEPS_SHOW); }
	if (persist_exists(PERSIST_HEART_SHOW))     { s_heart_show     = persist_read_bool(PERSIST_HEART_SHOW); }
	if (persist_exists(PERSIST_SYS_QUIET_TIME)) { s_sys_quiet_time = persist_read_bool(PERSIST_SYS_QUIET_TIME); }

	// Clamp anything that comes from sliders / selects / external input so that
	// stale or out-of-range persisted data can't cause odd behaviour.
	if (s_vibrate < 0)      { s_vibrate = 0; }
	if (s_vibrate > 3)      { s_vibrate = 3; }
	if (s_reveal_secs < 2)  { s_reveal_secs = 2; }
	if (s_reveal_secs > 10) { s_reveal_secs = 10; }
	if (s_quiet_start < 0 || s_quiet_start > 23) { s_quiet_start = 22; }
	if (s_quiet_end < 0 || s_quiet_end > 23)     { s_quiet_end = 7; }
}

static void inbox_received(DictionaryIterator *iter, void *context) {
	Tuple *t;
	// Values arrive under the wire keys (MESSAGE_KEY_*) and are stored under our
	// stable PERSIST_* keys. Clay sends select values as strings and
	// toggles/sliders/colours as integers (the colour is an int even though the
	// config default is written as a hex string).
	if ((t = dict_find(iter, MESSAGE_KEY_theme)))          { persist_write_string(PERSIST_THEME, t->value->cstring); }
	if ((t = dict_find(iter, MESSAGE_KEY_interval_style))) { persist_write_string(PERSIST_INTERVAL_STYLE, t->value->cstring); }
	if ((t = dict_find(iter, MESSAGE_KEY_vibrate)))        { persist_write_int(PERSIST_VIBRATE, atoi(t->value->cstring)); }
	if ((t = dict_find(iter, MESSAGE_KEY_reveal_secs)))    { persist_write_int(PERSIST_REVEAL_SECS, t->value->int32); }
	if ((t = dict_find(iter, MESSAGE_KEY_accent_color)))   { persist_write_int(PERSIST_ACCENT_COLOR, t->value->int32); }
	if ((t = dict_find(iter, MESSAGE_KEY_hourly_enabled))) { persist_write_bool(PERSIST_HOURLY_ENABLED, t->value->int32 != 0); }
	if ((t = dict_find(iter, MESSAGE_KEY_quiet_enabled)))  { persist_write_bool(PERSIST_QUIET_ENABLED, t->value->int32 != 0); }
	if ((t = dict_find(iter, MESSAGE_KEY_quiet_start)))    { persist_write_int(PERSIST_QUIET_START, t->value->int32); }
	if ((t = dict_find(iter, MESSAGE_KEY_quiet_end)))      { persist_write_int(PERSIST_QUIET_END, t->value->int32); }
	if ((t = dict_find(iter, MESSAGE_KEY_bt_alert)))       { persist_write_bool(PERSIST_BT_ALERT, t->value->int32 != 0); }
	if ((t = dict_find(iter, MESSAGE_KEY_battery_show)))   { persist_write_bool(PERSIST_BATTERY_SHOW, t->value->int32 != 0); }
	if ((t = dict_find(iter, MESSAGE_KEY_steps_show)))     { persist_write_bool(PERSIST_STEPS_SHOW, t->value->int32 != 0); }
	if ((t = dict_find(iter, MESSAGE_KEY_heart_show)))     { persist_write_bool(PERSIST_HEART_SHOW, t->value->int32 != 0); }
	if ((t = dict_find(iter, MESSAGE_KEY_respect_quiet_time))) { persist_write_bool(PERSIST_SYS_QUIET_TIME, t->value->int32 != 0); }

	load_settings();
	apply_theme();
}

// ---------------------------------------------------------------------------
// Window lifecycle — responsive layout derived from the window bounds so the
// face is correctly centred on rectangular and round displays alike.
// ---------------------------------------------------------------------------
static void window_load(Window *window) {
	Layer *root = window_get_root_layer(window);
	GRect b = layer_get_bounds(root);

	const int time_h = 46;
	const int status_h = 22;
	const int gap = 2;
	int top = b.origin.y + (b.size.h - (time_h + gap + status_h)) / 2;

	s_time_layer = text_layer_create(GRect(b.origin.x, top, b.size.w, time_h));
	text_layer_set_background_color(s_time_layer, GColorClear);
	text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
	text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
	text_layer_set_text(s_time_layer, s_time_text);
	layer_set_hidden(text_layer_get_layer(s_time_layer), true);
	layer_add_child(root, text_layer_get_layer(s_time_layer));

	s_status_layer = text_layer_create(GRect(b.origin.x, top + time_h + gap, b.size.w, status_h));
	text_layer_set_background_color(s_status_layer, GColorClear);
	text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
	text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
	layer_set_hidden(text_layer_get_layer(s_status_layer), true);
	layer_add_child(root, text_layer_get_layer(s_status_layer));

	apply_theme();
}

static void window_unload(Window *window) {
	text_layer_destroy(s_time_layer);
	text_layer_destroy(s_status_layer);
}

// ---------------------------------------------------------------------------
// App lifecycle
// ---------------------------------------------------------------------------
static void init(void) {
	load_settings();

	s_window = window_create();
	window_set_window_handlers(s_window, (WindowHandlers){
		.load = window_load,
		.unload = window_unload,
	});
	window_stack_push(s_window, true);

	// Seed the display so the first reveal shows the correct time before the
	// first minute tick arrives.
	time_t now = time(NULL);
	update_time(localtime(&now));

	app_message_register_inbox_received(inbox_received);
	app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());

	tick_timer_service_subscribe(MINUTE_UNIT, handle_minute_tick);
	accel_tap_service_subscribe(handle_tap);
	connection_service_subscribe((ConnectionHandlers){
		.pebble_app_connection_handler = handle_connection,
	});
}

static void deinit(void) {
	connection_service_unsubscribe();
	accel_tap_service_unsubscribe();
	tick_timer_service_unsubscribe();
	app_message_deregister_callbacks();
	window_destroy(s_window);
}

int main(void) {
	init();
	app_event_loop();
	deinit();
}
