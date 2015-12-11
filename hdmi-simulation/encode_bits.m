function [R1,G1,B1,R2,G2,B2] = encode_bits(r,g1,g2,b, bit_order)
    % Encode Bayer raw data (r,g1,g2,b) into two HDMI frames:
    % (R1,G1,B1) and (R2,G2,B2), according to bit_order.
    %
    % bit_order is an array of bit positions in the raw data:
    % bits 0-11: red
    % bits 12-23: green1
    % bits 24-35: green2
    % bits 36-47: blue
    % total: 48 bits
    % so valid values in bit_order are from 0 to 47, plus special cases:
    % a bit_order value of 48 will return always 0
    % a bit_order value of 49 will return always 1
    %
    % bit_order should have 6*8 = 48 entries:
    % first 8 for R1, next 8 for G1 and so on,
    % and LSB bits appear first.
    %
    % See init.m for examples.

    assert(length(bit_order) == 48);
%     assert(length(bit_order) == length(unique(bit_order)));

    % assume 12-bit data
    r = uint64(r); g1 = uint64(g1); g2 = uint64(g2); b = uint64(b);
    raw = uint64(zeros(size(r)));
    raw = bitor(raw, bitshift(r,  0));
    raw = bitor(raw, bitshift(g1,12));
    raw = bitor(raw, bitshift(g2,24));
    raw = bitor(raw, bitshift(b, 36));
    raw = bitor(raw, bitshift(1, 49));
    
    rgb2 = uint64(zeros(size(r)));
    for i = 0:47
%         disp([raw_bit_name(bit_order(i+1)) ' -> ' rgb2_bit_name(i)]);
        rgb2 = bitor(rgb2, bitshift(bit(raw, bit_order(i+1)), i));
    end
    
    R1 = bitand(bitshift(rgb2,  -0), 255);
    G1 = bitand(bitshift(rgb2,  -8), 255);
    B1 = bitand(bitshift(rgb2, -16), 255);
    R2 = bitand(bitshift(rgb2, -24), 255);
    G2 = bitand(bitshift(rgb2, -32), 255);
    B2 = bitand(bitshift(rgb2, -40), 255);
    
    R1 = double(R1); G1 = double(G1); B1 = double(B1);
    R2 = double(R2); G2 = double(G2); B2 = double(B2);
end
