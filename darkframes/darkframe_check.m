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

function darkframe_check(dcnu)
    % Run this into a folder with darkframes, all taken at the same gain,
    % with different exposure times (e.g. from 1ms to 50ms).
    %
    % It will create a master darkframe, then check each individual darkframe
    % against the master darkframe (offset, stdev, row/column noise).
    %
    % There are two dark frame algorithms available:
    % simple or with dark current nonuniformity (dcnu).

    more off

    % create master darkframe, then develop the dark frames using the master dark frame
    if dcnu,
        system('raw2dng *.raw12 --swap-lines --calc-dcnuframe')
        system('raw2dng *.raw12 --swap-lines')
    else
        system('raw2dng *.raw12 --swap-lines --calc-darkframe')
        system('raw2dng *.raw12 --swap-lines --no-dcnuframe')
    end

    % read each DNG and check it
    files = dir('.');
    E = [];
    expo = [];
    for i = 1:length(files)
        f = files(i).name;
        if length(f) > 5 && f(end-3:end) == '.DNG'
            [E(end+1,:), expo(end+1)] = darkframe_eval(f);
        end
    end

    [expo,o] = sort(expo);
    E = E(o,:);

    plot(expo*1000, E)
    legend('median', 'stdev', 'row noise', 'col noise')
    xlabel('exposure (ms)')
    ylabel('error metrics (dn)')
    print -dpng darkframe.png

    disp('')
    disp('Summary:')
    disp('========')
    printf('average offset   : %.2f\n', mean(abs(E(:,1))));
    printf('average stdev    : %.2f\n', mean(E(:,2)));
    printf('average row noise: %.2f\n', mean(E(:,3)));
    printf('average col noise: %.2f\n', mean(E(:,4)));
end
