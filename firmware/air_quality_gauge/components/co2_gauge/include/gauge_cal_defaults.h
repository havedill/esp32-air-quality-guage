#pragma once

/*
 * Fallback step calibration when NVS is empty (e.g. after full chip erase).
 * Update after re-calibrating on the dial, then run "cal save" on the device.
 *
 * 400 ppm  -> step -450
 * 2200 ppm -> step 1079
 * span 1529 steps
 */
#define GAUGE_CAL_STEP_MIN_DEFAULT  (-450)
#define GAUGE_CAL_STEP_MAX_DEFAULT  (1079)
