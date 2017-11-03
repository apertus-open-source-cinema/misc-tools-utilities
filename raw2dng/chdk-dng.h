#ifndef __CHDK_DNG_H_
#define __CHDK_DNG_H_

/*
 * Original code copyright (C) CHDK (GPLv2); ported to Magic Lantern (2013)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

void dng_set_framerate(int fpsx1000);
void dng_set_thumbnail_size(int width, int height);

void dng_set_framerate_rational(int nom, int denom);
void dng_set_shutter(int nom, int denom);
void dng_set_aperture(int nom, int denom);
void dng_set_camname(char *str);
void dng_set_camserial(char *str);
void dng_set_description(char *str);
void dng_set_lensmodel(char *str);
void dng_set_focal(int nom, int denom);
void dng_set_iso(int value);
void dng_set_wbgain(int gain_r_n, int gain_r_d, int gain_g_n, int gain_g_d, int gain_b_n, int gain_b_d);
void dng_set_datetime(char *datetime, char *subsectime);

#endif // __CHDK_DNG_H_
