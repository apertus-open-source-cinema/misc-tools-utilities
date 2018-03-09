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

function [raw,f] = reverse_curves(f, P)

    % undo gamma and scale to 12-bit
    f = (f / 65536) .^ 2 * 4095;

    % switch to log
    f = log2(f);

    fr = f(:,:,1);
    fg = f(:,:,2);
    fb = f(:,:,3);

    % apply curve
    fr = 2 .^ (P(1,1) * fr.^2 + P(1,2) * fr + P(1,3));
    fg = 2 .^ (P(2,1) * fg.^2 + P(2,2) * fg + P(2,3));
    fb = 2 .^ (P(3,1) * fb.^2 + P(3,2) * fb + P(3,3));

    f(:,:,1) = fr;
    f(:,:,2) = fg;
    f(:,:,3) = fb;

    % bayer GBRG
    raw = zeros(size(fr)*2);
    raw(1:2:end,1:2:end) = fg;
    raw(1:2:end,2:2:end) = fb;
    raw(2:2:end,1:2:end) = fr;
    raw(2:2:end,2:2:end) = fg;
end
