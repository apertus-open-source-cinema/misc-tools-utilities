function n = rgb2_bit_name(x)
    names = {'R1', 'G1', 'B1', 'R2', 'G2', 'B2'};
    n = sprintf('%2s%-4s ', names{1+floor(x/8)}, ['(' num2str(mod(x,8)) ')']);
end