/**
 * The CMV12000 is affected by pattern noise - a scalar offset applied to
 * each row, and also to each column. The sensor datasheet refers to this
 * noise as "row noise", but it affects the columns as well.
 *
 * Unfortunately, the pattern is not constant, but varies from frame to frame,
 * so we can't call it "fixed-pattern noise" (even if it looks similar),
 * and we can't get rid of it completely with a dark frame (but it helps).
 *
 * Here, we will try to estimate the pattern noise from low-contrast areas
 * in the image (where this kind of noise is obvious).
 *
 * Copyright (C) 2015 a1ex
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "stdint.h"
#include "raw.h"

void fix_pattern_noise(struct raw_info * raw_info, int16_t * raw16, int row_noise_only, int debug_flags);
void highlight_blur(struct raw_info * raw_info, int16_t * raw);

/* with this one, you supply a denoised image as well */
void fix_pattern_noise_ex(struct raw_info * raw_info, int16_t * raw, int16_t * denoised, int row_noise_only, int debug_flags);

/* debug flags */
#define FIXPN_DBG_ROWNOISE  0
#define FIXPN_DBG_COLNOISE  1

#define FIXPN_DBG_DENOISED  2
#define FIXPN_DBG_NOISE     4
#define FIXPN_DBG_MASK      8
