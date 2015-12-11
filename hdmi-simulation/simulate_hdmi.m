function e = simulate_hdmi(raw, method, arg, output_file)
    % assume raw is in [RG;GB] order
    % method can be:
    % - 'bits'  => arg is a bit_order array (48 entries)
    % - 'gamma' => arg is a scalar offset (parameter for the gamma curve)
    % - '422'   => arg is the same as for 'gamma' (parameter for the gamma curve)
    % output_file is optional

    disp('HDMI simulation...')

    raw = max(raw, 0);
    raw = min(raw, 4095);
    
    r  = raw(1:2:end,1:2:end);
    g1 = raw(1:2:end,2:2:end);
    g2 = raw(2:2:end,1:2:end);
    b  = raw(2:2:end,2:2:end);
    
    if (nargout == 0)
        % only check those in interactive mode, for speed
        assert(norm(g1(:)-g2(:)) < norm(r(:)-g1(:)));
        assert(norm(g1(:)-g2(:)) < norm(b(:)-g1(:)));
    end
    
    disp('Encoding image...')
    switch method
        case 'bits'
            [R1,G1,B1,R2,G2,B2] = encode_bits(r,g1,g2,b,arg);
        case 'gamma'
            [R1,G1,B1,R2,G2,B2] = encode_gamma(r,g1,g2,b,arg);
        case 'yuv'
            [Y1,U1,V1,Y2,U2,V2] = encode_yuv(r,g1,g2,b,arg);
    end
   
    disp('Simulating HDMI compression/processing...')
    if (strcmp(method, 'yuv'))
        [Y1,U1,V1,Y2,U2,V2] = process_hdmi_yuv(Y1,U1,V1,Y2,U2,V2);

        % for display only
        [R1,G1,B1] = yuv2rgb(Y1,U1,V1);
        [R2,G2,B2] = yuv2rgb(Y2,U2,V2);
    else
        [R1,G1,B1,R2,G2,B2] = process_hdmi_rgb(R1,G1,B1,R2,G2,B2);
    end
    
    disp('Decoding image...')
    switch method
        case 'bits'
            [rr,rg1,rg2,rb] = decode_bits(R1,G1,B1,R2,G2,B2,arg);
        case 'gamma'
            [rr,rg1,rg2,rb] = decode_gamma(R1,G1,B1,R2,G2,B2,arg);
        case 'yuv'
            [rr,rg1,rg2,rb] = decode_yuv(Y1,U1,V1,Y2,U2,V2,arg);
    end

    if (nargout == 0)
        rraw = raw;
        rraw(1:2:end,1:2:end) = rr;
        rraw(1:2:end,2:2:end) = rg1;
        rraw(2:2:end,1:2:end) = rg2;
        rraw(2:2:end,2:2:end) = rb;
        RGB1(:,:,1) = uint8(R1);
        RGB1(:,:,2) = uint8(G1);
        RGB1(:,:,3) = uint8(B1);
        RGB2(:,:,1) = uint8(R2);
        RGB2(:,:,2) = uint8(G2);
        RGB2(:,:,3) = uint8(B2);
        imraw = show_raw([raw rraw],0,4095);
        imhdmi = double([RGB1 RGB2])/255;
        if nargin == 4
            disp(['Saving ' output_file ' ...'])
            imwrite([imraw;imhdmi], output_file);
            p = find(output_file=='-',1); if isempty(p), p = length(output_file)-3; end
            fullres_raw(raw,  [output_file(1:p-1) '-f']);
            fullres_raw(rraw, [output_file(1:end-4) '-f']);
        else
            imshow([imraw;imhdmi]);
        end
    end
    
    % compute MSE between original and recovered raw
    n1 = norm(rr(:)  - r(:));
    n2 = norm(rg1(:) - g1(:));
    n3 = norm(rg2(:) - g2(:));
    n4 = norm(rb(:)  - b(:));
    e = norm([n1 n2 n3 n4]);
end

function [R,G,B] = yuv2rgb(Y,U,V)
    YCBCR(:,:,1) = uint8(Y);
    YCBCR(:,:,2) = uint8(U);
    YCBCR(:,:,3) = uint8(V);
    RGB = ycbcr2rgb(YCBCR);   
    R = uint8(RGB(:,:,1) * 255);
    G = uint8(RGB(:,:,2) * 255);
    B = uint8(RGB(:,:,3) * 255);
end