#pragma once
#include "obd.h"

// Builds the dashboard on the active LVGL screen.
void ui_create();

// Refreshes all widgets from a data snapshot. Call ~20 times per second.
void ui_update(const ObdSnapshot &d);
