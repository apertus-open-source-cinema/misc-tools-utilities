# you need to update frame numbers here
ImageSource("frame%03d.tif", 1, 31, 29.97)

# for some reason, the image ends up upside down
FlipVertical()

# temporal denoise
# you will want to change settings (try "low" for well-exposed files and "high" for underexposed ones)
ConvertToYV12()
MCTemporalDenoise(settings="medium")
ConvertToRGB()

