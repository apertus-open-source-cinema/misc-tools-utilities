function [Y1,U1,V1,Y2,U2,V2] = encode_yuv(r,g1,g2,b,offset)
    % gamma curve from 12 to 8 bits
    gamma = log2((0:4095) + offset);
    gamma = gamma - gamma(1);
    gamma = round(gamma * 219 / gamma(end)) + 16;

    % apply gamma curve
    r  = gamma(r+1);
    g1 = gamma(g1+1);
    g2 = gamma(g2+1);
    b  = gamma(b+1);

    % map to YUV
    Y1 = uint8(g1);
    Y2 = uint8(g2);
    
    % channel values from 16 to 235 => difference can be from -219 to 219
    U1 = uint8((b - g1) / 2 + 128);
    V1 = uint8((r - g1) / 2 + 128);
    U2 = uint8((b - g2) / 2 + 128);
    V2 = uint8((r - g2) / 2 + 128);
end
