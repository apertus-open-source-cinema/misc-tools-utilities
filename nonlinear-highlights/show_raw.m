function show_raw(im, black, white)
    r  = im(1:2:end,1:2:end);
    g1 = im(1:2:end,2:2:end);
    g2 = im(2:2:end,1:2:end);
    b  = im(2:2:end,2:2:end);
    g = (g1 + g2) / 2;
    
    show_raw_rgb(r, g, b, black, white);
end

function show_raw_rgb(r, g, b, black, white)
    r = r - black;
    g = g - black;
    b = b - black;
    white = white - black;
    m = log2(white);

    IM(:,:,1) = (log2(max(1,r*1.28)) / m).^2;
    IM(:,:,2) = (log2(max(1,g)) / m).^2;
    IM(:,:,3) = (log2(max(1,b*1.46)) / m).^2;
    imshow(IM,[]);
end
