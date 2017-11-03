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

function n = raw_bit_name(x)
    names = {'r', 'g1', 'g2', 'b'};
    if x == 48
        n = '   0   ';
        return;
    end
    if x == 49
        n = '   1   ';
        return;
    end
    n = sprintf('%2s%-4s ', names{1+floor(x/12)}, ['(' num2str(mod(x,12)) ')']);
end
