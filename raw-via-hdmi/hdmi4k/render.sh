# argument: mov file

# careful here
rm frame*.ppm frame*.pgm frame*.DNG

# extract a few frames to figure out the frame order
ffmpeg -i $1 -vf "framestep=2" -vframes 5 frame%05dA.ppm -y
ffmpeg -ss 00.016 -i $1 -vf "framestep=2" -vframes 5 frame%05dB.ppm -y
hdmi4k frame*A.ppm --check-only

if [ $? -eq 2 ]; then
    # not good, skip one frame
    ffmpeg -ss 00.016 -i $1 -vf "framestep=2" frame%05dA.ppm -y
    ffmpeg -ss 00.032 -i $1 -vf "framestep=2" frame%05dB.ppm -y
else
    # OK, process the entire video with these settings
    ffmpeg -i $1 -vf "framestep=2" frame%05dA.ppm -y
    ffmpeg -ss 00.016 -i $1 -vf "framestep=2" frame%05dB.ppm -y
fi

hdmi4k frame*A.ppm
raw2dng frame*.pgm

# todo:
# - temporal row noise correction
# - piping output to create DNGs directly from MOV without intermediate files
