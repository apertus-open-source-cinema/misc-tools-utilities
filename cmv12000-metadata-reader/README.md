# metadatareader

## About

Read the 128x16 bit block from a raw12/raw16 image or from the AXIOM Beta via piped stdin and display it in a human readable format. The metadatablock is just optionally appended at the end of a raw12/raw16 file.

## Compiling

```
make
```

## Usage Example

```
tail $1 -c 256 | ./metadatareader 
```

read-metadata.sh does the above and uses the raw12/raw16 file name as first argument and any arguments to be passed to metadatareader as second argument.
Red lines indicate  the values differ from the defaults - this does not mean there is a problem or error.

The included meta.dump is an example 128x16 bit block extracted from an image.

```
cat meta.dump | ./metadatareader
```

## Parameters

```
-h		print help message
-r		print raw registers
-swap-endian	swap endianess of piped binary input
```

## Example Output
```
-------------------------------------------------------------------------------------------------------------------
Register	Name			Decimal		Meaning			Description
-------------------------------------------------------------------------------------------------------------------
0		not used
1[15:0]		Number_lines_tot:	3072					number of used sensor lines
2[15:0]		Y_start:		0
3[15:0]		Y_start:		0
4[15:0]		Y_start:		0
5[15:0]		Y_start:		0
6[15:0]		Y_start:		0
7[15:0]		Y_start:		0
8[15:0]		Y_start:		0
9[15:0]		Y_start:		0
10[15:0]	Y_start:		0
11[15:0]	Y_start:		0
12[15:0]	Y_start:		0
13[15:0]	Y_start:		0
14[15:0]	Y_start:		0
15[15:0]	Y_start:		0
16[15:0]	Y_start:		0
17[15:0]	Y_start:		0
18[15:0]	Y_start:		0
19[15:0]	Y_start:		0
20[15:0]	Y_start:		0
21[15:0]	Y_start:		0
22[15:0]	Y_start:		0
23[15:0]	Y_start:		0
24[15:0]	Y_start:		0
25[15:0]	Y_start:		0
26[15:0]	Y_start:		0
27[15:0]	Y_start:		0
28[15:0]	Y_start:		0
29[15:0]	Y_start:		0
30[15:0]	Y_start:		0
31[15:0]	Y_start:		0
32[15:0]	Y_start:		0
33[15:0]	Y_start:		0
34[15:0]	Y_size:			0
35[15:0]	Y_size:			0
36[15:0]	Y_size:			0
37[15:0]	Y_size:			0
38[15:0]	Y_size:			0
39[15:0]	Y_size:			0
40[15:0]	Y_size:			0
41[15:0]	Y_size:			0
42[15:0]	Y_size:			0
43[15:0]	Y_size:			0
44[15:0]	Y_size:			0
45[15:0]	Y_size:			0
46[15:0]	Y_size:			0
47[15:0]	Y_size:			0
48[15:0]	Y_size:			0
49[15:0]	Y_size:			0
50[15:0]	Y_size:			0
51[15:0]	Y_size:			0
52[15:0]	Y_size:			0
53[15:0]	Y_size:			0
54[15:0]	Y_size:			0
55[15:0]	Y_size:			0
56[15:0]	Y_size:			0
57[15:0]	Y_size:			0
58[15:0]	Y_size:			0
59[15:0]	Y_size:			0
60[15:0]	Y_size:			0
61[15:0]	Y_size:			0
62[15:0]	Y_size:			0
63[15:0]	Y_size:			0
64[15:0]	Y_size:			0
65[15:0]	Y_size:			0

-------------------------------------------------------------------------------------------------------------------
Register	Name			Decimal		Meaning			Description
-------------------------------------------------------------------------------------------------------------------
66[15:0]	Sub_offset:		0
67[15:0]	Sub_step:		1
68[3]		Color_exp:		0		Color sensor
68[2]		Bin_en:			0		Binning disabled
68[1]		Sub_en:			0		Image subsampling disabled
68[0]		Color:			0		Color sensor
69[1:0]		Image_flipping:		2		Image flipping in Y
70[0]		Exp_dual:		0		OFF			HDR interleaved coloumn mode
70[1]		Exp_ext:		0		Internal Exposure Mode
71[15:0]	Exp_time:		1935		--			Exposure Time Part 1
72[23:16]	Exp_time:		0		--			Exposure Time Part 2
72[23:0]	Exp_time:		1935		19.9997			Exposure Time (ms)
73[15:0]	Exp_time2:		1536		--			Exposure Time 2 Part 1
74[23:16]	Exp_time2:		0		--			Exposure Time 2 Part 2
74[23:0]	Exp_time2:		1536		15.882			Exposure Time 2 (ms)
75[15:0]	Exp_kp1:		0		--			Exposure Knee Point 1 Part 1
76[23:16]	Exp_kp1:		0		--			Exposure Knee Point 1 Part 2
76[23:0]	Exp_kp1:		0		171799			Knee Point 1 Exposure Time (ms)
77[15:0]	Exp_kp2:		0		--			Exposure Knee Point 2 Part 1
78[23:16]	Exp_kp2:		0		--			Exposure Knee Point 2 Part 2
78[23:0]	Exp_kp1:		0		171799			Knee Point 2 Exposure Time (ms)
79[1:0]		Number_slopes:		1		1 slope			Number of slopes
80[15:0]	Number_frames:		1		1			Number of frames to grab and send (intern exp. only)
81[4:0]		Output_mode:		1		16 outputs		Number of LVDS channels used on each side
81[5]		Disable_top:		0		Two sided read-out (top and bottom)
82[15:0]	Setting_1:		1822					Additional register setting 1
83[15:0]	Setting_2:		5897					Additional register setting 2
84[15:0]	Setting_3:		257					Additional register setting 3
85[15:0]	Setting_4:		257					Additional register setting 4
86[15:0]	Setting_5:		257					Additional register setting 5
87[11:0]	Offset_bot:		2047					Dark level offset on bottom output signal
88[11:0]	Offset_top:		2047					Dark level offset on top output signal
89[11:0]	Training_pattern:	2709					Value sent over LVDS when no valid image data is sent
89[15]		Black_col_en:		1		Disabled			Electrical black reference columns
-------------------------------------------------------------------------------------------------------------------
Register	Name			Decimal		Meaning			Description
-------------------------------------------------------------------------------------------------------------------

90[15:0]	Channel_en_bot		21845		--			Bottom data output channel (See register 91)
91[31:16]	Channel_en_bot		21845		Enabled			Bottom data output channel
92[15:0]	Channel_en_top		21845		--			Top data output channel (See register 93)
93[31:16]	Channel_en_top		21845		Enabled			Top data output channel
94[0]		Channel_en		1		Enabled			Output clock channel
94[1]		Channel_en		1		Enabled			Control channel
94[2]		Channel_en		1		Enabled			Input clock channel
95[15:0]	ADC_clk_en_bot:		65535		Enabled			Bottom A/D Converter clock
96[15:0]	ADC_clk_en_top:		65535		Enabled			Top A/D Converter clock
97[--]		--			0		--			Fixed Value
98[--]		--			34952		--			Set to 39705
99[--]		--			34952		--			Fixed Value
100[--]		--			0		--			Do Not Change
101[--]		--			0		--			Do Not Change
102[--]		--			8302		--			Set to 8312
103[--]		--			4032		--			Fixed Value
104[--]		--			64		--			Fixed Value
105[--]		--			8256		--			Fixed Value
106[0:6]	Vtfl2:			64					Voltage threshold for Kneepoint 1 in Multiple slope
106[13:7]	Vtfl3:			64					Voltage threshold for Kneepoint 2 in Multiple slope
107[--]		--			10462		--			Set to 9814
108[--]		--			12381		--			Set to 12381
109[--]		--			14448		--			Additional register setting
110[--]		--			12368		--			Fixed Value
111[--]		--			34952		--			Fixed Value
112[--]		--			277		--			Set to 5
113[15:0]	Setting_6:		542					Additional register setting 6
114[15:0]	Setting_7:		200					Additional register setting 7
115[3]		PGA_div:		0		OFF			Divide signal by 3
115[2:0]	PGA_gain:		1		x2 gain			Analog gain applied on output signal
116[7:0]	ADC_range:		255		12 bit mode		Slope of the ramp used by the ADC
116[9:8]	ADC_range_mult:		3		10/12 bit mode		Slope of the ramp used by the ADC
117[4:0]	DIG_gain:		1		12 bit mode		Apply digital gain to signal
118[1:0]	Bit_mode:		0		12 bit mode		Bits per pixel
119[--]		--			0		--			Do Not Change
120[--]		--			9		--			Do Not Change
121[--]		--			1		--			Fixed Value
122[--]		--			32		--			Do Not Change
123[--]		--			0		--			Do Not Change
125[--]		--			2		--			Fixed Value
126[--]		--			770		--			Do Not Change
127[15:0]	Temp_sensor:		634					On-chip digital temperature sensor value
```

