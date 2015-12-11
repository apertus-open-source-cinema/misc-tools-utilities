function im = show_raw(im, black, white)
    % quick and dirty raw rendering
    % (half-res, hardcoded WB and gamma)
    
    r  = im(1:2:end,1:2:end);
    g1 = im(1:2:end,2:2:end);
    g2 = im(2:2:end,1:2:end);
    b  = im(2:2:end,2:2:end);
    g = (g1 + g2) / 2;
    
    if nargout == 0
        show_raw_rgb(r, g, b, black, white);
    else
        im = show_raw_rgb(r, g, b, black, white);
    end
end

function IM = show_raw_rgb(r, g, b, black, white)
    r = r - black;
    g = g - black;
    b = b - black;
    white = white - black;
    m = log2(white);

    IM(:,:,1) = (log2(max(1,r*1.1)) / m).^3;
    IM(:,:,2) = (log2(max(1,g)) / m).^3;
    IM(:,:,3) = (log2(max(1,b*1.8)) / m).^3;
    
    if nargout == 0
        imshow(IM,[]);
    end
end
