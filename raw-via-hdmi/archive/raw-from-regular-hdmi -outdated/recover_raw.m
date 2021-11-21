## Attempt to recover raw image from a regular HDMI recording
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

disp('reading data...')

% raw12 reference data
raw = double(imread('ref-raw.pgm'));

% HDMI frame (from ffmpeg)
f = double(imread('frame1.ppm'));

disp('matching sizes...')
skip_left = (4096 - 1920*2) / 2;
skip_top  = (3072 - 1080*2) / 2;
raw  = raw (1+skip_top : end-skip_top, 1+skip_left : end-skip_left);

% undo gamma and scale to 12-bit
f = (f / 65536) .^ 2 * 4095;

fr = f(:,:,1);
fg = f(:,:,2);
fb = f(:,:,3);

% bayer HDMI image
H = raw;
H(1:2:end,1:2:end) = fg;
H(1:2:end,2:2:end) = fb;
H(2:2:end,1:2:end) = fr;
H(2:2:end,2:2:end) = fg;

disp('sampling IT8 data...')
[Gr,Cr] = sample_it8(raw, 0);
[Gh,Ch] = sample_it8(H, 0);

% plot IT8 data (HDMI on X, reference raw on Y)
colors = 'rggbc';
clf, hold on
for i = 1:4
    plot(log2(Gh(:,i)), log2(Gr(:,i)), ['.' colors(i)]);
    % plot(Gh(:,i), Gr(:,i), ['.' colors(i)]);
end

disp('fitting IT8 data...')
P = [];
x = log2([Gh(:,2); Gh(:,3)]);
y = log2([Gr(:,2); Gr(:,3)]);
p = polyfit(x, y, 2);
P(2,:) = p;
x = sort(x);
plot(x, p(1)*x.^2 + p(2)*x + p(3), 'g');

% fit red and blue channels (we know they are identical)
x = log2([Gh(:,1); Gh(:,4)]);
y = log2([Gr(:,1); Gr(:,4)]);
p = polyfit(x, y, 2);
P(1,:) = p;
P(3,:) = p;
x = sort(x);
plot(x, p(1)*x.^2 + p(2)*x + p(3), 'm');

% print correction parameters
for i = [2 3 1 2]
    disp(sprintf('        { %.4g, %.4g, %.4g },', P(i,:)))
end

disp('reversing hdmi raw...');
f = double(imread('frame1.ppm'));
[fraw,f] = reverse_curves(f, P);

disp('adjusting dark frame...');

darkf = double(imread('darkframe-x1-hdmi.ppm'));
[dfraw,darkf] = reverse_curves(darkf, P);

dfraw = dfraw - 128;
darkf = darkf - 128;

imwrite(uint16(dfraw*8+1024), 'darkframe-x1.pgm');
disp('saved darkframe-x1.pgm')
disp('done.')
