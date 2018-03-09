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

function pre_mul_apertus = match_gray_point(wb_nikon, wb_apertus)
    % Nikon ColorMatrix2 for D65:
    % Color Matrix 2                  : 0.7866 -0.2108 -0.0555 -0.4869 1.2483 0.2681 -0.1176 0.2069 0.7501
    cam_xyz_nikon = [0.7866 -0.2108 -0.0555; -0.4869 1.2483 0.2681; -0.1176 0.2069 0.7501];
    [rgb_cam_nikon, pre_mul_nikon, rgb_cam_n_nikon] = calc_rgb_cam(cam_xyz_nikon);

    % find pre_mul_apertus so that WB matches with Nikon
    % assume wb_nikon .* pre_mul_nikon == wb_apertus .* pre_mul_apertus
    % proof: exercise to the reader (or: I don't know how to prove it, but it works)
    pre_mul_apertus = wb_nikon(:) .* pre_mul_nikon(:) ./ wb_apertus(:);
    pre_mul_apertus = pre_mul_apertus * norm(pre_mul_nikon) / norm(pre_mul_apertus);
end
