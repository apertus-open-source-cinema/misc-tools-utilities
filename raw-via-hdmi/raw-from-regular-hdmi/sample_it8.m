function [Cg,Cc] = sample_it8(filename_or_im, should_plot)
    marks = [745,239,1667,206,1685,743,754,770];

    if ischar(filename_or_im)
        im = read_raw(filename_or_im);
    else
        im = filename_or_im;
    end
    
    im = im(2:end-1,:);

    if should_plot,
        clf
        show_raw(im, 145, 4095);
        hold on;
    end
    
    % extract bottom gray line
    Lx = linspace(-0.003,1.003,25) - 0.001;
    Lx = (Lx(1:end-1) + Lx(2:end)) / 2;
    [x,y] = xform(Lx, 1.075 * ones(size(Lx)), marks);
    r = (x(2)-x(1)) / 3.5;
    Cg = extract_colors(im, x, y, r, r*2, 'g', 1, should_plot);

    % extract middle gray column
    Ly = linspace(-0.003,1.003,15);
    Ly = (Ly(1:end-1) + Ly(2:end)) / 2;
    [x,y] = xform(Lx(17) * ones(size(Ly(2:end-1))), Ly(2:end-1), marks);
    Cg2 = extract_colors(im, x, y, r, r, 'c', 1, should_plot);
    Cg = [Cg2; Cg];
    
    % extract color data
    [xx,yy] = meshgrid(Lx([2:16 18:end-1]), Ly(2:end-1));
    [x,y] = xform(xx, yy, marks);
    Cc = extract_colors(im, x, y, r, r, 'r', 1, should_plot);
    drawnow;
end

function [r,g1,g2,b] = raw_to_rggb(im)
    r  = im(1:2:end,1:2:end);
    g1 = im(1:2:end,2:2:end);
    g2 = im(2:2:end,1:2:end);
    b  = im(2:2:end,2:2:end);
end

function C = extract_colors(im, X, Y, rx, ry, marker_color, rggb, should_plot)
    C = [];
    
    % convert raw data to rgb (without debayering, just half-res)
    [R,G1,G2,B] = raw_to_rggb(im);
    
    X = round(X); Y = round(Y); rx = round(rx); ry = round(ry);
    
    % extract color patches and compute median color
    for i = 1:length(X)
        x = X(i); y = Y(i);
        if should_plot,
            plot([x - rx, x + rx, x + rx, x - rx, x - rx], [y - ry, y - ry, y + ry, y + ry, y - ry], marker_color),
        end
        r  = R (y-ry:y+ry, x-rx:x+rx);
        g1 = G1(y-ry:y+ry, x-rx:x+rx);
        g2 = G2(y-ry:y+ry, x-rx:x+rx);
        b  = B (y-ry:y+ry, x-rx:x+rx);
        if rggb,
            C(end+1,:) = [median(r(:)), median(g1(:)), median(g2(:)), median(b(:))];
        else
            g = (g1 + g2) / 2;
            C(end+1,:) = [median(r(:)), median(g(:)), median(b(:))];
        end
    end
end

% convert normalized coordinates (0...1, extremes being the fiducial marks)
% to pixel coordinates
function [x,y] = xform(xn, yn, marks)
    X = marks(1:2:end);
    Y = marks(2:2:end);
    xref = [0 1 1 0];
    yref = [0 0 1 1];
    % M * [xref;yref;1] = [X;Y;1]
    mref = [xref; yref; ones(1,4)];
    mrks = [X; Y; ones(1,4)];
    M = mrks / mref;
    xy = round(M * [xn(:)'; yn(:)'; ones(size(xn(:)')) ]);
    x = xy(1,:);
    y = xy(2,:);
end
