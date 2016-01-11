% Main script.
%
% This computes a set of LUTs for recovering highlights
% from the nonlinear region of the sensor response curve
% (in other words, highlights that would otherwise be clipped to white)
% 
% Input files can be downloaded from http://files.apertus.org/AXIOM-Beta/snapshots/
% You will also need a dark frame (average as many as you find practical)
% and what I call a "clip frame" (completely overexposed image of a blank wall,
% averaged and with dark frame subtracted). Save the frames with:
% imwrite(uint16(dark*8,'darkframe-x1.pgm')); imwrite(uint16(clip*8,'clipframe-x1.pgm'));
% See raw2dng help for more info about these files.
%
% You need a custom compilation of octave + GraphicsMagick to allow 16-bit images, btw.

% we need this to use csaps
pkg load splines
more off

% read IT8 reference data
[xyz_ref, gxyz_ref] = read_it8('R131007.txt', 1);

% sample IT8 data from properly-exposed and overexposed charts
system('raw2dng it8-gainx1-offset2047-20ms-02.raw12 --swap-lines');
system('raw2dng it8-gainx1-offset2047-80ms-02.raw12 --swap-lines');
[Cg_ref,Cc_ref] = sample_it8('it8-gainx1-offset2047-20ms-02.DNG');
[Cg_ovr,Cc_ovr] = sample_it8('it8-gainx1-offset2047-80ms-02.DNG');

% compute LUTs
lut = it8_luts(Cg_ref,Cc_ref,Cg_ovr,Cc_ovr,gxyz_ref);

% Now copy the LUTs from the console into raw2dng, compile it,
% and try: raw2dng overexposed-gainx1-80ms.raw12 --swap-lines --lut --fixrn
%
% You may want to fiddle with the row noise reduction algorithm,
% or fine-tune the LUTs. Have fun!
