function print_bit_order(bit_order)
    print_byte(bit_order(1:8),   'R1');
    print_byte(bit_order(9:16),  'G1');
    print_byte(bit_order(17:24), 'B1');
    print_byte(bit_order(25:32), 'R2');
    print_byte(bit_order(33:40), 'G2');
    print_byte(bit_order(41:48), 'B2');
end

function print_byte(bits, name)
    assert(length(bits) == 8)
    out = [name ': '];
    for i = 8:-1:1
        out = [out raw_bit_name(bits(i))];
    end
    disp(out)
end
