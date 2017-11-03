## Main script for performing color calibration.
##
## Copyright (C) 2015 a1ex
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 3 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##
## SPDX-License-Identifier: GPL-3.0-or-later

% You will need a test image taken with the Apertus camera,
% and a reference image taken with another camera (e.g. Nikon).
raw_nikon = 'CC3.dng';
raw_apertus = 'CC3.DNG';

% Octave setup
more off
pkg load image

% First, pick some points on a color chart.
% This interactive step is required only once; the results are saved to file.
% That means, you may just comment it out, to make it easier to debug the next step.
pick_points(raw_nikon, raw_apertus);

% Next, match the colors from the picked points and compute a color matrix.
match_images(raw_nikon, raw_apertus);

% That's it, folks!
