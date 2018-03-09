## Main script.
##
## This computes a set of LUTs for recovering highlights
## from the nonlinear region of the sensor response curve
## (in other words, highlights that would otherwise be clipped to white)
##
## You will need:
## - a custom compilation of octave to allow 16-bit images
##   ( http://marcelojoeng.blogspot.com/2012/11/compile-octave-using-1632-bits-colour.html )
## - splines package - http://octave.sourceforge.net/splines/
## - ufraw
## - input files will be downloaded from files.apertus.org
##
## Copyright (C) 2016 a1ex
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

% we need this to use csaps
pkg load splines
more off

% get files not included in repo
if ~exist('it8-gainx1-offset2047-80ms-02.raw12', 'file')
    system('wget http://files.apertus.org/AXIOM-Beta/snapshots/IT8%20Charts%20Nikon-08.01.2016/it8-gainx1-offset2047-80ms-02.raw12')
end
if ~exist('it8-gainx1-offset2047-20ms-02.raw12', 'file')
    system('wget http://files.apertus.org/AXIOM-Beta/snapshots/IT8%20Charts%20Nikon-08.01.2016/it8-gainx1-offset2047-20ms-02.raw12')
end
if ~exist('R131007.txt', 'file')
    system('wget http://files.apertus.org/AXIOM-Beta/snapshots/colorcharts/IT8/R131007.txt')
end
if ~exist('darkframe-x1.pgm', 'file')
    system('wget http://files.apertus.org/AXIOM-Beta/snapshots/darkframe-x1.pgm')
end
if ~exist('clipframe-x1.pgm', 'file')
    system('wget http://files.apertus.org/AXIOM-Beta/snapshots/clipframe-x1.pgm')
end

% read IT8 reference data
disp('Reading IT8 data...');
[xyz_ref, gxyz_ref] = read_it8('R131007.txt');

% sample IT8 data from properly-exposed and overexposed charts
% this step is slow, so only run it once
if ~exist('Cg_ref', 'var')
    disp('Rendering test files without LUT...');
    system('raw2dng it8-gainx1-offset2047-20ms-02.raw12 --swap-lines --fixrn');
    system('raw2dng it8-gainx1-offset2047-80ms-02.raw12 --swap-lines --fixrn');
    [Cg_ref,Cc_ref] = sample_it8('it8-gainx1-offset2047-20ms-02.DNG', 0);
    [Cg_ovr,Cc_ovr] = sample_it8('it8-gainx1-offset2047-80ms-02.DNG', 0);
end

% compute LUTs
disp('Computing LUT...');
lut = it8_luts(Cg_ref,Cc_ref,Cg_ovr,Cc_ovr,gxyz_ref);

% save lut
disp('Saving LUT...');
f = fopen('lut-x1.spi1d', 'w');
fprintf(f, 'Version 1\n');
fprintf(f, 'From 0.0 1.0\n');
fprintf(f, 'Length 4096\n');
fprintf(f, 'Components 4\n');
fprintf(f, '{\n');
for i = 1:size(lut,2)
    fprintf(f, '    %f %f %f %f\n', min(lut(:,i), 4095) / 4095);
end
fprintf(f, '}\n');
fclose(f);

disp('Rendering test files with LUT...');
system('raw2dng it8-gainx1-offset2047-20ms-02.raw12 --swap-lines --fixrn --lut');
system('raw2dng it8-gainx1-offset2047-80ms-02.raw12 --swap-lines --fixrn --lut');
system('ufraw-batch it8-gainx1-offset2047-80ms-02.ufraw --overwrite');

%~ disp('Done.');
