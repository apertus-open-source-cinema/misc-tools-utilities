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

function [rr,rg1,rg2,rb] = decode_gamma(R1,G1,B1,R2,G2,B2,offset)
    % reverse gamma curve
    gamma = log2((0:4095) + offset);
    gamma_range = gamma(end) - gamma(1);
    ginv = round(2 .^ ((0:255) * gamma_range / 255 + gamma(1)) - offset);
    ginv2 = round(2 .^ ((0:511) * gamma_range / 511 + gamma(1)) - offset);

    % recover RAW data from RGB
    % note: we have some redundancy for R and B
    rr = double(R1) + double(R2);
    rb = double(B1) + double(B2);
    rg1 = G1;
    rg2 = G2;

    % apply reverse gamma curve
    rr  = ginv2(rr + 1);
    rb  = ginv2(rb + 1);
    rg1 = ginv(rg1 + 1);
    rg2 = ginv(rg2 + 1);
end
