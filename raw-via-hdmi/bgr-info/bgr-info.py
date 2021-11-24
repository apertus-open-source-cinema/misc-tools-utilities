#!/usr/bin/python

# SPDX-FileCopyrightText: © 2021 Sebastian Pichelhofer <sp@apertus.org>
# SPDX-License-Identifier: GPL-2.0-or-later

import os
import sys
import getopt

version = 'V0.2'
resolution_width = 1920
resolution_height = 1080
skipHash = False

def print_help():
    print('This is bgr-info ' + version)
    print('')
    print('SYNOPSIS')
    print('\tbgr-info.py [parameters]')
    print('')
    print('EXAMPLE')
    print('\tbgr-info.py -w 2048 -h 1080 -i myfolder/')
    print('')
    print('OPTIONS')
    print('\t-w, --width:\t defines image resolution width (default: 1920)')
    print('\t-h, --height:\t defines image resolution height (default: 1080)')
    print('\t-i, --input:\t path to files that should be read')
    print('\t-s, --skip-hash:\t skip creating hash for every frame (significant speed up)')


def main(argv):
    global folder, resolution_height, resolution_width, skipHash

    try:
        opts, args = getopt.getopt(argv, "sh:w:h:i:", ["help", "width=", "height=", "input=", "skip-hash"])
    except getopt.GetoptError:
        print_help()
        sys.exit(2)

    for opt, arg in opts:
        if opt in ("-h", "--help"):
            print_help()
            sys.exit()
        elif opt in ("-w", "--width"):
            resolution_width = int(arg.strip())
            print('Parameter provided: Using resolution width: ' + str(resolution_width))
        elif opt in ("-h", "--height"):
            resolution_height = int(arg.strip())
            print('Parameter provided: Using resolution height: ' + str(resolution_height))
        elif opt in ("-s", "--skip-hash"):
            skipHash = True
            print('Parameter provided: skipping hash creation')
        elif opt in ("-i", "--input"):
            folder = arg.strip()
        else:
            print('Else: ', arg.strip())

if __name__ == "__main__":
    main(sys.argv[1:])

stream = os.popen('ls ' + folder + '/*.bgr | wc -l')
filescount = stream.read()
print('folder: ' + folder)
print('*.bgr files found in folder: ' + filescount)

print("==========================================================")
print("filename \t\t framecounter \t A/B frame")
print("==========================================================")

filenamelist = os.listdir(folder + "/.")
filenamelist.sort()

previousframecounter = 0
first = True
skippedFrames = 0
duplicateFrames = 0
filesize = resolution_width * resolution_height * 3

for filename in filenamelist:
    if filename.endswith(".bgr"):
        if ((os.path.getsize(folder + "/" + filename) > filesize - 100) & (os.path.getsize(folder + "/" + filename) < filesize + 100)):

            # check if corner markers are present and matching (A or B Frame indicator) on first image or throw error & exit
            if first:
                corner1 = os.popen('dd if=' + folder + "/" + filename +
                              ' bs=1 count=1 status=none | od -An -vtu1')
                corner1AB = corner1.read().strip("\n")

                corner2 = os.popen('dd if=' + folder + "/" + filename +
                              ' bs=1 count=1  skip=' + str((resolution_width-1)*3) + ' status=none | od -An -vtu1')
                corner2AB = corner2.read().strip("\n")

                corner3 = os.popen('dd if=' + folder + "/" + filename +
                              ' bs=1 count=1  skip=' + str((resolution_width * (resolution_height-1))*3) + ' status=none | od -An -vtu1')
                corner3AB = corner3.read().strip("\n")

                corner4 = os.popen('dd if=' + folder + "/" + filename +
                              ' bs=1 count=1  skip=' + str(filesize - 3) + ' status=none | od -An -vtu1')
                corner4AB = corner4.read().strip("\n")

                if (not (corner1AB == corner2AB == corner3AB == corner4AB)):
                    print ("problem with corner markers detected - please verify the provided resolution is correct")
                    sys.exit()

            # extract framecounter
            stream = os.popen('dd if=' + folder + "/" + filename +
                              ' bs=1 count=1 skip=2 status=none | od -An -vtu1')
            framecounter = int(stream.read().strip("\n"))

            # extract a/b frame
            stream = os.popen('dd if=' + folder + "/" + filename +
                              ' bs=1 count=1 status=none | od -An -vtu1')
            abframevalue = stream.read().strip("\n")
            abframe = ""
            if (int(abframevalue) == 85):
                abframe = "A-Frame"
            if (int(abframevalue) == 170):
                abframe = "B-Frame"

            # generate md5 hash
            if(not skipHash):
                stream = os.popen('md5sum ' + folder + "/" + filename)
                md5 = stream.read()
                md5 = md5.split()[0]
            else:
                md5 = ""

            # output table
            # don't report missmatch on 8 bit overflow
            if ((previousframecounter+1 == framecounter) | ((previousframecounter == 255) & (framecounter == 0))):
                print(filename + "\t\t" + str(framecounter) +
                      "\t\t" + abframe + "\t\t" + md5)
            else:
                if (first):  # don't report missmatch on first frame
                    print(filename + "\t\t" +
                          str(framecounter) + "\t\t" + abframe + "\t\t" + md5)
                    first = False
                else:
                    # skipped frame(s)
                    if (previousframecounter < framecounter):
                        skippedFrames += framecounter - previousframecounter

                    # duplicate frame(s)
                    if (previousframecounter == framecounter):
                        duplicateFrames += 1

                    print("\033[91m" + filename + "\t\t" + str(framecounter) +
                        "\t\t" + abframe + "\t\t" + md5 + "\t\t framecounter missmatch\033[0m")

            previousframecounter = framecounter
    else:
        continue


print("==========================================================")
print("Skipped Frames: " + str(skippedFrames))
print("Duplicate Frames: " + str(duplicateFrames))
print("==========================================================")
