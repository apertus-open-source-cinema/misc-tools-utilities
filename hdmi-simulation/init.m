## HDMI simulation
## Loads test data into the workspace
##
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

% Octave setup
more off
pkg load image

disp('Loading images...')

% For sample images, one needs to make sure:
% - their Bayer pattern is [RG;GB] (offset by one line or column if needed)
% - the data range is 0 - 4095
% - the black level is close to 0
% - width is multiple of 4

% from Axiom Beta
im1 = read_cr2('improved_register_settings.DNG');
im1 = im1(2:end-1, :) * 2;

% from Ursa Mini
im2 = read_cr2('FPN_4-ok.DNG');
im2 = im2(2:end-1, 2:end-3) * 2;

% from 5D3, crop mode
im3 = read_cr2('19020002.DNG');
im3 = max(im3 - 2048 + 100, 0);
im3 = min(im3, 4095);

images = {im1, im2, im3};

% Raw Mode 01, when "011"
bit_order_m1 = [ [4:11] ...                            % R1 = r(11:4)
            12 + [4:11] ...                            % G1 = g1(11:4)
            0+0 12+0 0+1 12+1 0+2 12+2 0+3 12+3 ...    % B1 = g1(3:0)*r(3:0)
            36 + [4:11] ...                            % R2 = b(11:4)
            24 + [4:11] ...                            % G2 = g2(11:4)
            36+0 24+0 36+1 24+1 36+2 24+2 36+3 24+3 ]; % B2 = g2(3:0)*b(3:0)

% Raw Mode 02, when "101"
bit_order_m2 = [ [4:11] ...                            % R1 = r(11 downto 4)
            12 + [4:11] ...                            % G1 = g1(11 downto 4)
            36 + [8:11] [48 48 48 48] ...              % B1 = b(11 downto 8), high bits padded with 0 (48 in bit_order means "always 0")
                 [0:3] [8:11] ...                      % R2 = r(11 downto 8) & r(3 downto 0)
            24 + [4:11] ...                            % G2 = g2(11 downto 4)
            36 + [[0:3] [8:11]] ];                     % B2 = b(11 downto 8) & b(3 downto 0);

% optional HDMI processing steps
global HDMI_PROCESS_YUV_BLUR_SHARPEN
global HDMI_PROCESS_YUV_NOISE
global HDMI_PROCESS_RGB_COMPRESS
HDMI_PROCESS_YUV_BLUR_SHARPEN = 0;
HDMI_PROCESS_YUV_NOISE = 0;
HDMI_PROCESS_RGB_COMPRESS = 0;
