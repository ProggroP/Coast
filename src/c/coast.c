// Coast -- ein Watchface, das so selten zeichnet wie moeglich.
//
// Grundgedanke: die Zeit wird auf ein Raster von 5 bzw. 15 Minuten abgerundet.
// Zwischen zwei Rasterschritten aendert sich das Bild nicht, also wird auch
// nichts gezeichnet. Das ist der eigentliche Sparhebel. Die Aufteilung auf
// mehrere Layer spart dagegen nichts: gemessen ruft das System bei einem
// einzigen dirty-Layer die update_procs aller Layer des Fensters auf.
//
// Zwei Darstellungen, per Einstellung waehlbar:
// - Digits: Stunde ueber Minute in der importierten LECO-1976-Schrift. Die
//   groesste Systemschrift (Leco 60) ist real nur ~45 px hoch, der Import
//   erlaubt gut das Anderthalbfache und kostet im Betrieb nichts.
// - Hands: zwoelf Striche und zwei Zeiger. Der Minutenzeiger springt im
//   Raster und steht damit immer genau auf einem Strich. Ein kompletter
//   Bildaufbau besteht aus rund 17 gefuellten Flaechen ohne Antialiasing --
//   nicht teurer als die vier Ziffern.
//
// Absichtlich nicht vorhanden: Sekundentick, AppMessage im Betrieb, Sensor-
// Abos, Animationen, Bitmaps, Hintergrund-Worker, light_enable().

#include <pebble.h>

// --------------------------------------------------------------- Diagnose

// Zaehlt die Aufrufe der update_procs und schreibt sie ins Log. Fuer den
// Release auskommentieren.
// #define COAST_DRAW_STATS

// Nur fuer Store-Screenshots: im Emulator entlaedt sich nichts, die Rate
// wuerde also nie gelernt. Vor dem Release wieder auskommentieren.
// #define COAST_DEMO_RATE  6000

// Nur fuer Store-Screenshots: haelt die Anzeige auf 10:10, damit beide Zeiger
// oben stehen und die Laufzeitzeile an ihrem Normalplatz unten erscheint.
// pebble emu-set-time greift in dieser Umgebung nicht. Vor dem Release wieder
// auskommentieren.
// #define COAST_DEMO_TIME

// --------------------------------------------------------------- Farben

#define COLOR_BG_DEFAULT   GColorBlack
#define COLOR_FG_DEFAULT   GColorWhite

// --------------------------------------------------------------- Layout Digits

// Die Ziffernhoehe ist auf den runden Displays nicht durch die Displayhoehe
// begrenzt, sondern durch die Sehnenbreite auf Hoehe der oberen Zeile.
//
// Die alten Plattformen lassen nur 256 Byte je Glyph zu statt 512, dort passt
// die LECO deshalb nur in Punktgroesse 76 (rund 53 px hohe Ziffern) statt 104.
#if defined(PBL_PLATFORM_EMERY)
  #define DIGIT_H     80
  #define HOUR_Y      18
  #define MIN_Y      106
  #define BATT_Y     190
  #define BATT_H      24
  #define TEXT_LIFT   22
#elif defined(PBL_PLATFORM_GABBRO)
  #define DIGIT_H     80
  #define HOUR_Y      26
  #define MIN_Y      116
  #define BATT_Y     198
  #define BATT_H      24
  #define TEXT_LIFT   22
#elif defined(PBL_PLATFORM_APLITE)         // 144 x 168, kleinerer Glyph-Cache
  #define DIGIT_H     52
  #define HOUR_Y      12
  #define MIN_Y       66
  #define BATT_Y     126
  #define BATT_H      22
  #define TEXT_LIFT   14
#elif defined(PBL_PLATFORM_CHALK)          // 180 x 180, rund
  #define DIGIT_H     58
  #define HOUR_Y      22
  #define MIN_Y       82
  #define BATT_Y     140
  #define BATT_H      22
  #define TEXT_LIFT   16
#else                                      // aplite, basalt, diorite, flint: 144 x 168
  #define DIGIT_H     58
  #define HOUR_Y       8
  #define MIN_Y       68
  #define BATT_Y     130
  #define BATT_H      22
  #define TEXT_LIFT   16
#endif

// Aplite laesst zur Laufzeit weniger durch, als fontgen beim Bauen erlaubt:
// bei Punktgroesse 76 ist die schmale Eins mit 212 Byte noch zu sehen, alle
// breiteren Ziffern mit 245 Byte bleiben leer -- ohne Fehler, ohne Log. Dort
// muss die Schrift also kleiner sein als das Bau-Limit von 256 Byte nahelegt.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define RES_DIGIT_FONT  RESOURCE_ID_FONT_LECO_104
#elif defined(PBL_PLATFORM_APLITE)
  #define RES_DIGIT_FONT  RESOURCE_ID_FONT_LECO_66
#else
  #define RES_DIGIT_FONT  RESOURCE_ID_FONT_LECO_76
#endif

// Auf den kleinen Displays reicht Gothic 18 bei zweistelliger Restlaufzeit in
// die Striche bei 4 und 8 Uhr hinein -- dort eine Nummer kleiner.
#if defined(PBL_PLATFORM_EMERY) || defined(PBL_PLATFORM_GABBRO)
  #define FONT_BATT    FONT_KEY_GOTHIC_18_BOLD
#else
  #define FONT_BATT    FONT_KEY_GOTHIC_14_BOLD
#endif

// --------------------------------------------------------------- Layout Hands

// Alle Radien vom Displaymittelpunkt aus. Der Minutenzeiger endet knapp vor
// den Strichen -- reicht er hinein, verschmilzt er optisch mit ihnen. Auf den
// eckigen Displays liegen die Striche trotzdem auf einem Kreis, der in die
// Breite passt; das haelt alle Plattformen im selben Bild.
#if defined(PBL_PLATFORM_EMERY)
  #define TICK_R_OUT       96
  #define TICK_R_IN        86
  #define TICK_R_IN_MAJOR  78
  #define MIN_HAND_LEN     70
  #define HOUR_HAND_LEN    52
  #define HAND_TAIL         8
  #define HUB_R             8
  #define BATT_DIST        56
  #define TICK_W            6
  #define TICK_W_MAJOR     10
  #define MIN_HAND_W        8
  #define HOUR_HAND_W      12
#elif defined(PBL_PLATFORM_GABBRO)
  #define TICK_R_OUT      122
  #define TICK_R_IN       110
  #define TICK_R_IN_MAJOR 100
  #define MIN_HAND_LEN     92
  #define HOUR_HAND_LEN    66
  #define HAND_TAIL        10
  #define HUB_R             9
  #define BATT_DIST        64
  #define TICK_W            6
  #define TICK_W_MAJOR     10
  #define MIN_HAND_W        8
  #define HOUR_HAND_W      12
#elif defined(PBL_PLATFORM_CHALK)
  #define TICK_R_OUT       84
  #define TICK_R_IN        75
  #define TICK_R_IN_MAJOR  68
  #define MIN_HAND_LEN     62
  #define HOUR_HAND_LEN    44
  #define HAND_TAIL         7
  #define HUB_R             7
  #define BATT_DIST        44
  #define TICK_W            5
  #define TICK_W_MAJOR      8
  #define MIN_HAND_W        7
  #define HOUR_HAND_W      10
#else                                      // 144 x 168
  #define TICK_R_OUT       68
  #define TICK_R_IN        60
  #define TICK_R_IN_MAJOR  54
  #define MIN_HAND_LEN     50
  #define HOUR_HAND_LEN    35
  #define HAND_TAIL         6
  #define HUB_R             6
  #define BATT_DIST        38
  #define TICK_W            4
  #define TICK_W_MAJOR      7
  #define MIN_HAND_W        6
  #define HOUR_HAND_W       9
#endif

#define HUB_HOLE_R        3
#define BATT_BOX_H       24
// Gothic 18 bringt oberhalb der Glyphen Luft mit; so sitzt die Zeile optisch
// mittig auf BATT_DIST.
#define BATT_TEXT_RAISE  12

// --------------------------------------------------------------- Einstellungen

#define INTERVAL_MIN_DEFAULT   5

#define STYLE_DIGITS   0
#define STYLE_HANDS    1

#define ROUND_DOWN     0
#define ROUND_UP       1

#define NIGHT_START_DEFAULT  22
#define DAY_START_DEFAULT     7

#define PKEY_INTERVAL      100
#define PKEY_COLOR_BG      101
#define PKEY_COLOR_FG      102
#define PKEY_SHOW_BATTERY  103
#define PKEY_STYLE         104
#define PKEY_ROUND         105
#define PKEY_NIGHT_MODE    106
#define PKEY_NIGHT_START   107
#define PKEY_DAY_START     108
#define PKEY_SEC_PER_PCT   110
#define PKEY_LAST_PCT      111
#define PKEY_LAST_TIME     112

// Glaettung der Entladerate: ein neuer Messwert geht mit 1/4 Gewicht ein.
#define RATE_SMOOTHING      4
// Eine Messung zaehlt erst, wenn beide Schwellen erreicht sind. Gegen
// Messrauschen wirkt vor allem die Zeitschwelle: ein Abschnitt von mindestens
// einer Stunde ist verlaesslich, unabhaengig davon, ueber wie viele
// Prozentpunkte er geht. Die Time 2 meldet in Einer-Schritten und braucht bei
// rund 2,4 h je Prozentpunkt sonst einen halben Tag bis zur ersten Prognose.
// Aeltere Pebbles melden in Zehnerschritten und erfuellen beide Schwellen mit
// jedem einzelnen Sprung.
#define RATE_MIN_SECONDS  3600
#define RATE_MIN_DROP        2

static int      s_interval_min = INTERVAL_MIN_DEFAULT;
static int      s_style        = STYLE_DIGITS;
static int      s_round        = ROUND_DOWN;
static bool     s_night_mode   = false;
static int      s_night_start  = NIGHT_START_DEFAULT;
static int      s_day_start    = DAY_START_DEFAULT;
static bool     s_is_night     = false;
static GColor   s_color_bg;
static GColor   s_color_fg;
static bool     s_show_battery = true;

// --------------------------------------------------------------- Zustand

static Window *s_window;
static Layer  *s_hour_layer;
static Layer  *s_min_layer;
static Layer  *s_batt_layer;
static Layer  *s_dial_layer;

static char s_hour_text[4]  = "";
static char s_min_text[4]   = "";
static char s_batt_text[20] = "";
static GFont s_digit_font;

// Zeigerstellung: Stunde 0..11 und gerundete Minute
static int s_hand_hour = -1;
static int s_hand_min  = -1;

// Batterie-Hochrechnung
static int     s_charge_pct     = 0;
static int     s_sec_per_pct    = 0;   // geglaettete Sekunden je Prozentpunkt
static int     s_last_pct       = -1;
static time_t  s_last_pct_time  = 0;

#ifdef COAST_DRAW_STATS
static int s_draws = 0;
#endif

// --------------------------------------------------------------- Farbwahl

// Im Nachtmodus werden die beiden eingestellten Farben schlicht getauscht.
// Alles Gezeichnete geht ueber diese beiden Funktionen, damit es genau eine
// Stelle gibt, an der der Tausch passiert.
static GColor cur_bg(void) { return s_is_night ? s_color_fg : s_color_bg; }
static GColor cur_fg(void) { return s_is_night ? s_color_bg : s_color_fg; }

// Die Nachtspanne darf ueber Mitternacht laufen. Gleiche Start- und Endstunde
// heisst: kein Nachtmodus, sonst waere die Spanne 24 Stunden lang.
static bool night_at_hour(int hour) {
  if (!s_night_mode || s_night_start == s_day_start) return false;
  if (s_night_start < s_day_start) {
    return hour >= s_night_start && hour < s_day_start;
  }
  return hour >= s_night_start || hour < s_day_start;
}

// --------------------------------------------------------------- Zeichnen Digits

static void fill_background(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, cur_bg());
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);
}

static void draw_digits_row(Layer *layer, GContext *ctx, const char *text) {
  GRect bounds = layer_get_bounds(layer);
  fill_background(layer, ctx);

  graphics_context_set_text_color(ctx, cur_fg());
  GRect box = GRect(bounds.origin.x, bounds.origin.y - TEXT_LIFT,
                    bounds.size.w, bounds.size.h + TEXT_LIFT);
  graphics_draw_text(ctx, text, s_digit_font, box,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
}

static void hour_update_proc(Layer *layer, GContext *ctx) {
#ifdef COAST_DRAW_STATS
  s_draws++;
#endif
  draw_digits_row(layer, ctx, s_hour_text);
}

static void min_update_proc(Layer *layer, GContext *ctx) {
  draw_digits_row(layer, ctx, s_min_text);
}

static void batt_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  fill_background(layer, ctx);
  if (!s_show_battery) return;

  graphics_context_set_text_color(ctx, cur_fg());
  graphics_draw_text(ctx, s_batt_text, fonts_get_system_font(FONT_BATT), bounds,
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                     NULL);
}

// --------------------------------------------------------------- Zeichnen Hands

// Punkt im Abstand r vom Mittelpunkt entlang des Winkels, seitlich um k
// versetzt. Pebble-Winkel: 0 = 12 Uhr, im Uhrzeigersinn.
static GPoint ray_point(GPoint c, int32_t angle, int r, int k) {
  int32_t s = sin_lookup(angle);
  int32_t co = cos_lookup(angle);
  return GPoint(c.x + (r * s + k * co) / TRIG_MAX_RATIO,
                c.y + (-r * co + k * s) / TRIG_MAX_RATIO);
}

// Gefuelltes Rechteck entlang eines Strahls von r_from bis r_to. Die Eckpunkte
// werden direkt berechnet -- gpath_rotate_to dreht nicht wie erwartet und
// laeuft ueber 360 Grad hinaus ueber.
static void fill_bar(GContext *ctx, GPoint c, int32_t angle,
                     int r_from, int r_to, int width) {
  int hw = width / 2;
  GPoint pts[4] = {
    ray_point(c, angle, r_from,  hw),
    ray_point(c, angle, r_to,    hw),
    ray_point(c, angle, r_to,   -hw),
    ray_point(c, angle, r_from, -hw),
  };
  GPath path = { .num_points = 4, .points = pts, .rotation = 0, .offset = GPointZero };
  gpath_draw_filled(ctx, &path);
}

static int32_t angle_gap(int32_t a, int32_t b) {
  int32_t d = (a - b) % TRIG_MAX_ANGLE;
  if (d < 0) d += TRIG_MAX_ANGLE;
  if (d > TRIG_MAX_ANGLE / 2) d = TRIG_MAX_ANGLE - d;
  return d;
}

static void dial_update_proc(Layer *layer, GContext *ctx) {
#ifdef COAST_DRAW_STATS
  s_draws++;
#endif
  GRect bounds = layer_get_bounds(layer);
  GPoint c = grect_center_point(&bounds);

  graphics_context_set_antialiased(ctx, false);
  fill_background(layer, ctx);
  graphics_context_set_fill_color(ctx, cur_fg());

  for (int i = 0; i < 12; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / 12;
    bool major = (i % 3 == 0);
    fill_bar(ctx, c, a, major ? TICK_R_IN_MAJOR : TICK_R_IN, TICK_R_OUT,
             major ? TICK_W_MAJOR : TICK_W);
  }

  if (s_hand_min < 0) return;

  int32_t min_angle  = TRIG_MAX_ANGLE * s_hand_min / 60;
  int32_t hour_angle = TRIG_MAX_ANGLE * (s_hand_hour * 60 + s_hand_min) / 720;

  // Die Laufzeitzeile steht unten, ausser die Zeiger liegen dort naeher als
  // oben -- dann wechselt sie nach oben. Ganz frei bekommt man sie nicht zu
  // jeder Uhrzeit, aber sie weicht dem Schlimmsten aus.
  if (s_show_battery) {
    int32_t bottom = TRIG_MAX_ANGLE / 2;
    int32_t gap_bottom = angle_gap(min_angle, bottom);
    int32_t gap = angle_gap(hour_angle, bottom);
    if (gap < gap_bottom) gap_bottom = gap;
    int32_t gap_top = angle_gap(min_angle, 0);
    gap = angle_gap(hour_angle, 0);
    if (gap < gap_top) gap_top = gap;

    int y = (gap_bottom >= gap_top) ? c.y + BATT_DIST : c.y - BATT_DIST;
    GRect box = GRect(bounds.origin.x, y - BATT_TEXT_RAISE,
                      bounds.size.w, BATT_BOX_H);
    graphics_context_set_text_color(ctx, cur_fg());
    graphics_draw_text(ctx, s_batt_text, fonts_get_system_font(FONT_BATT), box,
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter,
                       NULL);
  }

  fill_bar(ctx, c, hour_angle, -HAND_TAIL, HOUR_HAND_LEN, HOUR_HAND_W);
  fill_bar(ctx, c, min_angle, -HAND_TAIL, MIN_HAND_LEN, MIN_HAND_W);

  graphics_fill_circle(ctx, c, HUB_R);
  graphics_context_set_fill_color(ctx, cur_bg());
  graphics_fill_circle(ctx, c, HUB_HOLE_R);
}

// --------------------------------------------------------------- Darstellung

static void apply_style(void) {
  bool hands = (s_style == STYLE_HANDS);
  layer_set_hidden(s_hour_layer, hands);
  layer_set_hidden(s_min_layer, hands);
  layer_set_hidden(s_batt_layer, hands);
  layer_set_hidden(s_dial_layer, !hands);
}

static void redraw_all(void) {
  layer_mark_dirty(s_hour_layer);
  layer_mark_dirty(s_min_layer);
  layer_mark_dirty(s_batt_layer);
  layer_mark_dirty(s_dial_layer);
}

// --------------------------------------------------------------- Zeit

// Setzt Stunde und gerundete Minute und markiert nur dann etwas dirty, wenn
// sich der angezeigte Wert geaendert hat. Das ist der eigentliche
// Sparmechanismus.
static void apply_time(struct tm *t, bool force) {
  // Grosszuegig bemessen: die Werte sind 0..23 bzw. 0..59, der Compiler kann
  // das seit der Rundungsrechnung aber nicht mehr herleiten und warnt sonst
  // vor abgeschnittener Ausgabe.
  char hour_buf[8];
  char min_buf[8];

#ifdef COAST_DEMO_TIME
  struct tm demo = *t;
  demo.tm_hour = 10;
  demo.tm_min = 10;
  t = &demo;
#endif

  // Aufrunden laeuft ueber die volle Stunde und ueber Mitternacht hinaus --
  // 13:58 wird zu 14:00, 23:58 zu 00:00. Deshalb wird in Minuten seit
  // Mitternacht gerechnet und die Stunde daraus abgeleitet, statt sie direkt
  // aus tm_hour zu nehmen.
  int total = t->tm_hour * 60 + t->tm_min;
  if (s_round == ROUND_UP) {
    total = ((total + s_interval_min - 1) / s_interval_min) * s_interval_min;
    if (total >= 24 * 60) total = 0;
  } else {
    total = (total / s_interval_min) * s_interval_min;
  }
  // Die Klemmung ist sachlich ueberfluessig -- total liegt unter 1440 -- aber
  // der Compiler kann den Bereich hier nicht mehr herleiten und behandelt die
  // Truncation-Warnung auf den alten Plattformen als Fehler.
  int hour24  = (total / 60) % 24;
  int rounded = (total % 60) % 60;
  if (hour24 < 0) hour24 = 0;
  if (rounded < 0) rounded = 0;

  int hour = hour24;
  if (!clock_is_24h_style()) {
    hour = hour % 12;
    if (hour == 0) hour = 12;
    snprintf(hour_buf, sizeof(hour_buf), "%d", hour);
  } else {
    snprintf(hour_buf, sizeof(hour_buf), "%02d", hour);
  }
  snprintf(min_buf, sizeof(min_buf), "%02d", rounded);

  // Fuer die Nachtumschaltung zaehlt die echte Stunde, nicht die gerundete --
  // sonst kippt das Thema beim Aufrunden bis zu einem Raster zu frueh.
  bool night = night_at_hour(t->tm_hour);
  if (night != s_is_night) {
    s_is_night = night;
    window_set_background_color(s_window, cur_bg());
    force = true;
  }

  bool hour_changed = force || strcmp(hour_buf, s_hour_text) != 0;
  bool min_changed  = force || strcmp(min_buf, s_min_text) != 0;

  if (hour_changed) {
    strncpy(s_hour_text, hour_buf, sizeof(s_hour_text) - 1);
    s_hour_text[sizeof(s_hour_text) - 1] = 0;
  }
  if (min_changed) {
    strncpy(s_min_text, min_buf, sizeof(s_min_text) - 1);
    s_min_text[sizeof(s_min_text) - 1] = 0;
  }
  s_hand_hour = hour24 % 12;
  s_hand_min  = rounded;

  if (s_style == STYLE_HANDS) {
    if (hour_changed || min_changed) layer_mark_dirty(s_dial_layer);
  } else {
    if (hour_changed) layer_mark_dirty(s_hour_layer);
    if (min_changed)  layer_mark_dirty(s_min_layer);
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  apply_time(tick_time, false);
#ifdef COAST_DRAW_STATS
  APP_LOG(APP_LOG_LEVEL_INFO, "draws=%d", s_draws);
#endif
}

// --------------------------------------------------------------- Batterie

// Zeigt die verbleibende Laufzeit, sobald eine Rate gemessen ist -- vorher
// schlicht den Ladestand.
static void apply_battery_text(void) {
  char buf[20];

  if (s_sec_per_pct > 0) {
    int32_t remaining = (int32_t)s_charge_pct * (int32_t)s_sec_per_pct;
    if (remaining >= 86400) {
      int days = (int)(remaining / 86400);
      int tenths = (int)((remaining % 86400) * 10 / 86400);
      snprintf(buf, sizeof(buf), "%d.%d DAYS LEFT", days, tenths);
    } else if (remaining >= 7200) {
      snprintf(buf, sizeof(buf), "%d HOURS LEFT", (int)(remaining / 3600));
    } else if (remaining >= 3600) {
      snprintf(buf, sizeof(buf), "1 HOUR LEFT");
    } else {
      snprintf(buf, sizeof(buf), "UNDER 1 HOUR");
    }
  } else {
    snprintf(buf, sizeof(buf), "%d%%", s_charge_pct);
  }

  if (strcmp(buf, s_batt_text) != 0) {
    strncpy(s_batt_text, buf, sizeof(s_batt_text) - 1);
    s_batt_text[sizeof(s_batt_text) - 1] = 0;
    layer_mark_dirty(s_style == STYLE_HANDS ? s_dial_layer : s_batt_layer);
  }
}

static void battery_handler(BatteryChargeState state) {
  time_t now = time(NULL);
  int pct = state.charge_percent;

  if (state.is_charging || state.is_plugged || pct > s_last_pct) {
    // Laden: Referenz neu setzen, aber keine Rate ableiten.
    s_last_pct = pct;
    s_last_pct_time = now;
  } else if (s_last_pct >= 0 && pct < s_last_pct) {
    if (s_last_pct_time == 0) {
      // Erster beobachteter Wechsel: nur verankern. Wie lange der vorherige
      // Prozentwert schon stand, bevor das Watchface lief, wissen wir nicht --
      // daraus eine Rate zu rechnen wuerde die Laufzeit unterschaetzen.
      s_last_pct = pct;
      s_last_pct_time = now;
    } else {
      // Der Anker bleibt stehen, bis beide Schwellen erreicht sind -- so wird
      // ueber einen laengeren Abschnitt gemittelt statt ueber einen Einzelpunkt.
      //
      // Solange es noch gar keine Prognose gibt, reicht ein einzelner
      // Prozentpunkt: bei einem sparsamen Watchface steht ein Prozentpunkt
      // viele Stunden, und drei davon abzuwarten hiesse, tagelang nichts
      // anzuzeigen. Die erste Zahl darf grob sein -- sie wird mit jeder
      // weiteren Messung ueber RATE_SMOOTHING nachgezogen.
      int min_drop = (s_sec_per_pct == 0) ? 1 : RATE_MIN_DROP;
      int drop = s_last_pct - pct;
      int dt = (int)(now - s_last_pct_time);
      if (drop >= min_drop && dt >= RATE_MIN_SECONDS) {
        int sec_per_pct = dt / drop;
        s_sec_per_pct = (s_sec_per_pct == 0)
            ? sec_per_pct
            : (s_sec_per_pct * (RATE_SMOOTHING - 1) + sec_per_pct) / RATE_SMOOTHING;
        s_last_pct = pct;
        s_last_pct_time = now;
        persist_write_int(PKEY_SEC_PER_PCT, s_sec_per_pct);
      }
    }
  }

  persist_write_int(PKEY_LAST_PCT, s_last_pct);
  persist_write_int(PKEY_LAST_TIME, (int)s_last_pct_time);

  s_charge_pct = pct;
  apply_battery_text();
}

// --------------------------------------------------------------- Einstellungen

// Faengt die Farbwahl FG == BG ab, die das Watchface unlesbar machen wuerde.
// Clay kann das nicht verhindern, also tut es die Uhr.
static void enforce_contrast(void) {
  if (gcolor_equal(s_color_fg, s_color_bg)) {
    s_color_fg = gcolor_legible_over(s_color_bg);
  }
}

// Clay-Selects kommen je nach Konfiguration als String oder als Zahl an.
// Beides annehmen -- ein falsch geratener Typ wuerde stumm Muell liefern.
static int tuple_int(Tuple *t) {
  return (t->type == TUPLE_CSTRING) ? atoi(t->value->cstring)
                                    : (int)t->value->int32;
}

static void load_settings(void) {
  s_color_bg = COLOR_BG_DEFAULT;
  s_color_fg = COLOR_FG_DEFAULT;

  if (persist_exists(PKEY_INTERVAL)) {
    int v = persist_read_int(PKEY_INTERVAL);
    if (v == 5 || v == 15) s_interval_min = v;
  }
  if (persist_exists(PKEY_STYLE)) {
    int v = persist_read_int(PKEY_STYLE);
    if (v == STYLE_DIGITS || v == STYLE_HANDS) s_style = v;
  }
  if (persist_exists(PKEY_ROUND)) {
    int v = persist_read_int(PKEY_ROUND);
    if (v == ROUND_DOWN || v == ROUND_UP) s_round = v;
  }
  if (persist_exists(PKEY_NIGHT_MODE)) {
    s_night_mode = persist_read_bool(PKEY_NIGHT_MODE);
  }
  if (persist_exists(PKEY_NIGHT_START)) {
    int v = persist_read_int(PKEY_NIGHT_START);
    if (v >= 0 && v <= 23) s_night_start = v;
  }
  if (persist_exists(PKEY_DAY_START)) {
    int v = persist_read_int(PKEY_DAY_START);
    if (v >= 0 && v <= 23) s_day_start = v;
  }
  if (persist_exists(PKEY_COLOR_BG)) {
    s_color_bg = GColorFromHEX(persist_read_int(PKEY_COLOR_BG));
  }
  if (persist_exists(PKEY_COLOR_FG)) {
    s_color_fg = GColorFromHEX(persist_read_int(PKEY_COLOR_FG));
  }
  if (persist_exists(PKEY_SHOW_BATTERY)) {
    s_show_battery = persist_read_bool(PKEY_SHOW_BATTERY);
  }
  if (persist_exists(PKEY_SEC_PER_PCT)) s_sec_per_pct   = persist_read_int(PKEY_SEC_PER_PCT);
  if (persist_exists(PKEY_LAST_PCT))    s_last_pct      = persist_read_int(PKEY_LAST_PCT);
  if (persist_exists(PKEY_LAST_TIME))   s_last_pct_time = (time_t)persist_read_int(PKEY_LAST_TIME);

  enforce_contrast();
}

static void inbox_received(DictionaryIterator *iter, void *context) {
  Tuple *t;

  if ((t = dict_find(iter, MESSAGE_KEY_INTERVAL)) != NULL) {
    int v = tuple_int(t);
    if (v == 5 || v == 15) {
      s_interval_min = v;
      persist_write_int(PKEY_INTERVAL, v);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_STYLE)) != NULL) {
    int v = tuple_int(t);
    if (v == STYLE_DIGITS || v == STYLE_HANDS) {
      s_style = v;
      persist_write_int(PKEY_STYLE, v);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_ROUND)) != NULL) {
    int v = tuple_int(t);
    if (v == ROUND_DOWN || v == ROUND_UP) {
      s_round = v;
      persist_write_int(PKEY_ROUND, v);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_MODE)) != NULL) {
    s_night_mode = (t->length >= 4) ? (t->value->int32 != 0)
                                    : (t->value->uint8 != 0);
    persist_write_bool(PKEY_NIGHT_MODE, s_night_mode);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_NIGHT_START)) != NULL) {
    int v = tuple_int(t);
    if (v >= 0 && v <= 23) {
      s_night_start = v;
      persist_write_int(PKEY_NIGHT_START, v);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_DAY_START)) != NULL) {
    int v = tuple_int(t);
    if (v >= 0 && v <= 23) {
      s_day_start = v;
      persist_write_int(PKEY_DAY_START, v);
    }
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_BG)) != NULL) {
    int v = (int)t->value->int32;
    s_color_bg = GColorFromHEX(v);
    persist_write_int(PKEY_COLOR_BG, v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_COLOR_FG)) != NULL) {
    int v = (int)t->value->int32;
    s_color_fg = GColorFromHEX(v);
    persist_write_int(PKEY_COLOR_FG, v);
  }
  if ((t = dict_find(iter, MESSAGE_KEY_SHOW_BATTERY)) != NULL) {
    // Toggles koennen als 1-Byte-Wert ankommen; int32 wuerde darueber
    // hinauslesen.
    s_show_battery = (t->length >= 4) ? (t->value->int32 != 0)
                                      : (t->value->uint8 != 0);
    persist_write_bool(PKEY_SHOW_BATTERY, s_show_battery);
  }

  enforce_contrast();
  apply_style();

  // apply_time bewertet den Nachtzustand neu; erst danach steht die
  // Hintergrundfarbe fest.
  time_t now = time(NULL);
  s_is_night = night_at_hour(localtime(&now)->tm_hour);
  window_set_background_color(s_window, cur_bg());
  apply_time(localtime(&now), true);
  redraw_all();
}

// --------------------------------------------------------------- Fenster

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect b = layer_get_bounds(root);

  s_hour_layer = layer_create(GRect(0, HOUR_Y, b.size.w, DIGIT_H));
  layer_set_update_proc(s_hour_layer, hour_update_proc);
  layer_add_child(root, s_hour_layer);

  s_min_layer = layer_create(GRect(0, MIN_Y, b.size.w, DIGIT_H));
  layer_set_update_proc(s_min_layer, min_update_proc);
  layer_add_child(root, s_min_layer);

  s_batt_layer = layer_create(GRect(0, BATT_Y, b.size.w, BATT_H));
  layer_set_update_proc(s_batt_layer, batt_update_proc);
  layer_add_child(root, s_batt_layer);

  // Versteckte Layer werden nicht gezeichnet -- es laeuft immer nur der
  // Zeichenweg der gewaehlten Darstellung.
  s_dial_layer = layer_create(b);
  layer_set_update_proc(s_dial_layer, dial_update_proc);
  layer_add_child(root, s_dial_layer);

  apply_style();
}

static void window_unload(Window *window) {
  layer_destroy(s_hour_layer);
  layer_destroy(s_min_layer);
  layer_destroy(s_batt_layer);
  layer_destroy(s_dial_layer);
}

// --------------------------------------------------------------- Start

static void init(void) {
  load_settings();

  s_digit_font = fonts_load_custom_font(resource_get_handle(RES_DIGIT_FONT));

  time_t boot = time(NULL);
  s_is_night = night_at_hour(localtime(&boot)->tm_hour);

  s_window = window_create();
  window_set_background_color(s_window, cur_bg());
  window_set_window_handlers(s_window, (WindowHandlers){
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, false);

  time_t now = time(NULL);
  apply_time(localtime(&now), true);

  BatteryChargeState st = battery_state_service_peek();
#ifdef COAST_DEMO_RATE
  s_sec_per_pct = COAST_DEMO_RATE;
#endif
  if (s_last_pct < 0) {
    // Erster Start ueberhaupt: Zeitstempel bewusst auf 0. Der erste
    // beobachtete Wechsel dient nur als Anker, gerechnet wird ab dem zweiten
    // -- wie lange der aktuelle Wert schon steht, wissen wir nicht.
    s_charge_pct = st.charge_percent;
    s_last_pct = s_charge_pct;
    s_last_pct_time = 0;
    apply_battery_text();
  } else {
    // Es gibt bereits einen Anker aus einem frueheren Lauf. Waehrend ein
    // anderes Watchface lief, hat Coast keine Events bekommen -- der jetzige
    // Stand ist deshalb ein vollwertiger Messpunkt und darf nicht verfallen.
    battery_handler(st);
  }

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);

  app_message_register_inbox_received(inbox_received);
  app_message_open(256, 64);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
  fonts_unload_custom_font(s_digit_font);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
  return 0;
}
