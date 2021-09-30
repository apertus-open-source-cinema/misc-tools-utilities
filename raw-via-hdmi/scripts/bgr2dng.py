import os
import sys

folder = sys.argv[1]

stream = os.popen('ls ' + folder + '*.bgr | wc -l')
filescount = stream.read()
print('bgr files found in current folder: ' + filescount)

filenamelist = os.listdir(folder + '.')
filenamelist.sort()

previousframecounter = 0
first = True
frameindex = 0
fileindex = 0
clipname = folder

for filename in filenamelist:
    if filename.endswith(".bgr"):
        if ((os.path.getsize(folder + filename) > 6220700) & (os.path.getsize(folder + filename) < 6220900)):

            # extract framecounter
            stream = os.popen('dd if=' + folder + filename +
                              ' bs=1 count=1 skip=2 status=none | od -An -vtu1')
            framecounter = int(stream.read().strip("\n"))

            # extract a/b frame
            stream = os.popen('dd if=' + folder + filename +
                              ' bs=1 count=1 status=none | od -An -vtu1')
            abframevalue = stream.read().strip("\n")
            abframe = ""
            if (int(abframevalue) == 85):
                abframe = "A-Frame"
            if (int(abframevalue) == 170):
                abframe = "B-Frame"

            # convert every pair if an A-Frame exists
            if (abframe == "A-Frame"):
                # extract framecounter
                stream = os.popen('dd if=' + folder + filenamelist[fileindex+1] + ' bs=1 count=1 skip=2 status=none | od -An -vtu1')
                framecounternext = int(stream.read().strip("\n"))

                #only continue if a B frame exists that has a one value higher framecounter  
                if ((framecounternext == framecounter + 1) | ((framecounternext == 0) & (framecounter == 255))):
                    print(str(framecounter) + ':\t' + folder + filenamelist[fileindex] + '\t&\t' + str(framecounternext) + ':\t' + folder + filenamelist[fileindex+1] +
                          '\t-> ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')

                    # write raw12
                    stream = os.popen('montage -size 1920x1080 -depth 8 ' + folder + filenamelist[fileindex] + ' ' + folder + filenamelist[fileindex+1] +
                                      ' -tile 2x1 -geometry +0+0 rgb:' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')
                    stream.read()

                    # write dng
                    print('raw2dng ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12 --width=3840 --height=2160')
                    stream = os.popen('raw2dng ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12 --width=3840 --height=2160')
                    stream.read()

                    # remove raw12
                    #print('rm ' + clipname + f'{frameindex:05}' + '.raw12')
                    #stream = os.popen('rm ' + clipname + f'{frameindex:05}' + '.raw12')
                    #stream.read()

                    frameindex = frameindex + 1
            previousframecounter = framecounter
            #frameindex = frameindex + 1
        fileindex = fileindex + 1
    else:
        fileindex = fileindex + 1
        continue
