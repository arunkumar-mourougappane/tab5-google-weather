#include "daily_forecast_ui.h"

#include <lvgl.h>
#include <time.h>

#include "dashboard_fonts.h"
#include "dashboard_icon_lookup.h"
#include "dashboard_ui.h"
#include "date_utils.h"
#include "logging.h"
#include "weather_icon.h"

// The 7-Day-forecast screen (docs/mockups/daily-forecast.html), reached
// by tapping the Dashboard's "7-DAY OUTLOOK" section (dashboard_ui.cpp's
// onDailyTitleClicked()). Surfaces the day/night split, sunrise/sunset,
// and moon phase the Dashboard's own compact 7-row list throws away
// (dashboard_ui.cpp's buildDailyColumn only ever shows the daytime
// condition + a combined hi/lo range).
//
// Same build-once/update-in-place convention as dashboard_ui.cpp/
// hourly_detail_ui.cpp: this file's own g_screen is built on the first
// showDailyForecastScreen() call, then every later call just updates
// widgets in place. Unlike the Dashboard's own showDashboardScreen()
// (which relies on lv_screen_load() firing only inside its build path,
// safe there because it's *also* called on every background refresh
// regardless of which screen is visible), this screen's tap handler
// (dashboard_ui.cpp's onDailyTitleClicked()) calls lv_screen_load()
// itself, every time - the Hourly-detail screen shipped a real bug
// (fixed earlier this session) from conflating "build once" with "load
// once": re-opening it after returning to the Dashboard silently
// updated an inactive screen instead of switching to it.
//
// Day rows are 7 flex rows in a 1-column/7-row LV_GRID_FR(1) grid (not a
// scrollable list, despite the mockup's own caption calling it that) -
// same "guaranteed to fit, no shrink-to-fit in LVGL flex" reasoning
// dashboard_ui.cpp's buildDailyColumn already documents, reused here
// rather than re-derived.
//
// Sunrise/sunset are RFC3339 UTC timestamps with no full epoch/date
// parser in this codebase yet - localTimeOfDay() below extracts just the
// time-of-day and shifts it by utcOffsetSec (a wall-clock display, not a
// scheduling feature, so this is deliberately simpler than a real
// RFC3339->epoch conversion). No moon illumination % - the API/fixture
// has no such field; the mockup's own "82%" is decoration, not real
// data, so it's dropped rather than fabricated.
namespace {

// Same dark-theme palette dashboard_ui.cpp/hourly_detail_ui.cpp define -
// not shared via a common header, matching this project's existing
// per-screen-file convention (no UI-kit exists yet).
constexpr uint32_t kInk = 0xF0E9D8;
constexpr uint32_t kSub = 0xA89E8C;
constexpr uint32_t kPaper = 0x121417;
constexpr uint32_t kPanel = 0x1B1E22;
constexpr uint32_t kLine = 0x2A2E33;
constexpr uint32_t kLineStrong = 0x3A3F45;
constexpr uint32_t kBrass = 0xD99A4E;
constexpr uint32_t kBrassStrong = 0xE9B06C;
constexpr uint32_t kTeal = 0x6FB3AC;
constexpr uint32_t kEmber = 0xE2794E;

constexpr size_t kMaxRows = kMaxDailyPoints;

struct SegmentWidgets {
  lv_obj_t *icon = nullptr;
  lv_obj_t *temp = nullptr;
  lv_obj_t *pop = nullptr;
};

struct DayRowWidgets {
  lv_obj_t *row = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *date = nullptr;
  lv_obj_t *condIcon = nullptr;
  lv_obj_t *condLabel = nullptr;
  SegmentWidgets day;
  SegmentWidgets night;
  lv_obj_t *track = nullptr;
  lv_obj_t *fill = nullptr;
  lv_obj_t *lo = nullptr;
  lv_obj_t *hi = nullptr;
};

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_titleSub = nullptr;
lv_obj_t *g_clockLabel = nullptr;
DayRowWidgets g_rows[kMaxRows];
lv_obj_t *g_sunriseLabel = nullptr;
lv_obj_t *g_sunsetLabel = nullptr;
lv_obj_t *g_moonLabel = nullptr;

lv_obj_t *makeLabel(lv_obj_t *parent, uint32_t colorArgb, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_style_text_color(label, lv_color_hex(colorArgb), 0);
  lv_obj_set_style_text_font(label, font, 0);
  return label;
}

lv_obj_t *makeRow(lv_obj_t *parent) {
  lv_obj_t *row = lv_obj_create(parent);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  return row;
}

lv_obj_t *makeColumn(lv_obj_t *parent) {
  lv_obj_t *col = lv_obj_create(parent);
  lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(col, 0, 0);
  lv_obj_set_style_pad_all(col, 0, 0);
  lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
  return col;
}

// Animated press feedback for a clickable pill button - see
// hourly_detail_ui.cpp's own copy of this function for the full comment
// (same duplicated-per-file convention).
void addPressFeedback(lv_obj_t *btn) {
  static const lv_style_prop_t kProps[] = {LV_STYLE_BG_OPA, LV_STYLE_BG_COLOR, LV_STYLE_BORDER_COLOR,
                                            LV_STYLE_PROP_INV};
  static lv_style_transition_dsc_t kTransition;
  lv_style_transition_dsc_init(&kTransition, kProps, lv_anim_path_ease_out, 120, 0, nullptr);
  lv_obj_set_style_transition(btn, &kTransition, LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(btn, lv_color_hex(kBrass), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_PRESSED);
  lv_obj_set_style_border_color(btn, lv_color_hex(kBrassStrong), LV_STATE_PRESSED);
}

void onBackClicked(lv_event_t *e) {
  (void)e;
  LOG_D("ui", "daily-forecast back button clicked, target=%p\n", static_cast<void *>(dashboardScreenObj()));
  lv_screen_load(dashboardScreenObj());
}

// ---------------------------------------------------------------------
// Statusbar: back button + title/location (left), clock (right). Same
// shape as hourly_detail_ui.cpp's buildStatusbar() - both screens share
// this layout, and reuse its exact fonts (font_plexmono_backbtn_16/
// font_archivo_hdtitle_26/font_plexmono_hdsub_16/font_plexmono_clock_20).
// ---------------------------------------------------------------------
lv_obj_t *buildStatusbar(lv_obj_t *parent) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 30, 0);
  lv_obj_set_style_pad_top(bar, 16, 0);
  lv_obj_set_style_pad_bottom(bar, 10, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *back = makeRow(bar);
  lv_obj_set_style_pad_column(back, 12, 0);
  lv_obj_set_flex_align(back, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *backBtn = lv_obj_create(back);
  lv_obj_set_size(backBtn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(backBtn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(backBtn, lv_color_hex(kLineStrong), 0);
  lv_obj_set_style_border_width(backBtn, 1, 0);
  lv_obj_set_style_radius(backBtn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(backBtn, 12, 0);
  lv_obj_set_style_pad_ver(backBtn, 6, 0);
  lv_obj_clear_flag(backBtn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(backBtn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(backBtn, onBackClicked, LV_EVENT_CLICKED, nullptr);
  addPressFeedback(backBtn);
  lv_obj_t *backLabel = makeLabel(backBtn, kSub, &font_plexmono_backbtn_16);
  lv_label_set_text(backLabel, "<- Dashboard");

  lv_obj_t *titleCol = makeColumn(back);
  // 7px, not 2px - the location sub-label sat too close under the title,
  // per request.
  lv_obj_set_style_pad_row(titleCol, 7, 0);
  lv_obj_t *title = makeLabel(titleCol, kInk, &font_archivo_hdtitle_26);
  lv_label_set_text(title, "7-day outlook");
  g_titleSub = makeLabel(titleCol, kSub, &font_plexmono_hdsub_18);

  g_clockLabel = makeLabel(bar, kInk, &font_plexmono_clock_20);

  return bar;
}

// ---------------------------------------------------------------------
// Column widths shared between the head row and every day row, so their
// columns actually line up. Mockup: grid-template-columns: 118px 150px
// 1fr 1fr 96px - kept as fixed-width flex children here (not a real
// LVGL grid) rather than reinventing the wheel: this is the exact same
// "row is a flex row, columns are fixed-width/flex_grow children"
// pattern dashboard_ui.cpp's own buildDayRow already uses successfully.
// ---------------------------------------------------------------------
constexpr int32_t kNameColW = 140;
constexpr int32_t kCondColW = 260;
constexpr int32_t kRangeColW = 150;

lv_obj_t *buildHeadRow(lv_obj_t *parent) {
  lv_obj_t *row = makeRow(parent);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_hor(row, 30, 0);
  lv_obj_set_style_pad_ver(row, 8, 0);
  lv_obj_set_style_pad_column(row, 14, 0);
  lv_obj_set_style_border_side(row, static_cast<lv_border_side_t>(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM), 0);
  lv_obj_set_style_border_color(row, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *dayL = makeLabel(row, kSub, &font_plexmono_headcol_14);
  lv_label_set_text(dayL, "DAY");
  lv_obj_set_width(dayL, kNameColW);

  lv_obj_t *condL = makeLabel(row, kSub, &font_plexmono_headcol_14);
  lv_label_set_text(condL, "CONDITION");
  lv_obj_set_width(condL, kCondColW);

  lv_obj_t *dayHL = makeLabel(row, kSub, &font_plexmono_headcol_14);
  lv_label_set_text(dayHL, "DAY");
  lv_obj_set_flex_grow(dayHL, 1);

  lv_obj_t *nightHL = makeLabel(row, kSub, &font_plexmono_headcol_14);
  lv_label_set_text(nightHL, "NIGHT");
  lv_obj_set_flex_grow(nightHL, 1);

  lv_obj_t *rangeL = makeLabel(row, kSub, &font_plexmono_headcol_14);
  lv_label_set_text(rangeL, "RANGE");
  lv_obj_set_width(rangeL, kRangeColW);
  lv_obj_set_style_text_align(rangeL, LV_TEXT_ALIGN_RIGHT, 0);

  return row;
}

SegmentWidgets buildSegment(lv_obj_t *parent) {
  SegmentWidgets w;
  lv_obj_t *seg = makeRow(parent);
  lv_obj_set_flex_grow(seg, 1);
  lv_obj_set_style_pad_column(seg, 8, 0);
  lv_obj_set_flex_align(seg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  w.icon = lv_image_create(seg);
  lv_obj_set_size(w.icon, 18, 18);
  w.temp = makeLabel(seg, kInk, &font_plexmono_segtemp_20);
  w.pop = makeLabel(seg, kTeal, &font_plexmono_segpop_16);

  return w;
}

// ---------------------------------------------------------------------
// One day row - same "flex row of fixed-width/flex_grow cells" shape as
// dashboard_ui.cpp's buildDayRow, extended with the condition-label and
// day/night segment cells that screen doesn't need. Range track/fill
// widgets are copied verbatim from that function.
// ---------------------------------------------------------------------
DayRowWidgets buildDayRow(lv_obj_t *parent, bool isFirst) {
  DayRowWidgets w;
  w.row = makeRow(parent);
  lv_obj_set_size(w.row, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_pad_hor(w.row, 30, 0);
  lv_obj_set_style_pad_column(w.row, 14, 0);
  lv_obj_set_flex_align(w.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (!isFirst) {
    lv_obj_set_style_border_side(w.row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(w.row, lv_color_hex(kLine), 0);
    lv_obj_set_style_border_width(w.row, 1, 0);
  } else {
    // Mockup's .day-row.today background highlight - isFirst always maps
    // to data.daily[0] ("Today"), same index the loop below brass-
    // highlights the name label for.
    lv_obj_set_style_bg_color(w.row, lv_color_hex(kPanel), 0);
    lv_obj_set_style_bg_opa(w.row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(w.row, 8, 0);
  }

  lv_obj_t *nameCol = makeColumn(w.row);
  lv_obj_set_width(nameCol, kNameColW);
  lv_obj_set_style_pad_row(nameCol, 1, 0);
  w.name = makeLabel(nameCol, kInk, &font_archivo_dayname_14);
  w.date = makeLabel(nameCol, kSub, &font_plexmono_dailydate_16);

  lv_obj_t *condRow = makeRow(w.row);
  lv_obj_set_width(condRow, kCondColW);
  lv_obj_set_style_pad_column(condRow, 10, 0);
  lv_obj_set_flex_align(condRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  w.condIcon = lv_image_create(condRow);
  lv_obj_set_size(w.condIcon, 26, 26);
  w.condLabel = makeLabel(condRow, kInk, &font_archivo_dailydesc_18);
  lv_obj_set_flex_grow(w.condLabel, 1);
  lv_label_set_long_mode(w.condLabel, LV_LABEL_LONG_DOT);

  w.day = buildSegment(w.row);
  w.night = buildSegment(w.row);

  lv_obj_t *range = makeRow(w.row);
  lv_obj_set_width(range, kRangeColW);
  lv_obj_set_style_pad_column(range, 8, 0);
  lv_obj_set_flex_align(range, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  w.lo = makeLabel(range, kTeal, &font_plexmono_segtemp_20);

  w.track = lv_obj_create(range);
  lv_obj_set_size(w.track, LV_PCT(100), 6);
  lv_obj_set_flex_grow(w.track, 1);
  lv_obj_set_style_bg_color(w.track, lv_color_hex(kLine), 0);
  lv_obj_set_style_bg_opa(w.track, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.track, 0, 0);
  lv_obj_set_style_radius(w.track, 3, 0);
  lv_obj_set_style_pad_all(w.track, 0, 0);
  lv_obj_clear_flag(w.track, LV_OBJ_FLAG_SCROLLABLE);

  w.fill = lv_obj_create(w.track);
  lv_obj_set_size(w.fill, 4, 6);
  lv_obj_set_style_bg_color(w.fill, lv_color_hex(kTeal), 0);
  lv_obj_set_style_bg_grad_color(w.fill, lv_color_hex(kEmber), 0);
  lv_obj_set_style_bg_grad_dir(w.fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(w.fill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.fill, 0, 0);
  lv_obj_set_style_radius(w.fill, 3, 0);
  lv_obj_clear_flag(w.fill, LV_OBJ_FLAG_SCROLLABLE);

  w.hi = makeLabel(range, kEmber, &font_plexmono_segtemp_20);

  return w;
}

lv_obj_t *buildDayRows(lv_obj_t *parent) {
  lv_obj_t *rows = lv_obj_create(parent);
  lv_obj_set_size(rows, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(rows, 1);
  lv_obj_set_style_bg_opa(rows, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rows, 0, 0);
  lv_obj_set_style_pad_all(rows, 0, 0);
  lv_obj_clear_flag(rows, LV_OBJ_FLAG_SCROLLABLE);
  static int32_t colDsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rowDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                              LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static_assert(kMaxRows == 7, "rowDsc above is hand-written for 7 rows");
  lv_obj_set_grid_dsc_array(rows, colDsc, rowDsc);
  lv_obj_set_layout(rows, LV_LAYOUT_GRID);

  for (size_t i = 0; i < kMaxRows; i++) {
    g_rows[i] = buildDayRow(rows, i == 0);
    lv_obj_set_grid_cell(g_rows[i].row, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i),
                          1);
  }

  return rows;
}

// ---------------------------------------------------------------------
// Sun/moon bar: sunrise/sunset/moon-phase for "today" (daily[0]) + API
// attribution, matching the mockup's .sunbar.
// ---------------------------------------------------------------------
lv_obj_t *buildSunbar(lv_obj_t *parent) {
  lv_obj_t *bar = makeRow(parent);
  lv_obj_set_width(bar, LV_PCT(100));
  lv_obj_set_style_pad_hor(bar, 30, 0);
  lv_obj_set_style_pad_ver(bar, 12, 0);
  lv_obj_set_style_pad_column(bar, 22, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *left = makeRow(bar);
  lv_obj_set_style_pad_column(left, 22, 0);
  lv_obj_set_flex_align(left, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  g_sunriseLabel = makeLabel(left, kSub, &font_plexmono_sunbar_16);
  g_sunsetLabel = makeLabel(left, kSub, &font_plexmono_sunbar_16);
  g_moonLabel = makeLabel(left, kSub, &font_plexmono_sunbar_16);

  lv_obj_t *attribution = makeLabel(bar, kSub, &font_plexmono_sunbar_16);
  lv_label_set_text(attribution, "weather.googleapis.com · v1/forecast/days");

  return bar;
}

void buildScreen() {
  g_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(g_screen, lv_color_hex(kPaper), 0);
  lv_obj_set_style_bg_opa(g_screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_screen, 0, 0);
  lv_obj_set_style_border_width(g_screen, 0, 0);
  lv_obj_set_flex_flow(g_screen, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(g_screen, LV_OBJ_FLAG_SCROLLABLE);

  buildStatusbar(g_screen);
  buildHeadRow(g_screen);
  buildDayRows(g_screen);
  buildSunbar(g_screen);

  lv_screen_load(g_screen);
}

// Extracts an RFC3339 UTC timestamp's HH:MM time-of-day and shifts it by
// utcOffsetSec, wrapping across a UTC day boundary via modulo 1440 - see
// this file's own top comment for why this isn't a full epoch parse.
String localTimeOfDay(const String &rfc3339Utc, int32_t utcOffsetSec) {
  const int t = rfc3339Utc.indexOf('T');
  if (t < 0 || rfc3339Utc.length() < static_cast<unsigned>(t) + 6) {
    return "--:--";
  }
  const int hh = rfc3339Utc.substring(t + 1, t + 3).toInt();
  const int mm = rfc3339Utc.substring(t + 4, t + 6).toInt();
  int totalMin = (hh * 60 + mm + utcOffsetSec / 60) % 1440;
  if (totalMin < 0) {
    totalMin += 1440;
  }
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", totalMin / 60, totalMin % 60);
  return String(buf);
}

// "WANING_GIBBOUS" -> "Waning gibbous" - replace underscores with
// spaces, lowercase, capitalize just the first letter.
String formatMoonPhase(const String &raw) {
  if (raw.length() == 0) {
    return "--";
  }
  String out = raw;
  out.replace('_', ' ');
  out.toLowerCase();
  out.setCharAt(0, static_cast<char>(toupper(static_cast<unsigned char>(out[0]))));
  return out;
}

}  // namespace

void showDailyForecastScreen(const SharedState::DashboardSnapshot &data) {
  const bool firstBuild = (g_screen == nullptr);
  if (firstBuild) {
    buildScreen();
    // See lv_conf.h's LV_MEM_SIZE comment for why this is being watched.
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LOG_D("ui", "lv mem after daily-forecast build: used=%u%% frag=%u%% free=%u\n", mon.used_pct, mon.frag_pct,
          static_cast<unsigned>(mon.free_size));
  }
  LOG_D("ui", "showDailyForecastScreen called, firstBuild=%d screen=%p\n", firstBuild, static_cast<void *>(g_screen));

  lv_label_set_text(g_titleSub, data.displayLocation.c_str());

  float dayMin = 1e9f;
  float dayMax = -1e9f;
  for (size_t i = 0; i < data.dailyCount; i++) {
    dayMin = min(dayMin, data.daily[i].minTemperature);
    dayMax = max(dayMax, data.daily[i].maxTemperature);
  }
  const float dayRange = (dayMax > dayMin) ? (dayMax - dayMin) : 1.0f;

  for (size_t i = 0; i < kMaxRows; i++) {
    DayRowWidgets &w = g_rows[i];
    if (i >= data.dailyCount) {
      lv_obj_add_flag(w.row, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(w.row, LV_OBJ_FLAG_HIDDEN);

    const DailyForecastPoint &day = data.daily[i];
    if (i == 0) {
      lv_label_set_text(w.name, "Today");
      lv_obj_set_style_text_color(w.name, lv_color_hex(kBrassStrong), 0);
    } else if (i == 1) {
      lv_label_set_text(w.name, "Tomorrow");
      lv_obj_set_style_text_color(w.name, lv_color_hex(kInk), 0);
    } else {
      const int weekday = weekdayFromIsoDate(day.displayDate);
      lv_label_set_text(w.name, weekday >= 0 ? weekdayAbbrev(weekday) : day.displayDate.c_str());
      lv_obj_set_style_text_color(w.name, lv_color_hex(kInk), 0);
    }

    if (day.displayDate.length() == 10) {
      const int month = day.displayDate.substring(5, 7).toInt();
      const int dayOfMonth = day.displayDate.substring(8, 10).toInt();
      char dateBuf[8];
      snprintf(dateBuf, sizeof(dateBuf), "%s %d", monthAbbrev(month), dayOfMonth);
      lv_label_set_text(w.date, dateBuf);
    } else {
      lv_label_set_text(w.date, day.displayDate.c_str());
    }

    lv_image_set_src(w.condIcon, iconForBucket(bucketForCondition(day.daytimeCondition.type.c_str(),
                                                                    /*isDaytime=*/true),
                                                IconSize::DayRow));
    lv_label_set_text(w.condLabel, day.daytimeCondition.description.c_str());

    lv_image_set_src(w.day.icon, iconForBucket(bucketForCondition(day.daytimeCondition.type.c_str(),
                                                                    /*isDaytime=*/true),
                                                IconSize::Segment));
    char dayTempBuf[8];
    snprintf(dayTempBuf, sizeof(dayTempBuf), "%d°", static_cast<int>(day.maxTemperature));
    lv_label_set_text(w.day.temp, dayTempBuf);
    char dayPopBuf[8];
    snprintf(dayPopBuf, sizeof(dayPopBuf), "%d%%", day.precipitationProbabilityPercent);
    lv_label_set_text(w.day.pop, dayPopBuf);

    lv_image_set_src(w.night.icon, iconForBucket(bucketForCondition(day.nighttimeCondition.type.c_str(),
                                                                      /*isDaytime=*/false),
                                                  IconSize::Segment));
    char nightTempBuf[8];
    snprintf(nightTempBuf, sizeof(nightTempBuf), "%d°", static_cast<int>(day.minTemperature));
    lv_label_set_text(w.night.temp, nightTempBuf);
    char nightPopBuf[8];
    snprintf(nightPopBuf, sizeof(nightPopBuf), "%d%%", day.nightPrecipitationProbabilityPercent);
    lv_label_set_text(w.night.pop, nightPopBuf);

    char loBuf[8];
    char hiBuf[8];
    snprintf(loBuf, sizeof(loBuf), "%d°", static_cast<int>(day.minTemperature));
    snprintf(hiBuf, sizeof(hiBuf), "%d°", static_cast<int>(day.maxTemperature));
    lv_label_set_text(w.lo, loBuf);
    lv_label_set_text(w.hi, hiBuf);

    // Same lv_obj_update_layout()-before-lv_obj_get_width() requirement
    // dashboard_ui.cpp's own buildDayRow hit on first build - flex
    // geometry isn't resolved until the next lv_timer_handler() pass
    // otherwise, silently leaving the fill bar invisible.
    lv_obj_update_layout(w.track);
    const int32_t trackWidth = lv_obj_get_width(w.track);
    if (trackWidth > 0) {
      const int32_t left =
          static_cast<int32_t>(((day.minTemperature - dayMin) / dayRange) * static_cast<float>(trackWidth));
      const int32_t width = static_cast<int32_t>(((day.maxTemperature - day.minTemperature) / dayRange) *
                                                   static_cast<float>(trackWidth));
      lv_obj_set_x(w.fill, left);
      lv_obj_set_width(w.fill, width < 3 ? 3 : width);
    }
  }

  if (data.dailyCount > 0) {
    const DailyForecastPoint &today = data.daily[0];
    char sunriseBuf[16];
    snprintf(sunriseBuf, sizeof(sunriseBuf), "Sunrise %s", localTimeOfDay(today.sunriseTime, data.utcOffsetSec).c_str());
    lv_label_set_text(g_sunriseLabel, sunriseBuf);
    char sunsetBuf[16];
    snprintf(sunsetBuf, sizeof(sunsetBuf), "Sunset %s", localTimeOfDay(today.sunsetTime, data.utcOffsetSec).c_str());
    lv_label_set_text(g_sunsetLabel, sunsetBuf);
    lv_label_set_text(g_moonLabel, formatMoonPhase(today.moonPhase).c_str());
  }
}

void refreshDailyForecastClock(const SharedState::DashboardSnapshot &data) {
  if (g_screen == nullptr) {
    return;
  }

  const time_t now = time(nullptr);
  const time_t localNow = now + data.utcOffsetSec;
  struct tm tmInfo;
  gmtime_r(&localNow, &tmInfo);
  char clockBuf[6];
  snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
  lv_label_set_text(g_clockLabel, clockBuf);
}

bool dailyForecastScreenBuilt() { return g_screen != nullptr; }

lv_obj_t *dailyForecastScreenObj() { return g_screen; }
