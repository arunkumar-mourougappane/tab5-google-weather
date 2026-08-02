#include "hourly_detail_ui.h"

#include <lvgl.h>
#include <time.h>

#include "dashboard_fonts.h"
#include "dashboard_icon_lookup.h"
#include "dashboard_ui.h"
#include "logging.h"
#include "weather_icon.h"

// The Hourly-detail screen (docs/mockups/hourly-detail.html), reached by
// tapping the Dashboard's "HOURLY" section (dashboard_ui.cpp's
// onHourlyTitleClicked()). Read at arm's length adjusting a thermostat,
// not from across the room, per the mockup's own intro text - started
// with the mockup's own raw px on that theory, same as the Dashboard's
// own fonts originally did, but per hardware/visual review the whole
// screen actually needed the same "bump it up from the mockup's raw
// value" treatment the Dashboard's fonts already got (see
// tools/dashboard_fonts_manifest.txt's own notes on each one).
//
// Same build-once/update-in-place convention as dashboard_ui.cpp: this
// file's own g_screen is built on the first showHourlyDetailScreen()
// call and lv_screen_load()ed, then every later call just updates
// widgets in place - see that file's top comment for why (partial-DMA-
// flush friendly, matches the mockups' own stated on-device goal).
//
// Chart renders as an LVGL lv_chart (line series + faint gridlines) -
// the mockup's own caption specifies this technique; hour cards are a
// static 2x8 grid, not a scroll list (same caption) - touch-scroll on a
// panel this size adds jank for little benefit over paging.
//
// Precipitation-chance bars are a second, overlaid lv_chart (LV_CHART_TYPE_BAR,
// its own 0-100 axis, independent of the temperature line's) - added after
// an initial pass that left them out per the mockup's own "renders as
// line series + faint gridlines" caption; on reflection/feedback that
// undersold what the mockup actually shows and skipped real information
// (rain chance), so they're in from the start now. Deliberately still not
// built: the gradient area fill *under* the temperature line specifically
// (a pure visual flourish, no information the line/points don't already
// carry - lv_chart's stock line series has no built-in fill, so it would
// need custom draw-event code for something purely decorative); a second
// network fetch (reuses the same SharedState::DashboardSnapshot.hourly[]
// the Dashboard already gets).
namespace {

// Same dark-theme palette dashboard_ui.cpp defines - not shared via a
// common header, matching this project's existing per-screen-file
// convention (no UI-kit exists yet; see lib/font_gallery, lib/icon_gallery
// for the only shared code so far, and neither is a widget-style kit).
constexpr uint32_t kInk = 0xF0E9D8;
constexpr uint32_t kSub = 0xA89E8C;
constexpr uint32_t kPaper = 0x121417;
constexpr uint32_t kPanel = 0x1B1E22;
constexpr uint32_t kPanel2 = 0x22262B;  // mockup's --panel-2, the "now" hour card's highlight background.
constexpr uint32_t kLine = 0x2A2E33;
constexpr uint32_t kLineStrong = 0x3A3F45;  // mockup's --line-strong, the back button's border.
constexpr uint32_t kBrass = 0xD99A4E;
constexpr uint32_t kBrassStrong = 0xE9B06C;
constexpr uint32_t kTeal = 0x6FB3AC;

constexpr size_t kMaxHourCards = kMaxHourlyPoints;

struct HourCardWidgets {
  lv_obj_t *card = nullptr;
  lv_obj_t *time = nullptr;
  lv_obj_t *icon = nullptr;
  lv_obj_t *temp = nullptr;
  lv_obj_t *pop = nullptr;
};

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_titleSub = nullptr;
lv_obj_t *g_clockLabel = nullptr;

lv_obj_t *g_chart = nullptr;
lv_chart_series_t *g_tempSeries = nullptr;
lv_obj_t *g_precipChart = nullptr;
lv_chart_series_t *g_precipSeries = nullptr;
lv_obj_t *g_chartHourLabels[kMaxHourCards] = {nullptr};

HourCardWidgets g_hourCards[kMaxHourCards];

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

// Animated press feedback for a clickable pill button: on LV_STATE_PRESSED
// the border brightens and a faint brass tint fades in, then reverses on
// release - lv_style_transition_dsc_t makes LVGL animate both directions
// automatically, no extra event callbacks needed. Duplicated in
// dashboard_ui.cpp (same per-screen-file convention as makeLabel/
// makeRow/makeColumn - no shared UI-kit yet).
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
  LOG_D("ui", "back button clicked, target=%p\n", static_cast<void *>(dashboardScreenObj()));
  lv_screen_load(dashboardScreenObj());
}

// ---------------------------------------------------------------------
// Statusbar: back button + title/location (left), clock (right).
// Mockup: .statusbar { padding: 16px 30px 10px }, .back gap 12,
// .back-btn { border 1px, radius 999px, padding 6px 12px 6px 10px }.
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
  lv_obj_set_style_pad_row(titleCol, 2, 0);
  lv_obj_t *title = makeLabel(titleCol, kInk, &font_archivo_hdtitle_26);
  lv_label_set_text(title, "Hourly forecast");
  g_titleSub = makeLabel(titleCol, kSub, &font_plexmono_hdsub_16);

  g_clockLabel = makeLabel(bar, kInk, &font_plexmono_clock_20);

  return bar;
}

// ---------------------------------------------------------------------
// Chart: temperature line + faint gridlines, precipitation-chance bars
// behind it, and a thin row of hour labels underneath. Two overlaid
// lv_chart widgets, not one - lv_chart's type (LINE vs. BAR) is
// per-widget, not per-series, and temperature/precip need independent Y
// axes (a ~50-90 degree range vs. a fixed 0-100% range) anyway. Stacked
// via kChartStack (plain absolute-position container, not a flex row/
// column), precip chart created first so the temp line's transparent
// background lets it show through underneath - matching the mockup's
// own layering (precip bars behind the temp line/gradient).
// hour labels underneath (lv_chart's own tick-label API doesn't fit the
// "first + every 3rd" cadence the mockup's own JS uses, so this is a
// separate row of plain labels positioned via
// lv_chart_get_point_pos_by_id() instead), and a small legend.
// Mockup: .chart-wrap { padding: 6px 26px 0 }, svg height 168px.
// ---------------------------------------------------------------------
constexpr int32_t kChartHeight = 168;

lv_obj_t *buildChartSection(lv_obj_t *parent) {
  lv_obj_t *section = makeColumn(parent);
  lv_obj_set_width(section, LV_PCT(100));
  lv_obj_set_style_pad_hor(section, 26, 0);
  lv_obj_set_style_pad_top(section, 6, 0);
  lv_obj_set_style_border_side(section, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(section, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(section, 1, 0);

  lv_obj_t *chartStack = lv_obj_create(section);
  lv_obj_set_size(chartStack, LV_PCT(100), kChartHeight);
  lv_obj_set_style_bg_opa(chartStack, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(chartStack, 0, 0);
  lv_obj_set_style_pad_all(chartStack, 0, 0);
  lv_obj_clear_flag(chartStack, LV_OBJ_FLAG_SCROLLABLE);

  g_precipChart = lv_chart_create(chartStack);
  lv_obj_set_size(g_precipChart, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(g_precipChart, 0, 0);
  lv_obj_set_style_bg_opa(g_precipChart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_precipChart, 0, 0);
  lv_obj_set_style_pad_all(g_precipChart, 4, 0);
  lv_chart_set_type(g_precipChart, LV_CHART_TYPE_BAR);
  lv_chart_set_point_count(g_precipChart, kMaxHourCards);
  lv_chart_set_update_mode(g_precipChart, LV_CHART_UPDATE_MODE_CIRCULAR);
  lv_chart_set_axis_range(g_precipChart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
  // LV_PART_MAIN's pad_column is the gap lv_chart's bar-type rendering
  // opens up between each hour's column (see lv_chart.c's draw_series_bar
  // block_w = (w - (point_cnt-1)*pad_column) / point_cnt) - stock LVGL
  // bars default to filling most of that column's width (~76px wide at
  // 16 points across this chart), reading as solid blocks rather than
  // the mockup's thin 6px teal ticks (hourly-detail.html's `width: 6`
  // precip rects). 75px of gap narrows the actual bar down to ~6px to
  // match.
  lv_obj_set_style_pad_column(g_precipChart, 75, LV_PART_MAIN);
  lv_obj_set_style_radius(g_precipChart, 2, LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(g_precipChart, LV_OPA_60, LV_PART_ITEMS);
  g_precipSeries = lv_chart_add_series(g_precipChart, lv_color_hex(kTeal), LV_CHART_AXIS_PRIMARY_Y);

  g_chart = lv_chart_create(chartStack);
  lv_obj_set_size(g_chart, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(g_chart, 0, 0);
  lv_obj_set_style_bg_opa(g_chart, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_chart, 0, 0);
  lv_obj_set_style_pad_all(g_chart, 4, 0);
  lv_obj_set_style_line_color(g_chart, lv_color_hex(kLine), LV_PART_MAIN);
  lv_chart_set_type(g_chart, LV_CHART_TYPE_LINE);
  lv_chart_set_div_line_count(g_chart, 4, 0);
  lv_chart_set_point_count(g_chart, kMaxHourCards);
  lv_chart_set_update_mode(g_chart, LV_CHART_UPDATE_MODE_CIRCULAR);
  lv_obj_set_style_line_width(g_chart, 3, LV_PART_ITEMS);
  // 6px, not 5px - +10% per request (5*1.1 = 5.5, rounded to the nearest
  // whole pixel since lv_obj_set_style_size takes an int).
  lv_obj_set_style_size(g_chart, 6, 6, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(g_chart, lv_color_hex(kPaper), LV_PART_INDICATOR);
  lv_obj_set_style_border_color(g_chart, lv_color_hex(kBrass), LV_PART_INDICATOR);
  lv_obj_set_style_border_width(g_chart, 2, LV_PART_INDICATOR);
  g_tempSeries = lv_chart_add_series(g_chart, lv_color_hex(kBrass), LV_CHART_AXIS_PRIMARY_Y);

  // Overlaid, not a child of g_chart - lv_chart doesn't parent arbitrary
  // widgets over its plot area, so this is a same-size sibling row
  // positioned via lv_obj_align + per-label x offsets from
  // lv_chart_get_point_pos_by_id() (see refreshChartLabels()).
  lv_obj_t *labelLayer = lv_obj_create(section);
  lv_obj_set_size(labelLayer, LV_PCT(100), 14);
  lv_obj_set_style_bg_opa(labelLayer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(labelLayer, 0, 0);
  lv_obj_set_style_pad_all(labelLayer, 0, 0);
  lv_obj_clear_flag(labelLayer, LV_OBJ_FLAG_SCROLLABLE);
  for (size_t i = 0; i < kMaxHourCards; i++) {
    g_chartHourLabels[i] = makeLabel(labelLayer, kSub, &font_plexmono_hcardtime_14);
    lv_obj_set_pos(g_chartHourLabels[i], 0, 0);
    lv_obj_add_flag(g_chartHourLabels[i], LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_t *legend = makeRow(section);
  lv_obj_set_style_pad_top(legend, 8, 0);
  lv_obj_set_style_pad_column(legend, 18, 0);

  lv_obj_t *tempKey = makeRow(legend);
  lv_obj_set_style_pad_column(tempKey, 6, 0);
  lv_obj_set_flex_align(tempKey, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *tempSwatch = lv_obj_create(tempKey);
  lv_obj_set_size(tempSwatch, 10, 3);
  lv_obj_set_style_radius(tempSwatch, 2, 0);
  lv_obj_set_style_bg_color(tempSwatch, lv_color_hex(kBrass), 0);
  lv_obj_set_style_bg_opa(tempSwatch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(tempSwatch, 0, 0);
  lv_obj_clear_flag(tempSwatch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *legendTemp = makeLabel(tempKey, kSub, &font_plexmono_legend_14);
  lv_label_set_text(legendTemp, "Temperature");

  lv_obj_t *precipKey = makeRow(legend);
  lv_obj_set_style_pad_column(precipKey, 6, 0);
  lv_obj_set_flex_align(precipKey, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *precipSwatch = lv_obj_create(precipKey);
  lv_obj_set_size(precipSwatch, 10, 3);
  lv_obj_set_style_radius(precipSwatch, 2, 0);
  lv_obj_set_style_bg_color(precipSwatch, lv_color_hex(kTeal), 0);
  lv_obj_set_style_bg_opa(precipSwatch, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(precipSwatch, 0, 0);
  lv_obj_clear_flag(precipSwatch, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *legendPrecip = makeLabel(precipKey, kSub, &font_plexmono_legend_14);
  lv_label_set_text(legendPrecip, "Precipitation chance");

  return section;
}

// ---------------------------------------------------------------------
// Hour grid: 2 rows x 8 columns, guaranteed to fit via LV_GRID_FR(1)
// cells (same "no overflow regardless of content" pattern
// dashboard_ui.cpp's 7-day rows already established), not a scroll list
// (mockup's own stated on-device intent). Mockup: .hcard padding 10px
// 8px, icon 20x20, .now background highlight.
// ---------------------------------------------------------------------
HourCardWidgets buildHourCard(lv_obj_t *parent) {
  HourCardWidgets w;
  w.card = makeColumn(parent);
  lv_obj_set_size(w.card, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(w.card, lv_color_hex(kPanel), 0);
  lv_obj_set_style_bg_opa(w.card, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(w.card, 8, 0);
  lv_obj_set_style_pad_ver(w.card, 10, 0);
  // SPACE_BETWEEN, not START - keeps time/icon/temp grouped at the top
  // and pushes the precip % down to the card's own bottom edge, away
  // from that group, per request.
  lv_obj_set_flex_align(w.card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(w.card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *topGroup = makeColumn(w.card);
  lv_obj_set_style_pad_row(topGroup, 4, 0);
  lv_obj_set_flex_align(topGroup, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  w.time = makeLabel(topGroup, kSub, &font_plexmono_hcardtime_14);
  w.icon = lv_image_create(topGroup);
  // 40x40, matching the *_hcard icon bitmaps' own native render size
  // (dashboard_icons_manifest.txt) exactly - the previous 20x20 widget
  // size against 26px DayRow bitmaps let the image overflow its own
  // widget bounds asymmetrically, which is what actually read as
  // "off-center" (not a flex-align bug - cross-axis centering was
  // already correct, it was centering an undersized box).
  lv_obj_set_size(w.icon, 40, 40);
  w.temp = makeLabel(topGroup, kInk, &font_plexmono_hcardtemp_26);
  // Extra gap above the temperature specifically (on top of topGroup's
  // own 4px pad_row) - separates it from the icon above it, per request.
  lv_obj_set_style_pad_top(w.temp, 6, 0);

  w.pop = makeLabel(w.card, kTeal, &font_plexmono_hcardpop_18);

  return w;
}

lv_obj_t *buildHourGrid(lv_obj_t *parent) {
  lv_obj_t *section = makeColumn(parent);
  lv_obj_set_width(section, LV_PCT(100));
  lv_obj_set_height(section, LV_PCT(100));
  lv_obj_set_flex_grow(section, 1);
  lv_obj_set_style_pad_hor(section, 26, 0);
  lv_obj_set_style_pad_top(section, 4, 0);
  lv_obj_set_style_pad_bottom(section, 20, 0);
  lv_obj_set_style_border_side(section, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(section, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(section, 1, 0);

  static int32_t colDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                              LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                              LV_GRID_TEMPLATE_LAST};
  static int32_t rowDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static_assert(kMaxHourCards == 16, "colDsc/rowDsc above are hand-written for a 2x8 grid");
  lv_obj_t *grid = lv_obj_create(section);
  lv_obj_set_size(grid, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(grid, lv_color_hex(kLine), 0);
  lv_obj_set_style_bg_opa(grid, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(grid, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(grid, 1, 0);
  lv_obj_set_style_radius(grid, 8, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_gap(grid, 1, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_dsc_array(grid, colDsc, rowDsc);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  for (size_t i = 0; i < kMaxHourCards; i++) {
    g_hourCards[i] = buildHourCard(grid);
    const int32_t col = static_cast<int32_t>(i % 8);
    const int32_t row = static_cast<int32_t>(i / 8);
    lv_obj_set_grid_cell(g_hourCards[i].card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
  }

  return section;
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
  buildChartSection(g_screen);
  buildHourGrid(g_screen);

  lv_screen_load(g_screen);
}

void refreshChart(const SharedState::DashboardSnapshot &data) {
  float tMin = 1e9f;
  float tMax = -1e9f;
  for (size_t i = 0; i < data.hourlyCount; i++) {
    tMin = min(tMin, data.hourly[i].temperature);
    tMax = max(tMax, data.hourly[i].temperature);
  }
  if (data.hourlyCount == 0) {
    tMin = 0;
    tMax = 1;
  }
  // Matches the mockup's own `tMin = min(...) - 2, tMax = max(...) + 2`
  // padding so the line never touches the plot's top/bottom edge.
  lv_chart_set_axis_range(g_chart, LV_CHART_AXIS_PRIMARY_Y, static_cast<int32_t>(tMin - 2),
                           static_cast<int32_t>(tMax + 2));

  for (size_t i = 0; i < kMaxHourCards; i++) {
    if (i < data.hourlyCount) {
      lv_chart_set_next_value(g_chart, g_tempSeries, static_cast<int32_t>(data.hourly[i].temperature));
      // Matches the mockup's own `if (h.pop < 5) return;` - skips
      // drawing a bar at all for negligible chance, rather than a
      // barely-visible sliver.
      const int pop = data.hourly[i].precipitationProbabilityPercent;
      lv_chart_set_next_value(g_precipChart, g_precipSeries, pop < 5 ? LV_CHART_POINT_NONE : pop);
    } else {
      lv_chart_set_next_value(g_chart, g_tempSeries, LV_CHART_POINT_NONE);
      lv_chart_set_next_value(g_precipChart, g_precipSeries, LV_CHART_POINT_NONE);
    }
  }
  lv_chart_refresh(g_chart);
  lv_chart_refresh(g_precipChart);

  // Forces layout resolution before reading point positions below - same
  // "freshly built/updated widgets don't have real geometry until the
  // next lv_timer_handler() pass" issue dashboard_ui.cpp's day-row track
  // width hit once (see its own showDashboardScreen() comment).
  lv_obj_update_layout(g_chart);

  for (size_t i = 0; i < kMaxHourCards; i++) {
    lv_obj_t *label = g_chartHourLabels[i];
    // "NOW" (i==0) or every 3rd hour, matching the mockup's own
    // `i === 0 || i % 3 === 0` cadence - not shown past hourlyCount.
    if (i >= data.hourlyCount || !(i == 0 || i % 3 == 0)) {
      lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(label, i == 0 ? "NOW" : data.hourly[i].displayDateTime.c_str());

    lv_point_t pos;
    lv_chart_get_point_pos_by_id(g_chart, g_tempSeries, static_cast<uint32_t>(i), &pos);
    lv_obj_set_pos(label, pos.x - lv_obj_get_width(label) / 2, 0);
  }
}

}  // namespace

void showHourlyDetailScreen(const SharedState::DashboardSnapshot &data) {
  const bool firstBuild = (g_screen == nullptr);
  if (firstBuild) {
    buildScreen();
  }
  LOG_D("ui", "showHourlyDetailScreen called, firstBuild=%d screen=%p\n", firstBuild, static_cast<void *>(g_screen));

  lv_label_set_text(g_titleSub, data.displayLocation.c_str());

  refreshChart(data);

  for (size_t i = 0; i < kMaxHourCards; i++) {
    HourCardWidgets &w = g_hourCards[i];
    if (i >= data.hourlyCount) {
      lv_obj_add_flag(w.card, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(w.card, LV_OBJ_FLAG_HIDDEN);

    const HourlyForecastPoint &hour = data.hourly[i];
    const bool isNow = (i == 0);
    lv_obj_set_style_bg_color(w.card, lv_color_hex(isNow ? kPanel2 : kPanel), 0);
    lv_obj_set_style_text_color(w.time, lv_color_hex(isNow ? kBrassStrong : kSub), 0);
    lv_label_set_text(w.time, isNow ? "NOW" : hour.displayDateTime.c_str());

    lv_image_set_src(
        w.icon, iconForBucket(bucketForCondition(hour.condition.type.c_str(), hour.isDaytime), IconSize::HourCard));

    char tempBuf[8];
    snprintf(tempBuf, sizeof(tempBuf), "%d°", static_cast<int>(hour.temperature));
    lv_label_set_text(w.temp, tempBuf);

    char popBuf[8];
    snprintf(popBuf, sizeof(popBuf), "%d%%", hour.precipitationProbabilityPercent);
    lv_label_set_text(w.pop, popBuf);
  }
}

void refreshHourlyDetailClock(const SharedState::DashboardSnapshot &data) {
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

bool hourlyDetailScreenBuilt() { return g_screen != nullptr; }

lv_obj_t *hourlyDetailScreenObj() { return g_screen; }
