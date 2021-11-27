#!/bin/bash
input=$1 # not used currently

#todo: find first A-frame

startindex=2 # fixme

filescount=$(ls *.tif | wc -l)

while :
do
    Aframe="test_"$((startindex + i*2))".tif"
    Bframe="test_"$((startindex + i*2 + 1))".tif"

    if [ ! -f $Aframe ]; then
        break
    fi
    if [ ! -f $Bframe ]; then
        break
    fi

    echo "Aframe: $Aframe ";
    echo "Bframe: $Bframe ";

    montage $Bframe $Aframe -tile 2x1 -geometry +0+0 -depth 8 rgb:test_$i.raw12

    raw2dng test_$i.raw12 --width=3840 --height=2160 --bayer-order=1

    rm test_$i.raw12

    let "i+=1" 
done



