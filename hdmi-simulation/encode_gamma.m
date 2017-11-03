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

function [R1,G1,B1,R2,G2,B2] = encode_gamma(r,g1,g2,b,offset)
    % gamma curve from 12 to 8 bits
    gamma = log2((0:4095) + offset);
    gamma = gamma - gamma(1);
    gamma = round(gamma * 255 / gamma(end));

    % apply gamma curve
    r  = gamma(r+1);
    g1 = gamma(g1+1);
    g2 = gamma(g2+1);
    b  = gamma(b+1);

    % copy to output; R and B are redundant,
    % G1 and G2 are used to improve resolution
    R1 = r;
    G1 = g1;
    B1 = b;
    R2 = r;
    G2 = g2;
    B2 = b;
end
