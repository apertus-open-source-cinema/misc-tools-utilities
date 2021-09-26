import os
import sys

stream = os.popen('ls *.bgr | wc -l')
filescount = stream.read()
print('bgr files found in current folder: ' + filescount)

print("==========================================================")
print("filename \t\t framecounter \t A/B frame")
print("==========================================================")

filenamelist = os.listdir(".")
filenamelist.sort()

previousframecounter = 0
first = True

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

            # generate md5 hash
            stream = os.popen('md5sum ' + filename)
            md5 = stream.read()
            md5 = md5.split()[0]

            # output table
            # don't report missmatch on 8 bit overflow
            if ((previousframecounter+1 == framecounter) | ((previousframecounter == 255) & (framecounter == 0))):
                print(filename + "\t\t" + str(framecounter) +
                      "\t\t" + abframe + "\t\t" + md5)
            else:
                if (first):  # don't report missmatch on first frame
                    print(filename + "\t\t" +
                          str(framecounter) + "\t\t" + abframe+ "\t\t" + md5)
                    first = False
                else:
                    print("\033[91m" + filename + "\t\t" + str(framecounter) +
                          "\t\t" + abframe + "\t\t"  + md5 + "\t\t framecounter missmatch\033[0m")

            previousframecounter = framecounter
    else:
        continue
