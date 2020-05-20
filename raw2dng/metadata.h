#ifndef _metadata_h_
#define _metadata_h_

/*
 * Copyright (C) 2015 a1ex, irieger
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

void metadata_clear();
void metadata_extract(uint16_t registers[128]);
void metadata_dump_registers(uint16_t registers[128]);

int metadata_get_gain(uint16_t registers[128]);
int metadata_get_dark_offset(uint16_t registers[128]);
double metadata_get_exposure(uint16_t registers[128]);
int metadata_get_ystart(uint16_t registers[128]);
int metadata_get_ysize(uint16_t registers[128]);
int metadata_get_black_col(uint16_t registers[128]);

#endif
