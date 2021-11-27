#!/usr/bin/python

# SPDX-FileCopyrightText: © 2021 Sebastian Pichelhofer <sp@apertus.org>
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import sys
import getopt

folder = ''
previousframecounter = 0
first = True
frameindex = 0
fileindex = 0
version = 'V0.2'
resolution_width = 1920
resolution_height = 1080
target = 'raw12&dng'
clipname = "output"


def print_help():
    print('This is rgb-convert ' + version)
    print('')
    print('SYNOPSIS')
    print('\trgb-convert.py [parameters]')
    print('')
    print('EXAMPLE')
    print('\trgb-convert.py -w 2048 -h 1080 -i myfolder/')
    print('')
    print('OPTIONS')
    print('\t-w, --width:\t defines image resolution width (default: 1920)')
    print('\t-h, --height:\t defines image resolution height (default: 1080)')
    print('\t-i, --input:\t path to files that should be converted')
    print('\t-t, --target:\t options are: raw12, dng, raw12&dng (default: raw12&dng)')
    print('\t-n, --clipname:\t output clip file name')


def main(argv):
    global folder, resolution_height, resolution_width, target

    try:
        opts, args = getopt.getopt(argv, "h:w:h:t:i:n:", ["help", "width=", "height=", "target=", "input=", "clipname="])
    except getopt.GetoptError:
        print_help()
        sys.exit(2)

    for opt, arg in opts:
        if opt in ("-h", "--help"):
            print_help()
            sys.exit()
        elif opt in ("-w", "--width"):
            resolution_width = arg.strip()
            print('Parameter provided: Using resolution width: ' + resolution_width)
        elif opt in ("-h", "--height"):
            resolution_height = arg.strip()
            print('Parameter provided: Using resolution height: ' + resolution_height)
        elif opt in ("-t", "--target"):
            target = arg.strip()
            print('Target: ', target)
        elif opt in ("-n", "--clipname"):
            clipname = arg.strip()
            print('Target: ', target)
        elif opt in ("-i", "--input"):
            folder = arg.strip()
        else:
            print('Else: ', arg.strip())

if __name__ == "__main__":
    main(sys.argv[1:])

if (folder == ''):
    print('No input-folder specified')
    print('Exiting...')
    print()
    print_help()
    sys.exit()

stream = os.popen('ls ' + folder + '*.frame | wc -l')
filescount = stream.read().strip('\n')

print('Configuration:')
print('')
print('folder:\t\t\t\t', folder)
print('frames found in folder:\t' + filescount)       
print('target:\t\t\t\t' + target)
print('clipname:\t\t\t\t' + clipname)
print('source resolution width:\t' + str(resolution_width))
print('source resolution height:\t' + str(resolution_height))
print('target resolution width:\t' + str(resolution_width*2))
print('target resolution height:\t' + str(resolution_height*2))
print('')

filenamelist = os.listdir(folder + '.')
filenamelist.sort()

frame_size = resolution_width * resolution_height * 24 / 8

for filename in filenamelist:
    if filename.endswith(".frame"):
        if ((os.path.getsize(folder + filename) > frame_size - 100) & (os.path.getsize(folder + filename) < frame_size + 100)):

            # extract framecounter
            stream = os.popen('dd if=' + folder + filename + ' bs=1 count=1 status=none | od -An -vtu1')
            framecounter = int(stream.read().strip("\n"))

            # extract a/b frame
            stream = os.popen('dd if=' + folder + filename + ' bs=1 count=1  skip=2 status=none | od -An -vtu1')
            abframevalue = stream.read().strip("\n")
            abframe = ""
            if (int(abframevalue) == 85):
                abframe = "A-Frame"
            if (int(abframevalue) == 170):
                abframe = "B-Frame"

            # convert every pair if an A-Frame exists
            if (abframe == "A-Frame"):

                # extract framecounter
                stream = os.popen('dd if=' + folder + filenamelist[fileindex+1] + ' bs=1 count=1 status=none | od -An -vtu1')
                framecounternext = int(stream.read().strip("\n"))

                #only continue if a B frame exists that contains a framecounter that is one value highe than the A-frame
                if ((framecounternext == framecounter + 1) | ((framecounternext == 0) & (framecounter == 255))):
                    print(str(framecounter) + ':\t' + folder + filenamelist[fileindex] + '\t&\t' + str(framecounternext) + ':\t' + folder + filenamelist[fileindex+1] +
                          '\t-> ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')

                    # write raw12
                    #stream = os.popen('montage -size ' + str(resolution_width) + 'x' + str(resolution_height) + ' -depth 8 ' + folder + filenamelist[fileindex] + ' ' + folder + filenamelist[fileindex+1] +
                    #                  ' -tile 2x1 -geometry +0+0 rgb:' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')
                    stream = os.popen('rgb-merge ' + folder + filenamelist[fileindex] + ' ' + folder + filenamelist[fileindex+1] +
                                      ' ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')
                    print(stream.read())

                    # write dng
                    if (target != "raw12"):
                        print            ('raw2dng ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12 --width=' + str(resolution_width*2) + ' --height=' + str(resolution_height*2))
                        stream = os.popen('raw2dng ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12 --width=' + str(resolution_width*2) + ' --height=' + str(resolution_height*2))
                        print(stream.read())

                    # remove raw12
                    if (target == "dng"):
                        print('rm ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')
                        stream = os.popen('rm ' + folder + clipname.strip('/') + f'_{frameindex:05}' + '.raw12')
                        stream.read()
                        print('')

                    frameindex = frameindex + 1

            previousframecounter = framecounter
            #frameindex = frameindex + 1
        fileindex = fileindex + 1
    else:
        fileindex = fileindex + 1
        continue
