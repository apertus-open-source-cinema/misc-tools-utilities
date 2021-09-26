import os
import sys

stream = os.popen('ls *.bgr | wc -l')
filescount = stream.read()
print('bgr files found in current folder: ' + filescount)

filenamelist = os.listdir(".")
filenamelist.sort()

previousframecounter = 0
first = True
frameindex = 0
fileindex = 0
clipname = "Test_"

for filename in filenamelist:
    if filename.endswith(".bgr"):
        if ((os.path.getsize(filename) > 6220700) & (os.path.getsize(filename) < 6220900)):

            # extract framecounter
            stream = os.popen('dd if=' + filename +
                              ' bs=1 count=1 skip=2 status=none | od -An -vtu1')
            framecounter = int(stream.read().strip("\n"))

            # extract a/b frame
            stream = os.popen('dd if=' + filename +
                              ' bs=1 count=1 status=none | od -An -vtu1')
            abframevalue = stream.read().strip("\n")
            abframe = ""
            if (int(abframevalue) == 85):
                abframe = "A-Frame"
            if (int(abframevalue) == 170):
                abframe = "B-Frame"

            if (abframe == "A-Frame"):
                # extract framecounter
                stream = os.popen('dd if=' + filenamelist[fileindex+1] + ' bs=1 count=1 skip=2 status=none | od -An -vtu1')
                framecounternext = int(stream.read().strip("\n"))

                if ((framecounternext == framecounter + 1) | ((framecounternext == 0) & (framecounter == 255))):
                    print(str(framecounter) + ':\t' + filenamelist[fileindex] + '\t&\t' + str(framecounternext) + ':\t' + filenamelist[fileindex+1] +
                          '\t-> ' + clipname + f'{frameindex:05}' + '.raw12')

                    # write raw12
                    stream = os.popen('montage -size 1920x1080 -depth 8 ' + filenamelist[fileindex] + ' ' + filenamelist[fileindex+1] +
                                      ' -tile 2x1 -geometry +0+0 rgb:' + clipname + f'{frameindex:05}' + '.raw12')
                    stream.read()

                    # write dng
                    print('raw2dng ' + clipname + f'{frameindex:05}' + '.raw12 --width=3840 --height=2160')
                    stream = os.popen('raw2dng ' + clipname + f'{frameindex:05}' + '.raw12 --width=3840 --height=2160')
                    stream.read()

                    # remove raw12
                    print('rm ' + clipname + f'{frameindex:05}' + '.raw12')
                    stream = os.popen('rm ' + clipname + f'{frameindex:05}' + '.raw12')
                    stream.read()

                    frameindex = frameindex + 1
            previousframecounter = framecounter
            #frameindex = frameindex + 1
        fileindex = fileindex + 1
    else:
        fileindex = fileindex + 1
        continue
