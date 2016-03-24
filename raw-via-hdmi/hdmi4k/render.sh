# argument: mov file

# careful here
rm *-[0-9][0-9][0-9][0-9][0-9].pgm *-[0-9][0-9][0-9][0-9][0-9].DNG

hdmi4k $1
raw2dng *.pgm

# todo:
# - temporal row noise correction
# - piping output to raw2dng
