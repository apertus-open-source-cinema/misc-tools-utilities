function fullres_raw(raw, base_name)
    pgm = [base_name '.pgm'];
    dng = [base_name '.DNG'];
    raw = raw(2:end-1,2:end-1);
    imwrite(uint16(raw), pgm);
    system(['pgm2dng ' pgm]);
    system(['exiftool ' dng ' -TagsFromFile improved_register_settings.DNG -ColorMatrix1 -overwrite_original']);
    system(['ufraw-batch --out-type=jpg --exposure=2 --wb=auto --overwrite ' dng ' ']);
    system(['rm ' pgm]);
    system(['rm ' dng]);
end