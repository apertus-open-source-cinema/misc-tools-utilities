# you need to update frame numbers here
ImageSource("frame%03d.tif", 1, 31, 29.97)

# for some reason, the image ends up upside down
FlipVertical()

ConvertToYV12()

# simple temporal denoising
FluxSmoothT()

# stronger temporal denoising, for clips with exposure messed up (if any)
# note this will fail for 4K videos (maybe a 64-bit build of AviSynth could help)
# you will want to change settings (try "low" for well-exposed files and "high" for underexposed ones)
#MCTemporalDenoise(settings="medium")

ConvertToRGB()

