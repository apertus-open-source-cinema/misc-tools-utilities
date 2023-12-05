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

function show_raw(im, black, white)
    r  = im(1:2:end,1:2:end);
    g1 = im(1:2:end,2:2:end);
    g2 = im(2:2:end,1:2:end);
    b  = im(2:2:end,2:2:end);
    g = (g1 + g2) / 2;

    show_raw_rgb(r, g, b, black, white);
end

function show_raw_rgb(r, g, b, black, white)
    r = r - black;
    g = g - black;
    b = b - black;
    white = white - black;
    m = log2(white);

    IM(:,:,1) = (log2(max(1,r*1.28)) / m).^2;
    IM(:,:,2) = (log2(max(1,g)) / m).^2;
    IM(:,:,3) = (log2(max(1,b*1.46)) / m).^2;
    imshow(IM,[]);
end
