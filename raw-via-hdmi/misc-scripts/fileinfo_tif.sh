#!/bin/bash
input=$1

filescount=$(ls *.tif | wc -l)

echo "tif files found in current folder: $filescount"
echo -e "=========================================="
echo -e "filename \t framecounter \t A/B frame"
echo -e "=========================================="

for f in *.tif
do
    framecounter=$(convert $f[1x1+0+0] -format "%[fx:int(255*r)]" info:)
    abframevalue=$(convert $f[1x1+0+0] -format "%[fx:int(255*b)]" info:)
    if [ $abframevalue -eq 85 ]
    then
        abframe="A-Frame"
    fi
    if [ $abframevalue -eq 170 ]
    then
        abframe="B-Frame"
    fi
    echo -e "$f \t $framecounter \t\t $abframe"
done




