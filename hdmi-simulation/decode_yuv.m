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

function [r,g1,g2,b] = decode_yuv(Y1,U1,V1,Y2,U2,V2,offset)
    % reverse gamma curve
    gamma = log2((0:4095) + offset);
    gamma_range = gamma(end) - gamma(1);
    yuv_scaling = [zeros(1,16) (0:219) zeros(1,20)+219];
    ginv = round(2 .^ (yuv_scaling * gamma_range / 219 + gamma(1)) - offset);

    Y1 = double(Y1);
    U1 = double(U1);
    V1 = double(V1);
    Y2 = double(Y2);
    U2 = double(U2);
    V2 = double(V2);

    % U is mapped to b - g1 => b = U + g1
    % V is mapped to r - g1 => r = V + g1
    b1 = Y1 + (U1 - 128) * 2;
    r1 = Y1 + (V1 - 128) * 2;
    b2 = Y2 + (U2 - 128) * 2;
    r2 = Y2 + (V2 - 128) * 2;
    r = double(uint8((r1 + r2) / 2));
    b = double(uint8((b1 + b2) / 2));
    r  = ginv(r + 1);
    b  = ginv(b + 1);

    % green is mapped to Y, with gamma curve
    g1 = ginv(Y1 + 1);
    g2 = ginv(Y2 + 1);
end
