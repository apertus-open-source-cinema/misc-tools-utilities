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

function [r,g1,g2,b] = decode_bits(R1,G1,B1,R2,G2,B2, bit_order)
    % Recover (decode) Bayer raw data (r,g1,g2,b) from two HDMI frames:
    % (R1,G1,B1) and (R2,G2,B2), according to bit_order.
    %
    % See encode for arguments.

    R1 = uint64(R1); G1 = uint64(G1); B1 = uint64(B1);
    R2 = uint64(R2); G2 = uint64(G2); B2 = uint64(B2);

    raw = uint64(zeros(size(R1)));
    rgb2 = raw;
    rgb2 = bitor(rgb2, bitshift(R1,0));
    rgb2 = bitor(rgb2, bitshift(G1,8));
    rgb2 = bitor(rgb2, bitshift(B1,16));
    rgb2 = bitor(rgb2, bitshift(R2,24));
    rgb2 = bitor(rgb2, bitshift(G2,32));
    rgb2 = bitor(rgb2, bitshift(B2,40));

    for i = 0:47
        if any(double(bitand(raw, bitshift(1, bit_order(i+1)))))
            disp([rgb2_bit_name(i) ' -> ' raw_bit_name(bit_order(i+1)) ' redundant, skipping']);
            continue
        end
        raw = bitor(raw, bitshift(bit(rgb2,i), bit_order(i+1)));
    end

    r  = bitand(bitshift(raw,   0), 4095);
    g1 = bitand(bitshift(raw, -12), 4095);
    g2 = bitand(bitshift(raw, -24), 4095);
    b  = bitand(bitshift(raw, -36), 4095);

    r  = double(r);
    g1 = double(g1);
    g2 = double(g2);
    b  = double(b);
end
