function [E,expo] = darkframe_eval(filename)
    disp(filename)
    [im,exif] = read_raw(filename);
    im = im - exif.black_level;
    expo = exif.exposure;
    
    E = [median(im(:)) std(im(:)) std(median(im')) std(median(im))];
    printf(' - median   : %.2f\n', E(1));
    printf(' - stdev    : %.2f\n', E(2));
    printf(' - row noise: %.2f\n', E(3));
    printf(' - col noise: %.2f\n', E(4));
end
