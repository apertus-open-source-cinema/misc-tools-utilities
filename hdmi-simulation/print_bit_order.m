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

function print_bit_order(bit_order)
    print_byte(bit_order(1:8),   'R1');
    print_byte(bit_order(9:16),  'G1');
    print_byte(bit_order(17:24), 'B1');
    print_byte(bit_order(25:32), 'R2');
    print_byte(bit_order(33:40), 'G2');
    print_byte(bit_order(41:48), 'B2');
end

function print_byte(bits, name)
    assert(length(bits) == 8)
    out = [name ': '];
    for i = 8:-1:1
        out = [out raw_bit_name(bits(i))];
    end
    disp(out)
end
