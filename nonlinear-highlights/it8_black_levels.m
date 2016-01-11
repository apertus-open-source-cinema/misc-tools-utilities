function blacks = it8_black_levels(gray_patches, gxyz_ref)
        % find black level that gives a good match between
        % the gray patches and the reference XYZ brightness values
        x0 = [0 0 0 0] + 100;
        options = optimset('TolX', 1e-5, 'TolFun', 1e-5, 'MaxFunEvals', 10000);
        [blacks,e] = fminsearch(@(x) eval_black_level(x, gray_patches, gxyz_ref), x0, options);
        
        if nargout == 0,
            blacks
            round(blacks)
            e
            eval_black_level(blacks, gray_patches, gxyz_ref);
        end
end

function e = eval_black_level(x, gray_patches, gxyz_ref)
    
    % remove overexposed pixels (nonlinear response)
    M = max(gray_patches')';
    mask = M < 2500;
    gray_patches = gray_patches(mask,:);
    gxyz_ref = gxyz_ref(mask,:);
    
    % apply per-channel black level
    gray_patches(:,1) = -x(1) + gray_patches(:,1);
    gray_patches(:,2) = -x(2) + gray_patches(:,2);
    gray_patches(:,3) = -x(3) + gray_patches(:,3);
    gray_patches(:,4) = -x(4) + gray_patches(:,4);
    
    % extract colors channels
    r  = gray_patches(:,1);
    g1 = gray_patches(:,2);
    g2 = gray_patches(:,3);
    b  = gray_patches(:,4);
    g = (g1+g2) / 2;
    Y = gxyz_ref(:,2);

    % compute ratios
    gg = log2(g1./g2);
    rg = log2(r./g);
    bg = log2(b./g);
    rY = log2(r./Y);
    g1Y = log2(g1./Y);
    g2Y = log2(g2./Y);
    bY = log2(b./Y);

    % green-green ratio should be 1
    e_gg = norm(gg);
    
    % red-green and blue-green ratios should be constant (minimize variation)
    e_rg = std(rg);
    e_bg = std(bg);

    % each channel / luma ratio should be constant
    e_rY  = std(rY);
    e_g1Y = std(g1Y);
    e_g2Y = std(g2Y);
    e_bY  = std(bY);
    e_Y   = norm([e_rY e_g1Y e_g2Y e_bY]);
    
    if nargout == 0,
        hold off
        plot(rg, 'r'); hold on;
        plot(bg, 'b');
        plot(gg, 'g');

        plot(rY  - 5, 'r--');
        plot(g1Y - 5, 'g--');
        plot(g2Y - 5, 'g--');
        plot(bY  - 5, 'b--');
    end
        
    e = norm([e_Y e_gg e_rg e_bg]);
end
