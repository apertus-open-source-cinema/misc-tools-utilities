function [im,exif] = read_raw(filename, also_ob)
    if nargin < 2,
        also_ob = 0;
    end
    if also_ob
        dcraw = 'dcraw -c -4 -E';
    else
        dcraw = 'dcraw -c -4 -D';
    end
    system(sprintf('%s "%s" > tmp.pgm', dcraw, filename));
    im = double(imread('tmp.pgm'));
    system('rm tmp.pgm');
    
    if nargout == 2
        system(sprintf('exiftool "%s" -BlackLevel -WhiteLevel -ExposureTime -ISO -T > tmp.csv', filename));
        f = fopen('tmp.csv');
        d = char(fread(f))';
        fclose(f);
        d = str2num(d);
        exif.black_level = d(1);
        exif.white_level = d(2);
        exif.exposure    = d(3);
        exif.iso         = d(4);
        system('rm tmp.csv');
    end
end
