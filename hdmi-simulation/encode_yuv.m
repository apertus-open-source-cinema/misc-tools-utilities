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

function [Y1,U1,V1,Y2,U2,V2] = encode_yuv(r,g1,g2,b,offset)
    % gamma curve from 12 to 8 bits
    gamma = log2((0:4095) + offset);
    gamma = gamma - gamma(1);
    gamma = round(gamma * 219 / gamma(end)) + 16;

    % apply gamma curve
    r  = gamma(r+1);
    g1 = gamma(g1+1);
    g2 = gamma(g2+1);
    b  = gamma(b+1);

    % map to YUV
    Y1 = uint8(g1);
    Y2 = uint8(g2);

    % channel values from 16 to 235 => difference can be from -219 to 219
    U1 = uint8((b - g1) / 2 + 128);
    V1 = uint8((r - g1) / 2 + 128);
    U2 = uint8((b - g2) / 2 + 128);
    V2 = uint8((r - g2) / 2 + 128);
end
