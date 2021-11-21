# Usage
```
This is bgr-info V0.2

SYNOPSIS
	bgr-info.py [parameters]

EXAMPLE
	bgr-info.py -w 2048 -h 1080 -i myfolder/

OPTIONS
	-w, --width:	    defines image resolution width (default: 1920)
	-h, --height:	    defines image resolution height (default: 1080)
	-i, --input:	    path to files that should be read
	-s, --skip-hash:	skip creating hash for every frame (significant speed up)
```

# Background
bgr-info reports and exits if there are no corner markers detected in the first *.bgr in the sequence.
Skipped and duplicate frames are reported at the end of the analysis.

