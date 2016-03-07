# you need to update frame numbers here
input = ImageSource("frame%05d.tif", 1, 31, 29.97)

# for some reason, the image ends up upside down
input = FlipVertical(input)

input = ConvertToYV12(input)

# simple temporal denoising
denoised = FluxSmoothT(input)

# stronger temporal denoising, for clips with exposure messed up (if any)
# note this will fail for 4K videos (maybe a 64-bit build of AviSynth could help)
# you will want to change settings (try "low" for well-exposed files and "high" for underexposed ones)
#denoised = MCTemporalDenoise(input, settings="medium")

# we don't need any denoising in highlights
mask = Levels(input, 50, 1, 150, 255, 0)
output = Overlay(input, denoised, 0, 0, mask)

output = ConvertToRGB(output)

return output
