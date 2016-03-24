# argument: mov file

# careful here
rm frame*.dng

hdmi4k $1 - | raw2dng --pgm frame%05d.dng

# todo:
# - temporal row noise correction
