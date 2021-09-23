#!/bin/bash
input=$1

#todo: find first A-frame
skip=$2 # improve me

filescount=$(ls *.bgr | wc -l)
i=0

while :
do
  #  size=$( wc -c $f | awk '{print $1}')
 #   if [[ ( "$size" > 6220000 ) && ("$size" < 6220900 ) ]] ; then
    printf -v aframeindex "%05d" $((skip + i*2))
    printf -v bframeindex "%05d" $((skip + i*2 + 1))

    Aframe="$input$aframeindex.bgr"
    Bframe="$input$bframeindex.bgr"

    # check if file exists
    if [ ! -f $Aframe ]; then
        break
    fi

    # check if file exists
    if [ ! -f $Bframe ]; then
        break
    fi

    echo "A-frame: $Aframe ";
    echo "B-frame: $Bframe ";

    montage -size 1920x1080 -depth 8 $Aframe $Bframe -tile 2x1 -geometry +0+0 rgb:$input$i.raw12
    raw2dng $input$i.raw12 --width=3840 --height=2160 --bayer-order=2

    rm $input$i.raw12

    let "i+=1" 
done



