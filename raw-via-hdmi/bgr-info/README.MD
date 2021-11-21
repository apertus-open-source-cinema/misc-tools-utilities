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


# Example Output
```
folder: ../scripts/Clip_00001-magritte-bubbles/
*.bgr files found in folder: 698

==========================================================
filename 		 framecounter 	 A/B frame
==========================================================
Clip_00001_frame_00000.bgr		22		A-Frame		09c1925ca53fb86aa8ec6575c6e14790
Clip_00001_frame_00001.bgr		23		B-Frame		cb1f00f9ba3f7b63f11377605e7a30ec
Clip_00001_frame_00002.bgr		24		A-Frame		44c9b405d28ba51bba265f2ceb88273e
Clip_00001_frame_00003.bgr		26		A-Frame		c47a412c0a6738a37318e6d887386b94		 framecounter missmatch
Clip_00001_frame_00004.bgr		27		B-Frame		b3c11cc53c8fd9bce02439a75896bf83
Clip_00001_frame_00005.bgr		28		A-Frame		06e50f49373d70f08d1306ce6d45a0e0
Clip_00001_frame_00006.bgr		29		B-Frame		852f81434e522638d95da8db017a6187
Clip_00001_frame_00007.bgr		30		A-Frame		aa27b07a964f191c7c8584e977c1fe3e
Clip_00001_frame_00008.bgr		32		A-Frame		f56e7d14cca90f746497dd97aaed6626		 framecounter missmatch
Clip_00001_frame_00009.bgr		33		B-Frame		0bc89c2740d243748973335c2384e53b
Clip_00001_frame_00010.bgr		34		A-Frame		a909a8af34f70959f07682378e34853b

...

Clip_00001_frame_00691.bgr		83		B-Frame		5a017f7e5306eef7f1ba8cb32e65ba6d
Clip_00001_frame_00692.bgr		84		A-Frame		0c1d3f9d440a714133764f26b1f6402f
Clip_00001_frame_00693.bgr		86		A-Frame		49ea7bea40398bbb77f634e5a1e0212a		 framecounter missmatch
Clip_00001_frame_00694.bgr		87		B-Frame		ed2b7c3395afd3f8e8aef013ab8f0056
Clip_00001_frame_00695.bgr		88		A-Frame		95381fa9f3790994cdecb941dbad399f
Clip_00001_frame_00696.bgr		89		B-Frame		a28c84e574a3652e5811a996c0c10c6d
Clip_00001_frame_00697.bgr		90		A-Frame		2eb29916c1f1479c5e701f3350e79d26
==========================================================
Skipped Frames: 276
Duplicate Frames: 0
==========================================================
```

