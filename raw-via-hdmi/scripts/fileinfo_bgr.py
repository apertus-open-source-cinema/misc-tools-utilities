import os
import sys

stream = os.popen('ls *.bgr | wc -l')
filescount = stream.read()
print('bgr files found in current folder: ' + filescount)

print("==========================================================")
print("filename \t\t\t framecounter \t A/B frame")
print("==========================================================")

filenamelist = os.listdir(".")
filenamelist.sort()

previousframecounter = 0

for filename in filenamelist:
    if filename.endswith(".bgr"):
        if ((os.path.getsize(filename) > 6220700) & (os.path.getsize(filename) < 6220900)):

            # extract framecounter
            stream = os.popen('dd if=' + filename + ' bs=1 count=1 skip=2 status=none | od -An -vtu1')
            framecounter = int(stream.read().strip("\n"))

            # extract a/b frame
            stream = os.popen('dd if=' + filename + ' bs=1 count=1 status=none | od -An -vtu1')
            abframevalue = stream.read().strip("\n")
            abframe = ""
            if (int(abframevalue) == 85):
                abframe = "A-Frame"
            if (int(abframevalue) == 170):
                abframe = "B-Frame"

            # output table
            if ((previousframecounter+1 == framecounter) | ((previousframecounter == 255) & (framecounter == 0))):
                print(filename + "\t\t" + str(framecounter) + "\t" + abframe)
            else:
                print(filename + "\t\t" + str(framecounter) + "\t" + abframe + "\t framecounter missmatch" )

            previousframecounter = framecounter
    else:
        continue
