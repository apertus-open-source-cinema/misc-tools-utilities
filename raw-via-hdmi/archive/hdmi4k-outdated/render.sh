#!/bin/sh
#
# Convert Shogun MOV footage recorded with AXIOM Beta to DNG
#
# Copyright (C) 2016 a1ex
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Input should be a .mov file transcoded from Shogun with ffmpeg -vcodec copy (not directly!)
# that's because I've designed the HDMI recovery filters on transcoded output
# which is - surprise! - not identical (yes, with -vcodec copy; ffmpeg bug?)

# You need to get calibration frames (darkframe-hdmi-[AB].ppm) from:
# http://files.apertus.org/AXIOM-Beta/snapshots/calibration-frames/cam2/

# careful here
rm frame*.dng

# this reads the MOV file via ffmpeg
# the output is piped to raw2dng, which also does temporal row noise correction
# lower the black level because of the "black hole" sensor behavior
hdmi4k $* - | raw2dng --pgm --fixrnt --black=120 frame%05d.dng

# todo: HDMI dark frame averaging script
