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

function [xyz,xyz_gray,labels] = read_it8(filename)
    % read XYZ data from IT8 reference file

    xyz = [];
    labels = {};

    f = fopen(filename);
    while ~strcmp(strtrim(fgetl(f)), 'BEGIN_DATA'), end

    for i = 1:288
        line = fgetl(f);
        labels{end+1} = strtrim(line(1:4));
        data = str2num(line(5:end));
        xyz(end+1,:) = data(1:3);
    end
    fclose(f);

    xyz_gray = [];
    for i = 1:288
        if strcmp(labels{i}(2:end),'16') || strcmp(labels{i}(1:2),'GS')
            xyz_gray(end+1,:) = xyz(i,:);
        end
    end
end
