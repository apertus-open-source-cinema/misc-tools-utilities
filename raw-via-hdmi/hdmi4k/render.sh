#!/bin/sh
# Render a Shogun MOV footage recorded with Axiom BETA
# Very rough workflow, not optimized at all

# cleanup the working directory (be careful)
rm frame*

# extract frames from input file
# - input should be a .mov file transcoded from Shogun with ffmpeg -vcodec copy (not directly!)
# - that's because I've designed the HDMI recovery filters on transcoded output 
#   which is - surprise! - not identical (yes, with -vcodec copy; ffmpeg bug?)
ffmpeg -i T083.mov -vf "decimate=2" frame%05d.ppm

# convert to 4K output, ufraw-like look, exposure compensation, fix row noise temporally
# - you need to get calibration frames (darkframe-hdmi.ppm and lut-hdmi.spi1d) from:
#     http://files.apertus.org/AXIOM-Beta/snapshots/calibration-frames/cam2/
# - for some weird reasons, we get crushed blacks (I suspect the "black hole" effect)
#   so I've added an offset as an workaround (may not be needed on well-exposed files, to be tested)
# - output should be an approximation of sRGB (can't guarantee it is proper sRGB, I'm not troy_s :P )
# - for linear sRGB output, drop the --ufraw-gamma
# - for "raw" output (without LUT/matrix), remove the LUT file
# - tip: --exposure is linear, and --soft-film compresses highlights without clipping
hdmi4k frame*[0-9].ppm --ufraw-gamma --soft-film=1.5 --fixrnt --offset=500

# problem: avisynth doesn't read 16-bit PPM
rename 's/-out.ppm/.tif/' *
mogrify *.tif

# process the frames through avisynth and pipe the result through ffmpeg
# you may want to change the codec ;)
# another problem is that output is 420 (from avs2yuv); important for youtube?
wine avs2yuv render.avs - | ffmpeg -i pipe:0 -pix_fmt yuv420p -vcodec rawvideo video.avi -y

# now let's admire our masterpiece :)
ffplay -loop 100 video.avi

