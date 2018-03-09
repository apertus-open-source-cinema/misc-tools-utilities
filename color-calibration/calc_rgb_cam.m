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

function [rgb_cam, pre_mul, rgb_cam_n] = calc_rgb_cam(cam_xyz)
    % notations from dcraw
    xyz_rgb = [0.412453, 0.357580, 0.180423; 0.212671, 0.715160, 0.072169; 0.019334, 0.119193, 0.950227];
    cam_rgb = cam_xyz * xyz_rgb;
    pre_mul = cam_rgb * [1;1;1];
    cam_rgb_n = cam_rgb .* (1 ./ (pre_mul * [1 1 1]));
    rgb_cam_n = inv(cam_rgb_n);
    rgb_cam = inv(cam_rgb);
end
