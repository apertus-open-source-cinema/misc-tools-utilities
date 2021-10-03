#!/usr/bin/python

from posixpath import abspath
import PySimpleGUI as sg
import os
import sys
import getopt
import shutil
import glob
from subprocess import PIPE, Popen
from threading import Thread
from queue import Queue, Empty

ON_POSIX = 'posix' in sys.builtin_module_names

videodevice = "/dev/video0"
total, used, free = shutil.disk_usage(os.getcwd())
space = free // (2**30)
clipindex = 0
q = False


def main(argv):
    global videodevice
    try:
        opts, args = getopt.getopt(argv, "d:h:",
                                   ["help", "video-device"])
    except getopt.GetoptError:
        print('recorder.py -d <video-device>')
        window.close()
    for opt, arg in opts:
        if opt in ("-h", "--help"):
            print('recorder.py -d <video-device>')
            sys.exit()
        elif opt in ("-d", "--video-device"):
            videodevice = arg.strip()

    print('Using Video Device: ', videodevice)


if __name__ == "__main__":
    main(sys.argv[1:])


def enqueue_output(out, queue):
    for line in iter(out.readline, b''):
        queue.put(line)
    out.close()

# Load the recorded clips


def update_recordings_list():
    directories = []

    for foldername in os.listdir("."):
        if os.path.isdir(foldername):
            # Check if there is a folder with an #.rgb file inside
            if (glob.glob(foldername + '/*.rgb')):
                # print(foldername)
                # check size of *.rgb file to estimate number of frames in it

                directories.append(foldername)
    try:
        window['-recordings-'].update(values=directories)
    except:
        window['-recordings-'].update(values=directories)


# Read and display information about a specific clip
def update_clip_info():
    frames_rgb = 0
    frames_extracted = False
    frames_converted_raw12 = False
    frames_converted_dng = False

    window['-clipinfo-'].update('Clipinfo:')

    if (window['-recordings-'].Widget.curselection()):
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
            0]]

        for filename in os.listdir(foldername):
            if filename.endswith(".rgb"):
                # How many frames are inside one big .rgb file
                frames_rgb = int(os.path.getsize(
                    foldername + '/' + filename)/6220800)

                # How many extracted *.bgr files are in that folder already
                stream = os.popen('ls ' + foldername + '/*.bgr | wc -l')
                bgr_frame_files_count = int(stream.read())

                # if all bgr files have been extracted disable the extract button
                if (bgr_frame_files_count == frames_rgb):
                    window['-extract-'].Update(disabled=True)
                else:
                    window['-extract-'].Update(disabled=False)

                # How many *.raw12 files are in that folder already
                stream = os.popen('ls ' + foldername + '/*.raw12 | wc -l')
                raw12_frame_files_count = int(stream.read())

                # How many *.DNG files are in that folder already
                stream = os.popen('ls ' + foldername + '/*.DNG | wc -l')
                dng_frame_files_count = int(stream.read())

                # Preview video present
                stream = os.popen('ls ' + foldername + '/' + foldername + '.mp4 | wc -l')
                preview_video = int(stream.read())
                if (preview_video):
                    preview_video_string = foldername + '/' + foldername + '.mp4'
                else:
                    preview_video_string = 'none'

                # Read and display information about a specific clip
                window['-clipinfo-'].update('Clipinfo:\nClip: ' + filename + '\ncontains A/B-Frames: ' + str(
                    frames_rgb) + '\nbgr A/B frames extracted: ' + str(bgr_frame_files_count) + '\nraw12 converted: ' +
                    str(raw12_frame_files_count) + '\nDNG converted: ' + str(dng_frame_files_count) + 
                    '\nPreview Video: ' + preview_video_string)


def get_rgb_file():
    foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
    for filename in os.listdir(foldername):
        if filename.endswith(".rgb"):
            return foldername + '/' + filename


layout = [[sg.Button('View Stream'), sg.Button('Start Recording')],
          [sg.Text('Free Disk Space: ' + str(space) + "GiB")],
          [sg.Text('Recording Directory: ')],
          [sg.Input(os.getcwd()), sg.FileBrowse()],
          [sg.Text('Recordings:')],
          [sg.Listbox(values=('Loading...', 'Listbox 2', 'Listbox 3'), size=(
              50, 10), key='-recordings-'), sg.Text('Clipinfo:\n', key='-clipinfo-')],
          [sg.Button('Reload Recordings'), sg.Button('Update Clipinfo'), sg.Button(
              'Extract Frames', key='-extract-'), sg.Button('Convert Frames', key='-convert-')],
          [sg.Button('Create Preview Video', key='-preview-'), sg.Button('Play Preview Video', key='-playpreview-'), sg.Button('Exit')]]

# Create the Window
window = sg.Window('AXIOM Recorder', layout, resizable=True,)

init = True

# Event Loop to process "events" and get the "values" of the inputs
while True:
    event, values = window.read()

    # read line without blocking
    if (q):
        try:
            line = q.get_nowait()  # or q.get(timeout=.1)
        except Empty:
            a=0
        #else:  # got line
            #print(line)

    if (init):
        update_recordings_list()  # fixme, does not work
        init = False
    if event == sg.WIN_CLOSED or event == 'Exit':  # if user closes window or clicks Exit
        break

    if event == 'View Stream':
        stream = os.popen('ffplay ' + videodevice)
        output = stream.read()

    if event == 'Reload Recordings':
        update_recordings_list()

    if event == 'Update Clipinfo':
        update_clip_info()

    if event == '-extract-':
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
            0]]
        print('splitting selected clip: ' + get_rgb_file())
        #print ('split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
        #           '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5')

        p = Popen(['split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
                   '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5'], shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
        q = Queue()
        t = Thread(target=enqueue_output, args=(p.stdout, q))
        t.daemon = True  # thread dies with the program
        t.start()

        # stream = os.popen('split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
        #                  '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5')
        #output = stream.read()

        update_clip_info()

    if event == '-convert-':
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
            0]]
        print('converting to raw12/dng selected clip: ' + get_rgb_file())
        #print('python3 bgr2dng.py ' + foldername + '/')
        #stream = os.popen('python3 bgr2dng.py ' + foldername + '/')
        #output = stream.read()

        p = Popen(['python3 bgr2dng.py ' + foldername + '/'], shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
        q = Queue()
        t = Thread(target=enqueue_output, args=(p.stdout, q))
        t.daemon = True  # thread dies with the program
        t.start()

        update_clip_info()

    if event == '-preview-':
        
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
            0]]

        p = Popen(['dcraw -T -h ' + foldername + '/*.DNG && ffmpeg -r 30 -i ' + foldername + '/' +
         foldername + '_%05d.tiff -c:v libx264 -vf fps=30 ' + foldername + '/' + foldername + '.mp4 && rm ' + foldername + '/*.tiff'],
          shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
        q = Queue()
        t = Thread(target=enqueue_output, args=(p.stdout, q))
        t.daemon = True  # thread dies with the program
        t.start()

    if event == '-playpreview-':
        
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
            0]]
        stream = os.popen('ffplay ' + foldername + '/' + foldername + '.mp4 -vf scale=960:-1')
        output = stream.read()

    if event == 'Start Recording':
        folderdir = "Clip_" + f'{clipindex:05d}'
        while 1:
            if not os.path.exists(folderdir):
                os.mkdir(folderdir)
                print("Directory ", folderdir,  " Created ")
                break
            else:
                print("Directory ", folderdir,  " already exists")
                clipindex += 1
                folderdir = "Clip_" + f'{clipindex:05d}'

        print('ffmpeg -i ' + videodevice + ' -map 0 ' +
              folderdir + '/' + folderdir + '.rgb')
        stream = os.popen('ffmpeg -i ' + videodevice +
                          ' -map 0 ' + folderdir + '/' + folderdir + '.rgb')
        output = stream.read()

window.close()
