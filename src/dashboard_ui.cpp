#include "dashboard_ui.h"

#include <lvgl.h>
#include <time.h>

#include "dashboard_fonts.h"

// The Dashboard screen (docs/mockups/dashboard.html), Phase 1: statusbar,
// current conditions + hi/lo + 4-metric grid, 7-day outlook, 8-hour strip.
// No weather icons yet (the mockup's own glyphs are static/non-condition-
// aware today anyway - see docs/mockups/dashboard.html's fixed SVGs), no
// tap-to-navigate into hourly-detail/daily-forecast (those screens don't
// exist yet), no Day/Night toggle (this kiosk always runs the dark
// palette, same as every other screen in this firmware).
//
// Every padding/gap/size below is the mockup's own raw px value, used
// as-is - not scaled. docs/mockups/dashboard.html's `.device` frame is
// already built at exactly 1280x720, the panel's own native resolution,
// so its CSS px are already real device px by construction. An earlier
// version of this file multiplied every value by ~2.9x on the theory
// that the panel's pixel density needed compensating for (the right call
// for the *status/provisioning* screens, whose sizes were picked
// freehand with no mockup reference - see docs/rendering.md) - wrong
// here, and confirmed wrong on hardware: the whole layout overflowed its
// 720px height badly (hero digit alone at 312px). See
// tools/dashboard_fonts_manifest.txt for the same correction on the font
// side.
//
// Unlike showStatusScreen() (boot_ui.cpp), which rebuilds from scratch on
// every call - fine for a rare, simple screen - this screen is built once
// (module-level static lv_obj_t* for everything that changes) and updated
// in place on every subsequent call, matching the mockup's own stated
// on-device goal ("redrawn only where data changed (partial DMA flush)").
// Same static-widget-pointer convention provisioning.cpp already uses for
// its net-card/step-list.
namespace {

// Dark-theme palette, docs/mockups/assets/style.css's --ink/--sub/etc.
// Only the dark values: this kiosk never runs the light theme.
constexpr uint32_t kInk = 0xF0E9D8;
constexpr uint32_t kSub = 0xA89E8C;
constexpr uint32_t kPaper = 0x121417;
constexpr uint32_t kPanel = 0x1B1E22;
constexpr uint32_t kLine = 0x2A2E33;  // Same solid approximation of --line
                                      // provisioning.cpp's net-card border
                                      // already uses.
constexpr uint32_t kBrass = 0xD99A4E;
constexpr uint32_t kBrassStrong = 0xE9B06C;
constexpr uint32_t kTeal = 0x6FB3AC;
constexpr uint32_t kEmber = 0xE2794E;

constexpr size_t kMaxDayRows = kMaxDailyPoints;
constexpr size_t kMaxHourCells = kMaxHourlyPoints;

struct DayRowWidgets {
  lv_obj_t *row = nullptr;
  lv_obj_t *name = nullptr;
  lv_obj_t *lo = nullptr;
  lv_obj_t *track = nullptr;
  lv_obj_t *fill = nullptr;
  lv_obj_t *hi = nullptr;
  lv_obj_t *pop = nullptr;
};

struct HourCellWidgets {
  lv_obj_t *cell = nullptr;
  lv_obj_t *t = nullptr;
  lv_obj_t *barWrap = nullptr;
  lv_obj_t *bar = nullptr;
  lv_obj_t *temp = nullptr;
  lv_obj_t *pop = nullptr;
};

lv_obj_t *g_screen = nullptr;
lv_obj_t *g_nameLabel = nullptr;
lv_obj_t *g_regionLabel = nullptr;
lv_obj_t *g_syncDot = nullptr;
lv_obj_t *g_syncLabel = nullptr;
lv_obj_t *g_clockLabel = nullptr;

lv_obj_t *g_tempLabel = nullptr;
lv_obj_t *g_degLabel = nullptr;
lv_obj_t *g_conditionLabel = nullptr;
lv_obj_t *g_feelsLabel = nullptr;
lv_obj_t *g_hiLabel = nullptr;
lv_obj_t *g_loLabel = nullptr;

// Humidity, Wind, UV Index, Precip, in that order (matches the mockup).
lv_obj_t *g_metricValueLabels[4] = {nullptr, nullptr, nullptr, nullptr};
lv_obj_t *g_metricSpanLabels[4] = {nullptr, nullptr, nullptr, nullptr};

DayRowWidgets g_dayRows[kMaxDayRows];
HourCellWidgets g_hourCells[kMaxHourCells];

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

// ---------------------------------------------------------------------
// Statusbar: location name/region (left), sync status + clock (right).
// Mockup: .statusbar { padding: 18px 30px 12px }, .loc gap 10, .right
// gap 22, .sync gap 7, dot 6px.
// ---------------------------------------------------------------------

lv_obj_t *buildStatusbar(lv_obj_t *parent) {
  lv_obj_t *bar = lv_obj_create(parent);
  lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_hor(bar, 30, 0);
  lv_obj_set_style_pad_top(bar, 18, 0);
  lv_obj_set_style_pad_bottom(bar, 12, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *loc = makeRow(bar);
  lv_obj_set_style_pad_column(loc, 10, 0);
  lv_obj_set_flex_align(loc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
  g_nameLabel = makeLabel(loc, kInk, &font_archivo_statusname_20);
  g_regionLabel = makeLabel(loc, kSub, &font_plexmono_region_20);

  lv_obj_t *right = makeRow(bar);
  lv_obj_set_style_pad_column(right, 22, 0);
  lv_obj_set_flex_align(right, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t *sync = makeRow(right);
  lv_obj_set_style_pad_column(sync, 7, 0);
  lv_obj_set_flex_align(sync, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  g_syncDot = lv_obj_create(sync);
  lv_obj_set_size(g_syncDot, 6, 6);
  lv_obj_set_style_radius(g_syncDot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_syncDot, lv_color_hex(kTeal), 0);
  lv_obj_set_style_bg_opa(g_syncDot, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_syncDot, 0, 0);
  lv_obj_clear_flag(g_syncDot, LV_OBJ_FLAG_SCROLLABLE);
  g_syncLabel = makeLabel(sync, kSub, &font_plexmono_statusright_13);

  g_clockLabel = makeLabel(right, kInk, &font_plexmono_clock_20);

  return bar;
}

// ---------------------------------------------------------------------
// Primary column: current reading + hi/lo + 4-metric grid.
// Mockup: .primary { padding: 22px 30px 18px }, .reading gap 2, .hilo
// gap 22 / margin-top 4, .metrics gap 1, .metric { padding: 12px 14px },
// value/span gap implicit (inline flex, no explicit gap in mockup).
// ---------------------------------------------------------------------

lv_obj_t *buildMetricCell(lv_obj_t *parent, const char *label, size_t index) {
  lv_obj_t *cell = makeColumn(parent);
  lv_obj_set_style_bg_color(cell, lv_color_hex(kPanel), 0);
  lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(cell, 18, 0);
  lv_obj_set_style_pad_ver(cell, 16, 0);
  lv_obj_set_style_pad_row(cell, 8, 0);
  lv_obj_set_width(cell, LV_PCT(100));

  makeLabel(cell, kSub, &font_plexmono_metriclabel_12);
  lv_label_set_text(lv_obj_get_child(cell, -1), label);

  lv_obj_t *valueRow = makeRow(cell);
  // Full cell width, not content-sized: END on the main axis only
  // right-aligns within whatever free space actually exists, and a
  // content-sized row has none.
  lv_obj_set_width(valueRow, LV_PCT(100));
  lv_obj_set_flex_align(valueRow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START);
  g_metricValueLabels[index] = makeLabel(valueRow, kInk, &font_plexmono_metricvalue_26);
  g_metricSpanLabels[index] = makeLabel(valueRow, kSub, &font_plexmono_metricspan_14);

  return cell;
}

lv_obj_t *buildPrimaryColumn(lv_obj_t *parent) {
  lv_obj_t *col = makeColumn(parent);
  lv_obj_set_height(col, LV_PCT(100));
  lv_obj_set_flex_grow(col, 155);
  lv_obj_set_flex_align(col, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_hor(col, 30, 0);
  lv_obj_set_style_pad_top(col, 22, 0);
  lv_obj_set_style_pad_bottom(col, 18, 0);
  lv_obj_set_style_border_side(col, LV_BORDER_SIDE_RIGHT, 0);
  lv_obj_set_style_border_color(col, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(col, 1, 0);

  lv_obj_t *top = makeColumn(col);
  lv_obj_set_style_pad_row(top, 2, 0);

  lv_obj_t *tempRow = makeRow(top);
  lv_obj_set_flex_align(tempRow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  g_tempLabel = makeLabel(tempRow, kInk, &font_plexmono_hero_108);
  g_degLabel = makeLabel(tempRow, kBrassStrong, &font_plexmono_deg_44);

  g_conditionLabel = makeLabel(top, kInk, &font_archivo_condition_19);
  g_feelsLabel = makeLabel(top, kSub, &font_plexmono_feels_13);

  lv_obj_t *hilo = makeRow(top);
  lv_obj_set_style_pad_column(hilo, 22, 0);
  lv_obj_set_style_pad_top(hilo, 4, 0);
  lv_obj_set_flex_align(hilo, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  g_hiLabel = makeLabel(hilo, kEmber, &font_plexmono_hilo_14);
  lv_obj_t *sep = makeLabel(hilo, kSub, &font_plexmono_hilo_14);
  lv_label_set_text(sep, "/");
  g_loLabel = makeLabel(hilo, kTeal, &font_plexmono_hilo_14);

  // 2x2, not the mockup's 1x4 - more room per cell within the same
  // overall width, which is what let the metric font sizes above grow.
  // Row height is LV_GRID_CONTENT (not FR(1)/stretch): the grid should
  // size to what its now-bigger cells actually need, with
  // buildPrimaryColumn()'s SPACE_BETWEEN taking up whatever's left
  // between it and the reading block above, not the other way around.
  static int32_t colDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
  static int32_t rowDsc[] = {LV_GRID_CONTENT, LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_t *metrics = lv_obj_create(col);
  lv_obj_set_size(metrics, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(metrics, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(metrics, 0, 0);
  lv_obj_set_style_pad_all(metrics, 0, 0);
  lv_obj_set_style_pad_column(metrics, 6, 0);
  lv_obj_set_style_pad_row(metrics, 6, 0);
  lv_obj_clear_flag(metrics, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_dsc_array(metrics, colDsc, rowDsc);
  lv_obj_set_layout(metrics, LV_LAYOUT_GRID);

  const char *labels[4] = {"HUMIDITY", "WIND", "UV INDEX", "PRECIP"};
  for (size_t i = 0; i < 4; i++) {
    lv_obj_t *cell = buildMetricCell(metrics, labels[i], i);
    const int32_t column = static_cast<int32_t>(i % 2);
    const int32_t row = static_cast<int32_t>(i / 2);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_STRETCH, column, 1, LV_GRID_ALIGN_STRETCH, row, 1);
  }

  return col;
}

// ---------------------------------------------------------------------
// Daily column: "7-Day Outlook" title + up to 7 rows + footer.
// Mockup: .daily { padding: 20px 26px 16px }, .day-row { grid-template-
// columns: 74px 26px 1fr auto; gap: 12px; padding: 9px 0 } - the 26px
// icon column is dropped (no icons this phase), .range gap 8, .footer
// padding-top 10.
// ---------------------------------------------------------------------

DayRowWidgets buildDayRow(lv_obj_t *parent, bool isFirst) {
  DayRowWidgets w;
  w.row = makeRow(parent);
  lv_obj_set_width(w.row, LV_PCT(100));
  lv_obj_set_style_pad_column(w.row, 12, 0);
  lv_obj_set_flex_align(w.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_ver(w.row, 9, 0);
  if (!isFirst) {
    lv_obj_set_style_border_side(w.row, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(w.row, lv_color_hex(kLine), 0);
    lv_obj_set_style_border_width(w.row, 1, 0);
  }

  w.name = makeLabel(w.row, kInk, &font_archivo_dayname_14);
  lv_obj_set_width(w.name, 74);

  lv_obj_t *range = makeRow(w.row);
  lv_obj_set_flex_grow(range, 1);
  lv_obj_set_style_pad_column(range, 8, 0);
  lv_obj_set_flex_align(range, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  w.lo = makeLabel(range, kTeal, &font_plexmono_dayrange_12);

  w.track = lv_obj_create(range);
  lv_obj_set_size(w.track, LV_PCT(100), 3);
  lv_obj_set_flex_grow(w.track, 1);
  lv_obj_set_style_bg_color(w.track, lv_color_hex(kLine), 0);
  lv_obj_set_style_bg_opa(w.track, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.track, 0, 0);
  lv_obj_set_style_radius(w.track, 3, 0);
  lv_obj_set_style_pad_all(w.track, 0, 0);
  lv_obj_clear_flag(w.track, LV_OBJ_FLAG_SCROLLABLE);

  w.fill = lv_obj_create(w.track);
  lv_obj_set_size(w.fill, 4, 3);
  lv_obj_set_style_bg_color(w.fill, lv_color_hex(kTeal), 0);
  lv_obj_set_style_bg_grad_color(w.fill, lv_color_hex(kEmber), 0);
  lv_obj_set_style_bg_grad_dir(w.fill, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_opa(w.fill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.fill, 0, 0);
  lv_obj_set_style_radius(w.fill, 3, 0);
  lv_obj_clear_flag(w.fill, LV_OBJ_FLAG_SCROLLABLE);

  w.hi = makeLabel(range, kEmber, &font_plexmono_dayrange_12);

  w.pop = makeLabel(w.row, kSub, &font_plexmono_dailypop_11);
  lv_obj_set_width(w.pop, 34);
  lv_obj_set_style_text_align(w.pop, LV_TEXT_ALIGN_RIGHT, 0);

  return w;
}

lv_obj_t *buildDailyColumn(lv_obj_t *parent) {
  lv_obj_t *col = makeColumn(parent);
  lv_obj_set_height(col, LV_PCT(100));
  lv_obj_set_flex_grow(col, 100);
  lv_obj_set_style_pad_hor(col, 26, 0);
  lv_obj_set_style_pad_top(col, 20, 0);
  lv_obj_set_style_pad_bottom(col, 16, 0);

  lv_obj_t *title = makeLabel(col, kSub, &font_plexmono_title_11);
  lv_label_set_text(title, "7-DAY OUTLOOK");
  lv_obj_set_style_pad_bottom(title, 10, 0);

  lv_obj_t *rows = makeColumn(col);
  lv_obj_set_width(rows, LV_PCT(100));
  lv_obj_set_flex_grow(rows, 1);
  for (size_t i = 0; i < kMaxDayRows; i++) {
    g_dayRows[i] = buildDayRow(rows, i == 0);
  }

  lv_obj_t *footer = makeRow(col);
  lv_obj_set_width(footer, LV_PCT(100));
  lv_obj_set_style_pad_top(footer, 10, 0);
  lv_obj_set_flex_align(footer, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *source = makeLabel(footer, kSub, &font_plexmono_title_11);
  lv_label_set_text(source, "weather.googleapis.com");
  lv_obj_t *version = makeLabel(footer, kSub, &font_plexmono_title_11);
  lv_label_set_text(version, "v1 · forecast.days");

  return col;
}

// ---------------------------------------------------------------------
// Hourly strip: title + up to 8 bar-chart columns.
// Mockup: .hourly { padding: 14px 30px 16px }, .head margin-bottom 8,
// .hourly-row gap 4, .hour gap 6 / padding-top 6, .bar-wrap height 46,
// .bar width 4.
// ---------------------------------------------------------------------

constexpr int32_t kBarWrapHeight = 46;
constexpr int32_t kBarWidth = 4;

HourCellWidgets buildHourCell(lv_obj_t *parent) {
  HourCellWidgets w;
  w.cell = makeColumn(parent);
  lv_obj_set_flex_align(w.cell, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(w.cell, 6, 0);
  lv_obj_set_style_pad_top(w.cell, 6, 0);

  w.t = makeLabel(w.cell, kSub, &font_plexmono_title_11);

  w.barWrap = lv_obj_create(w.cell);
  lv_obj_set_size(w.barWrap, kBarWidth, kBarWrapHeight);
  lv_obj_set_style_bg_opa(w.barWrap, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(w.barWrap, 0, 0);
  lv_obj_set_style_pad_all(w.barWrap, 0, 0);
  lv_obj_clear_flag(w.barWrap, LV_OBJ_FLAG_SCROLLABLE);

  w.bar = lv_obj_create(w.barWrap);
  lv_obj_set_size(w.bar, kBarWidth, 1);
  lv_obj_align(w.bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(w.bar, lv_color_hex(kBrass), 0);
  lv_obj_set_style_bg_opa(w.bar, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(w.bar, 0, 0);
  lv_obj_set_style_radius(w.bar, 2, 0);
  lv_obj_clear_flag(w.bar, LV_OBJ_FLAG_SCROLLABLE);

  w.temp = makeLabel(w.cell, kInk, &font_plexmono_hourtemp_13);
  w.pop = makeLabel(w.cell, kTeal, &font_plexmono_hourpop_10);

  return w;
}

lv_obj_t *buildHourlyRow(lv_obj_t *parent) {
  lv_obj_t *section = makeColumn(parent);
  lv_obj_set_width(section, LV_PCT(100));
  lv_obj_set_style_pad_hor(section, 30, 0);
  lv_obj_set_style_pad_top(section, 14, 0);
  lv_obj_set_style_pad_bottom(section, 16, 0);
  lv_obj_set_style_border_side(section, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(section, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(section, 1, 0);

  lv_obj_t *title = makeLabel(section, kSub, &font_plexmono_title_11);
  lv_label_set_text(title, "HOURLY · NEXT 8 HOURS");
  lv_obj_set_style_pad_bottom(title, 8, 0);

  static int32_t colDsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                              LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                              LV_GRID_TEMPLATE_LAST};
  static int32_t rowDsc[] = {LV_GRID_CONTENT, LV_GRID_TEMPLATE_LAST};
  lv_obj_t *grid = lv_obj_create(section);
  lv_obj_set_size(grid, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_column(grid, 4, 0);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_grid_dsc_array(grid, colDsc, rowDsc);
  lv_obj_set_layout(grid, LV_LAYOUT_GRID);

  for (size_t i = 0; i < kMaxHourCells; i++) {
    g_hourCells[i] = buildHourCell(grid);
    lv_obj_set_grid_cell(g_hourCells[i].cell, LV_GRID_ALIGN_STRETCH, static_cast<int32_t>(i), 1, LV_GRID_ALIGN_STRETCH,
                          0, 1);
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

  lv_obj_t *mainRow = lv_obj_create(g_screen);
  lv_obj_set_width(mainRow, LV_PCT(100));
  // Grows to fill whatever's left between the statusbar and the hourly
  // strip below (both content-sized), not LV_SIZE_CONTENT - the
  // primary/daily columns inside size themselves to LV_PCT(100) of
  // *this* row's height, which is circular (and resolves to ~zero) if
  // this row is itself only as tall as its content. Confirmed on
  // hardware: without this, the row collapsed to just its own 1px
  // border, and the entire primary/daily section silently disappeared.
  lv_obj_set_flex_grow(mainRow, 1);
  lv_obj_set_style_bg_opa(mainRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_side(mainRow, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(mainRow, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_width(mainRow, 1, 0);
  lv_obj_set_style_pad_all(mainRow, 0, 0);
  lv_obj_set_flex_flow(mainRow, LV_FLEX_FLOW_ROW);
  lv_obj_clear_flag(mainRow, LV_OBJ_FLAG_SCROLLABLE);

  buildPrimaryColumn(mainRow);
  buildDailyColumn(mainRow);

  buildHourlyRow(g_screen);

  lv_screen_load(g_screen);
}

// currentTime is a full RFC3339 timestamp (e.g.
// "2024-05-13T21:53:24.752338756Z") - just what's needed for "AS OF
// HH:MM" is the 5 characters right after 'T', no full datetime parse.
String extractHhMm(const String &rfc3339) {
  int t = rfc3339.indexOf('T');
  if (t < 0 || rfc3339.length() < static_cast<unsigned>(t) + 6) {
    return "--:--";
  }
  return rfc3339.substring(t + 1, t + 6);
}

// Weather API's wind.direction.cardinal is one of the 16 compass points
// spelled out in full (e.g. "EAST_NORTHEAST"), not an abbreviation - too
// long for the metric grid's width. Maps to the standard 1-3 letter
// abbreviation; falls back to the raw value (better than blank) if
// Google ever adds a point this table doesn't have yet.
const char *abbreviateCardinal(const String &cardinal) {
  struct Entry {
    const char *full;
    const char *abbr;
  };
  static const Entry kEntries[] = {
      {"NORTH", "N"},           {"NORTH_NORTHEAST", "NNE"}, {"NORTHEAST", "NE"}, {"EAST_NORTHEAST", "ENE"},
      {"EAST", "E"},            {"EAST_SOUTHEAST", "ESE"},  {"SOUTHEAST", "SE"}, {"SOUTH_SOUTHEAST", "SSE"},
      {"SOUTH", "S"},           {"SOUTH_SOUTHWEST", "SSW"}, {"SOUTHWEST", "SW"}, {"WEST_SOUTHWEST", "WSW"},
      {"WEST", "W"},            {"WEST_NORTHWEST", "WNW"},  {"NORTHWEST", "NW"}, {"NORTH_NORTHWEST", "NNW"},
  };
  for (const Entry &entry : kEntries) {
    if (cardinal == entry.full) {
      return entry.abbr;
    }
  }
  return cardinal.c_str();
}

// Shared 3-bucket "value -> instrument-panel accent" mapping for metrics
// with a genuine ascending risk/severity gradient (UV index, precip
// chance) - not applied to every metric: humidity's "notable at both
// extremes, fine in the middle" shape doesn't fit an ascending scale,
// and typical wind speeds rarely reach anything worth flagging, so
// those stay neutral (kSub/kInk) rather than forcing this pattern onto
// values it doesn't describe well.
uint32_t severityColor(float value, float moderateThreshold, float highThreshold) {
  return value >= highThreshold ? kEmber : value >= moderateThreshold ? kBrass : kTeal;
}

}  // namespace

void showDashboardScreen(const SharedState::DashboardSnapshot &data) {
  if (g_screen == nullptr) {
    buildScreen();
  }

  // Location text is fixed once resolved (never changes mid-session) -
  // setting it on every call is wasted work, but cheap enough (two
  // label-text-set calls) that a separate "first call only" path isn't
  // worth the extra state. displayLocation is the resolved "City, ST"
  // when available, already computed in main.cpp's
  // fetchDashboardSnapshot() - this file doesn't need to know it might
  // instead be a raw ZIP code some of the time.
  lv_label_set_text(g_nameLabel, data.displayLocation.c_str());
  lv_label_set_text(g_regionLabel, "GOOGLE WEATHER");

  const char *degText = data.unitsImperial ? "°F" : "°C";

  const CurrentConditions &cur = data.current;

  char tempBuf[8];
  snprintf(tempBuf, sizeof(tempBuf), "%d", static_cast<int>(cur.temperature));
  lv_label_set_text(g_tempLabel, tempBuf);
  lv_label_set_text(g_degLabel, degText);

  lv_label_set_text(g_conditionLabel, cur.condition.description.c_str());

  char feelsBuf[64];
  snprintf(feelsBuf, sizeof(feelsBuf), "FEELS LIKE %d° · AS OF %s", static_cast<int>(cur.feelsLikeTemperature),
            extractHhMm(cur.currentTime).c_str());
  lv_label_set_text(g_feelsLabel, feelsBuf);

  char hiBuf[24];
  char loBuf[24];
  float hiTemp = cur.temperature;
  float loTemp = cur.temperature;
  if (data.dailyCount > 0) {
    hiTemp = data.daily[0].maxTemperature;
    loTemp = data.daily[0].minTemperature;
  }
  snprintf(hiBuf, sizeof(hiBuf), "↑%d° HIGH", static_cast<int>(hiTemp));
  snprintf(loBuf, sizeof(loBuf), "↓%d° LOW", static_cast<int>(loTemp));
  lv_label_set_text(g_hiLabel, hiBuf);
  lv_label_set_text(g_loLabel, loBuf);

  char humidityBuf[8];
  snprintf(humidityBuf, sizeof(humidityBuf), "%d", cur.relativeHumidity);
  lv_label_set_text(g_metricValueLabels[0], humidityBuf);
  lv_label_set_text(g_metricSpanLabels[0], "%");

  char windBuf[8];
  snprintf(windBuf, sizeof(windBuf), "%d", static_cast<int>(cur.windSpeed));
  lv_label_set_text(g_metricValueLabels[1], windBuf);
  char windSpanBuf[24];
  snprintf(windSpanBuf, sizeof(windSpanBuf), " mph %s", abbreviateCardinal(cur.windDirectionCardinal));
  lv_label_set_text(g_metricSpanLabels[1], windSpanBuf);

  char uvBuf[8];
  snprintf(uvBuf, sizeof(uvBuf), "%d", static_cast<int>(cur.uvIndex));
  lv_label_set_text(g_metricValueLabels[2], uvBuf);
  const char *uvWord = cur.uvIndex >= 8   ? "very high"
                       : cur.uvIndex >= 6 ? "high"
                       : cur.uvIndex >= 3 ? "moderate"
                                          : "low";
  lv_label_set_text(g_metricSpanLabels[2], uvWord);
  // UV and precip get colored (both a real ascending risk gradient) -
  // reuses the existing instrument-panel accents rather than
  // introducing a new hazard-scale palette, same reasoning as hi/lo
  // temperature already sharing these two colors. Humidity/wind stay
  // neutral - see severityColor()'s comment for why. High/very high (UV)
  // and >=50%/moderate (precip) share ember/brass respectively (only
  // two colors past "low" to work with); the word/number still
  // distinguishes the two ends where it matters (UV's word text).
  const uint32_t uvColor = severityColor(cur.uvIndex, 3, 6);
  lv_obj_set_style_text_color(g_metricValueLabels[2], lv_color_hex(uvColor), 0);
  lv_obj_set_style_text_color(g_metricSpanLabels[2], lv_color_hex(uvColor), 0);

  char precipBuf[8];
  snprintf(precipBuf, sizeof(precipBuf), "%d", cur.precipitationProbabilityPercent);
  lv_label_set_text(g_metricValueLabels[3], precipBuf);
  lv_label_set_text(g_metricSpanLabels[3], "%");
  const uint32_t precipColor = severityColor(static_cast<float>(cur.precipitationProbabilityPercent), 20, 50);
  lv_obj_set_style_text_color(g_metricValueLabels[3], lv_color_hex(precipColor), 0);
  lv_obj_set_style_text_color(g_metricSpanLabels[3], lv_color_hex(precipColor), 0);

  // Daily rows: render up to what actually came back (<= kMaxDailyPoints),
  // hide the rest rather than showing empty rows.
  float dayMin = 1e9f;
  float dayMax = -1e9f;
  for (size_t i = 0; i < data.dailyCount; i++) {
    dayMin = min(dayMin, data.daily[i].minTemperature);
    dayMax = max(dayMax, data.daily[i].maxTemperature);
  }
  const float dayRange = (dayMax > dayMin) ? (dayMax - dayMin) : 1.0f;

  for (size_t i = 0; i < kMaxDayRows; i++) {
    DayRowWidgets &w = g_dayRows[i];
    if (i >= data.dailyCount) {
      lv_obj_add_flag(w.row, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(w.row, LV_OBJ_FLAG_HIDDEN);

    const DailyForecastPoint &day = data.daily[i];
    // displayDate is "YYYY-MM-DD" (see weather.cpp) - Phase 1 just shows
    // "Today" for index 0 and the raw date for the rest (day-of-week
    // formatting needs a date library this project doesn't have yet).
    if (i == 0) {
      lv_label_set_text(w.name, "Today");
      lv_obj_set_style_text_color(w.name, lv_color_hex(kBrassStrong), 0);
    } else {
      lv_label_set_text(w.name, day.displayDate.c_str());
      lv_obj_set_style_text_color(w.name, lv_color_hex(kInk), 0);
    }

    char loBuf2[8];
    char hiBuf2[8];
    snprintf(loBuf2, sizeof(loBuf2), "%d°", static_cast<int>(day.minTemperature));
    snprintf(hiBuf2, sizeof(hiBuf2), "%d°", static_cast<int>(day.maxTemperature));
    lv_label_set_text(w.lo, loBuf2);
    lv_label_set_text(w.hi, hiBuf2);

    const int32_t trackWidth = lv_obj_get_width(w.track);
    if (trackWidth > 0) {
      const int32_t left =
          static_cast<int32_t>(((day.minTemperature - dayMin) / dayRange) * static_cast<float>(trackWidth));
      const int32_t width = static_cast<int32_t>(
          ((day.maxTemperature - day.minTemperature) / dayRange) * static_cast<float>(trackWidth));
      lv_obj_set_x(w.fill, left);
      lv_obj_set_width(w.fill, width < 3 ? 3 : width);
    }

    char popBuf[8];
    snprintf(popBuf, sizeof(popBuf), "%d%%", day.precipitationProbabilityPercent);
    lv_label_set_text(w.pop, popBuf);
  }

  // Hourly bars: same min/max-normalize-to-height approach as the mockup's
  // own JS.
  float hourMin = 1e9f;
  float hourMax = -1e9f;
  for (size_t i = 0; i < data.hourlyCount; i++) {
    hourMin = min(hourMin, data.hourly[i].temperature);
    hourMax = max(hourMax, data.hourly[i].temperature);
  }
  const float hourRange = (hourMax > hourMin) ? (hourMax - hourMin) : 1.0f;

  for (size_t i = 0; i < kMaxHourCells; i++) {
    HourCellWidgets &w = g_hourCells[i];
    if (i >= data.hourlyCount) {
      lv_obj_add_flag(w.cell, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(w.cell, LV_OBJ_FLAG_HIDDEN);

    const HourlyForecastPoint &hour = data.hourly[i];
    lv_label_set_text(w.t, i == 0 ? "NOW" : hour.displayDateTime.c_str());

    const float pct = 0.18f + ((hour.temperature - hourMin) / hourRange) * 0.82f;
    const int32_t barHeight = static_cast<int32_t>(pct * static_cast<float>(kBarWrapHeight));
    lv_obj_set_height(w.bar, barHeight < 1 ? 1 : barHeight);
    lv_obj_set_style_bg_color(w.bar, lv_color_hex(i == 0 ? kBrassStrong : kBrass), 0);

    char hourTempBuf[8];
    snprintf(hourTempBuf, sizeof(hourTempBuf), "%d°", static_cast<int>(hour.temperature));
    lv_label_set_text(w.temp, hourTempBuf);

    if (hour.precipitationProbabilityPercent >= 20) {
      char hourPopBuf[8];
      snprintf(hourPopBuf, sizeof(hourPopBuf), "%d%%", hour.precipitationProbabilityPercent);
      lv_label_set_text(w.pop, hourPopBuf);
    } else {
      lv_label_set_text(w.pop, "");
    }
  }

  refreshDashboardClock(data);
}

void refreshDashboardClock(const SharedState::DashboardSnapshot &data) {
  if (g_screen == nullptr) {
    return;
  }

  const time_t now = time(nullptr);

  // data.utcOffsetSec (raw + DST, from the Time Zone API - see
  // timezone_lookup.h) is added to the UTC epoch here rather than via the
  // system TZ/NTP config (setenv("TZ",...)/configTime()): the offset can
  // arrive well after boot (it depends on a network lookup that runs
  // after WiFi/geocoding), and re-touching NTP config after the fact
  // risks a disruptive resync of an already-correct clock for no
  // benefit. gmtime_r() on a pre-shifted epoch gives correct wall-clock
  // fields without any of that - defaults to UTC (offset 0) until
  // resolveTimezoneIfNeeded() (main.cpp) succeeds, same as before this
  // existed.
  const time_t localNow = now + data.utcOffsetSec;
  struct tm tmInfo;
  gmtime_r(&localNow, &tmInfo);
  char clockBuf[6];
  snprintf(clockBuf, sizeof(clockBuf), "%02d:%02d", tmInfo.tm_hour, tmInfo.tm_min);
  lv_label_set_text(g_clockLabel, clockBuf);

  char syncBuf[32];
  if (data.syncedAtUnix <= 0) {
    snprintf(syncBuf, sizeof(syncBuf), "Not yet synced");
  } else {
    // Both sides are UTC (unshifted) - a duration, so the offset would
    // cancel out anyway, but using `now` here instead of `localNow` keeps
    // that obviously true rather than relying on the cancellation.
    const long ageSec = static_cast<long>(now - data.syncedAtUnix);
    const long ageMin = ageSec / 60;
    if (ageMin < 1) {
      snprintf(syncBuf, sizeof(syncBuf), "Synced just now");
    } else {
      snprintf(syncBuf, sizeof(syncBuf), "Synced %ldm ago", ageMin);
    }
  }
  lv_label_set_text(g_syncLabel, syncBuf);
}
