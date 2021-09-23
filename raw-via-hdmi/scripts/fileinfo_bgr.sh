#!/bin/bash

filescount=$(ls *.bgr | wc -l)
echo "*.bgr files found in current folder: $filescount"


echo -e "=========================================="
echo -e "filename \t framecounter \t A/B frame"
echo -e "=========================================="

for f in *.bgr
do
    framecounter=$( dd if=$f bs=1 count=1 skip=2 status=none | od -An -vtu1)
    abframevalue=$( dd if=$f bs=1 count=1 status=none | od -An -vtu1)
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




