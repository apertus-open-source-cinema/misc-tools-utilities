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
