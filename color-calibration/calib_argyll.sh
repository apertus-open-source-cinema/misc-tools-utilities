#!/bin/bash

# Apertus color calibration using Argyll
#
# Based on https://wiki.apertus.org/index.php?title=Color_Profiling
# and https://encrypted.pcode.nl/blog/2010/06/28/darktable-camera-color-profiling/
# and advice from troy_s

# Adjust these paths to point to your Argyll installation (including / at the end),
# or leave then empty to use the default installation
ARGYLL_BIN_PATH=~/src/Argyll_V1.8.3/bin/
ARGYLL_REF_PATH=~/src/Argyll_V1.8.3/ref/

# Input data:
# 1. Input file (.raw12)
# 2. Color chart reference files
# 3. Fiducial markers (x1,y1,x2,y2,x3,y3,x4,y4, used with scanin -F)
# (just add charts under the 'case' statement as needed)

case $1 in
    ("CCP")
        # ColorCheckerPassport
        RAW_CHART="colorchecker_gainx2_15ms_01.raw12"
        CHT=${ARGYLL_REF_PATH}ColorCheckerPassport.cht
        CIE=${ARGYLL_REF_PATH}ColorCheckerPassport.cie
        MARKS=1309,741,1318,1171,674,1196,659,763
    ;;
    ("CC")
        # ColorChecker - assuming the lower half of the Passport is compatible
        RAW_CHART="colorchecker_gainx2_15ms_01.raw12"
        CHT=${ARGYLL_REF_PATH}ColorChecker.cht
        CIE=${ARGYLL_REF_PATH}ColorChecker.cie
        MARKS=659,768,1305,746,1315,1171,674,1196
    ;;
    ("IT8")
        # IT8
        RAW_CHART="colorchart_IT8_gainx2_15ms_01.raw12"
        CHT=${ARGYLL_REF_PATH}it8.cht
        CIE=R131007.txt
        MARKS=562,285,1630,280,1633,904,557,903
    ;;
    ("HTC")
        # for Hutch
        RAW_CHART="colorchart_HTC_gainx2_15ms_01.raw12"
        CHT=${ARGYLL_REF_PATH}Hutchcolor.cht
        CIE=0585.txt
        MARKS=530,424,1522,415,1512,1134,549,1145
    ;;
    ("clean")
        # be careful with this one ;)
        rm *.tiff *.tif *.png *.DNG *.ti3 *.icc *.spi1d
        exit
    ;;
    (*)
        echo "Usage: $0 [CCP|CC|IT8|HTC|clean]"
        exit
    ;;
esac

# Base file name
BASE_NAME=`basename "$RAW_CHART" .raw12`

# Argyll executables and data files
SCANIN=${ARGYLL_BIN_PATH}scanin
COLPROF=${ARGYLL_BIN_PATH}colprof
PROFCHECK=${ARGYLL_BIN_PATH}profcheck

# print Argyll version (better way to do this?)
echo "Checking Argyll version:"
$SCANIN 2>&1 |grep --color=never "Scanin, Version"
echo ""

# from now on, log all commands executed
set -o xtrace

# convert .raw12 to DNG
raw2dng $RAW_CHART --swap-lines --black=-50

# convert DNG to TIF
# options: no WB, no matrix, output colorspace raw, no gamma, linear 16-bit TIF, half-res
dcraw -r 1 1 1 1 -M -H 0 -o 0 -4 -T -h "${BASE_NAME}.DNG"

# read color data from the charts
$SCANIN -v -p -a -G 1.0 -dipn -F $MARKS "${BASE_NAME}.tiff" $CHT $CIE
mogrify diag.tif -normalize

# add synthetic black
python2 add_black.py ${BASE_NAME}.ti3

# compute the color profiles
$COLPROF -v -A "Apertus" -M "Axiom Beta" -qh -as -nc "$BASE_NAME"

# verify the results
$PROFCHECK -v2 -s ${BASE_NAME}.ti3 ${BASE_NAME}.icc

# create OCIO stanza
python2 icc2ocio.py ${BASE_NAME}.icc
