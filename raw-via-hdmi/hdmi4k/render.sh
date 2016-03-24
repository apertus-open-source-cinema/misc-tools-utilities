#!/bin/sh
# Convert Shogun MOV footage recorded with Axiom BETA to DNG

# Input should be a .mov file transcoded from Shogun with ffmpeg -vcodec copy (not directly!)
# that's because I've designed the HDMI recovery filters on transcoded output 
# which is - surprise! - not identical (yes, with -vcodec copy; ffmpeg bug?)

# You need to get calibration frames (darkframe-hdmi-[AB].ppm) from:
# http://files.apertus.org/AXIOM-Beta/snapshots/calibration-frames/cam2/

# careful here
rm frame*.dng

# this reads the MOV file via ffmpeg
# the output is piped to raw2dng, which also does temporal row noise correction
hdmi4k $1 - | raw2dng --pgm --fixrnt frame%05d.dng

# todo: HDMI dark frame averaging script
