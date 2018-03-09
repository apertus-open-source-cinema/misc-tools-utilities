## Copyright (C) 2016 a1ex
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

function [E,expo] = darkframe_eval(filename)
    disp(filename)
    [im,exif] = read_raw(filename);
    im = im - exif.black_level;
    expo = exif.exposure;

    E = [median(im(:)) std(im(:)) std(median(im')) std(median(im))];
    printf(' - median   : %.2f\n', E(1));
    printf(' - stdev    : %.2f\n', E(2));
    printf(' - row noise: %.2f\n', E(3));
    printf(' - col noise: %.2f\n', E(4));
end
