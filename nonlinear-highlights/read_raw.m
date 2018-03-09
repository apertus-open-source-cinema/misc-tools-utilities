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

function im = read_raw(filename, also_ob)
    if nargin < 2,
        also_ob = 0;
    end
    if also_ob
        dcraw = 'dcraw -c -4 -E';
    else
        dcraw = 'dcraw -c -4 -D';
    end
    system(sprintf('%s "%s" > tmp.pgm', dcraw, filename));
    im = double(imread('tmp.pgm'));
    system('rm tmp.pgm');
end
