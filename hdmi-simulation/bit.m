function b = bit(x, pos)
    b = bitshift(bitand(x, bitshift(1,pos)), -pos);
end