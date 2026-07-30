// LVGL + M5GFX display/touch bring-up and pump loop. See docs/rendering.md
// for the flush-path/rotation/color-format decisions this encodes.
#pragma once

// Rotates the panel into landscape, brings up LVGL against M5GFX
// (partial, PSRAM-backed draw buffers; see docs/rendering.md), and wires
// the touch input device. Call once, before any LVGL calls.
void initDisplay();

// Advances LVGL's tick and processes its timers; call frequently — every
// loop() iteration and inside any blocking wait loop — to keep touch/redraw
// responsive.
void pumpLvgl();
