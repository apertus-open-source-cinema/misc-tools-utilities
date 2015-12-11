function [R1,G1,B1,R2,G2,B2] = encode_gamma(r,g1,g2,b,offset)
    % gamma curve from 12 to 8 bits
    gamma = log2((0:4095) + offset);
    gamma = gamma - gamma(1);
    gamma = round(gamma * 255 / gamma(end));

    % apply gamma curve
    r  = gamma(r+1);
    g1 = gamma(g1+1);
    g2 = gamma(g2+1);
    b  = gamma(b+1);

    % copy to output; R and B are redundant,
    % G1 and G2 are used to improve resolution
    R1 = r;
    G1 = g1;
    B1 = b;
    R2 = r;
    G2 = g2;
    B2 = b;
end
