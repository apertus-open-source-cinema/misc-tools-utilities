function n = raw_bit_name(x)
    names = {'r', 'g1', 'g2', 'b'};
    if x == 48
        n = '   0   ';
        return;
    end
    if x == 49
        n = '   1   ';
        return;
    end
    n = sprintf('%2s%-4s ', names{1+floor(x/12)}, ['(' num2str(mod(x,12)) ')']);
end