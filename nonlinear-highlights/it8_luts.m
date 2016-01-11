function lut = it8_luts(Cg_ref,Cc_ref,Cg_ovr,Cc_ovr,gxyz_ref)

    % compute black levels
    % (these will include any light leaks, which get added to the entire image)
    B_ref = it8_black_levels(Cg_ref, gxyz_ref);
    B_ovr = it8_black_levels(Cg_ovr, gxyz_ref);
    
    % concatenate color and grayscale data 
    C_ref = [Cg_ref; Cc_ref];
    C_ovr = [Cg_ovr; Cc_ovr];

    % subtract black levels
    N = size(C_ref,1);
    C_ref = C_ref - ones(N,1) * B_ref;
    C_ovr = C_ovr - ones(N,1) * B_ovr;
    
    % compute a LUT for each channel
    clf, hold on
    lut_r  = compute_lut_spline(C_ref(:,1), C_ovr(:,1), 'r');
    lut_g1 = compute_lut_spline(C_ref(:,2), C_ovr(:,2), 'g');
    lut_g2 = compute_lut_spline(C_ref(:,3), C_ovr(:,3), 'c');
    lut_b  = compute_lut_spline(C_ref(:,4), C_ovr(:,4), 'b');
    lut = [lut_r; lut_g1; lut_g2; lut_b];

    % fixme: hackery to undo the black offsets (doesn't look quite right)
    for i = 1:4
        lut(i,:)  = offset_lut(lut(i,:),  B_ref(i), B_ovr(i));
    end

    % print the LUTs to console (copy/paste to raw2dng)
    disp(['const uint16_t Lut_R [] = { ' sprintf('%d,', round(lut(1,:) * 8)) ' }; ' ]);
    disp(['const uint16_t Lut_G1[] = { ' sprintf('%d,', round(lut(2,:) * 8)) ' }; ' ]);
    disp(['const uint16_t Lut_G2[] = { ' sprintf('%d,', round(lut(3,:) * 8)) ' }; ' ]);
    disp(['const uint16_t Lut_B [] = { ' sprintf('%d,', round(lut(4,:) * 8)) ' }; ' ]);
end

function lut = offset_lut(lut, b_ref, b_ovr)
    lut = interp1(1:4096, lut, (1:4096) - b_ovr) + b_ref;
    b_ref0 = b_ref - b_ovr * 20/80;
    N = round(b_ovr)+4;
    lut(1:N) = linspace(b_ref0, lut(N+1), N);
end

% find a LUT that matches the overexposed image to the reference (well-exposed) one
% assume there are 2 stops between them
function lut = compute_lut_spline(ref, ovr, color)
    Y = log2(ref);
    X = log2(ovr);
    
    % trickery for csaps to accept non-monotonic inputs
    Y = Y + randn(size(Y)) * 1e-5;
    [Y,o] = sort(Y);
    X = X(o);
    
    % fit the LUT, reversed (from normal to overexposed)
    % because the result is much better this way
    % (the curves for undoing the nonlinearity are really steep)
    sp = csaps(Y,X,0.99);

    xlut = log2(1:4096);
    lut = fnval(sp, xlut);
    
    % we don't have shadow data, so the fit is bogus there
    % replace it with a dummy LUT (darken by 2 stops)
    % with a smooth transition to the fitted one
    thr = 9;
    k = xlut(xlut<thr); k = k / k(end); k = 1-k; k = k .^ 0.5;
    lut(xlut<thr) = (xlut(xlut<thr) + 2) .* k + lut(xlut<thr) .* (1-k);

    % plot the normal-to-overexposed LUTs
    plot(log2(ref), log2(ovr), ['.' color]);
    plot(log2(1:4096), lut, 'k', 'linewidth', 2);
    
    % now reverse the LUT (from overexposed to normal)
    lut = 2 .^ interp1(lut, log2(1:4096), log2(1:4096));
    lut(isnan(lut)) = max(lut);
end
