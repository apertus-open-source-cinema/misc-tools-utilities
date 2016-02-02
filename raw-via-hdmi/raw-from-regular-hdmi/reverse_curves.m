function [raw,f] = reverse_curves(f, P)
    
    % undo gamma and scale to 12-bit
    f = (f / 65536) .^ 2 * 4095;
    
    % switch to log
    f = log2(f);
    
    fr = f(:,:,1);
    fg = f(:,:,2);
    fb = f(:,:,3);
    
    % apply curve
    fr = 2 .^ (P(1,1) * fr.^2 + P(1,2) * fr + P(1,3));
    fg = 2 .^ (P(2,1) * fg.^2 + P(2,2) * fg + P(2,3));
    fb = 2 .^ (P(3,1) * fb.^2 + P(3,2) * fb + P(3,3));
    
    f(:,:,1) = fr;
    f(:,:,2) = fg;
    f(:,:,3) = fb;

    % bayer GBRG
    raw = zeros(size(fr)*2);
    raw(1:2:end,1:2:end) = fg;
    raw(1:2:end,2:2:end) = fb;
    raw(2:2:end,1:2:end) = fr;
    raw(2:2:end,2:2:end) = fg;
end
