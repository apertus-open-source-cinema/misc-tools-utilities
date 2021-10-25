#!/usr/bin/python

import getopt
import glob
import os
import subprocess
import shutil
import sys
import json
from queue import Queue
from subprocess import PIPE, Popen
from threading import Thread

import PySimpleGUI as sg

ON_POSIX = 'posix' in sys.builtin_module_names

# config data
data = {}

video_device = "/dev/video0"
total, used, free = shutil.disk_usage(os.getcwd())
space = free // (2 ** 30)
clip_index = 0
q = False
window = None
current_stream_process = None

# load configs from last session
def safetofile():
    with open('recorder.json', 'w') as f:
        json.dump(data, f)

# load configs from last session
f = open('recorder.json')
data = json.load(f)

if 'inputfolder' in data:
    print('found inputfolder: ' + data['inputfolder'])
else:
    print('no input folder found in config, loading default')
    data['inputfolder'] = os.getcwd()


def enqueue_output(out, queue):
    for line in iter(out.readline, b''):
        queue.put(line)
    out.close()


# Load the recorded clips


def update_recordings_list():
    directories = []

    for foldername in os.listdir(window['-inputfolder-'].get()):
        if os.path.isdir(foldername):
            # Check if there is a folder with an #.rgb file inside
            if glob.glob(foldername + '/*.rgb'):
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

    if window['-recordings-'].Widget.curselection():
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
            0]]

        for filename in os.listdir(foldername):
            if filename.endswith(".rgb"):
                # How many frames are inside one big .rgb file
                frames_rgb = int(os.path.getsize(
                    foldername + '/' + filename) / 6220800)

                # How many extracted *.bgr files are in that folder already
                p1 = subprocess.Popen('ls ' + foldername + '/*.bgr | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate() 
                bgr_frame_files_count = int(out)
                p1.stdout.close()


                # if all bgr files have been extracted disable the extract button
                if bgr_frame_files_count == frames_rgb:
                    window['-extract-'].Update(disabled=True)
                else:
                    window['-extract-'].Update(disabled=False)

                # How many *.raw12 files are in that folder already
                p1 = subprocess.Popen('ls ' + foldername + '/*.raw12 | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate() 
                raw12_frame_files_count = int(out)
                p1.stdout.close()

                # How many *.DNG files are in that folder already
                p1 = subprocess.Popen('ls ' + foldername + '/*.DNG | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate() 
                dng_frame_files_count = int(out)
                p1.stdout.close()

                # Preview video present?
                p1 = subprocess.Popen('ls ' + foldername + '/*.mp4 | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate() 
                preview_video = int(out)
                p1.stdout.close()
                if preview_video:
                    preview_video_string = foldername + '/' + foldername + '.mp4'
                else:
                    preview_video_string = 'none'

                # Read and display information about a specific clip
                window['-clipinfo-'].update('Clip: ' + filename + '\ncontains A/B-Frames (bgr): ' + str(
                    frames_rgb) + '\nA/B frames (bgr) extracted: ' + str(
                    bgr_frame_files_count) + '\nraw12 converted: ' +
                                            str(raw12_frame_files_count) + '\nDNG converted: ' + str(
                    dng_frame_files_count) +
                                            '\nPreview Video: ' + preview_video_string)


def get_rgb_file():
    foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
    for filename in os.listdir(foldername):
        if filename.endswith(".rgb"):
            return foldername + '/' + filename


def view_stream():
    stream = os.popen('ffplay ' + video_device)
    stream.read()


record_button = None


def handle_recording():
    if current_stream_process is None:
        start_recording()
        record_button.update(image_filename="images/recording_button.png")
    else:
        stop_recording()
        record_button.update(image_filename="images/record_button.png")


def start_recording():
    global clip_index
    folderdir = "Clip_" + f'{clip_index:05d}'
    while 1:
        if not os.path.exists(folderdir):
            os.mkdir(folderdir)
            print("Directory ", folderdir, " Created ")
            break
        else:
            print("Directory ", folderdir, " already exists")
            clip_index += 1
            folderdir = "Clip_" + f'{clip_index:05d}'

    print('ffmpeg -i ' + video_device + ' -map 0 ' +
          folderdir + '/' + folderdir + '.rgb')
    global current_stream_process
    current_stream_process = Popen('ffmpeg -i ' + video_device +
                                   ' -map 0 ' + folderdir + '/' + folderdir + '.rgb', shell=True)
    print("Recording started")


def stop_recording():
    global current_stream_process
    if current_stream_process is not None:
        current_stream_process.kill()

    print("Recording stopped")
    current_stream_process = None
    update_recordings_list()


def setup():
    # Create the Window
    global record_button
    record_button = sg.Button('', key=handle_recording, button_color=sg.TRANSPARENT_BUTTON,
                              image_filename="images/record_button.png", size=(120, 60), border_width=0)

    layout = [[sg.Text('AXIOM Beta HDMI Raw Recorder', font=("Helvetica", 25))],
              [sg.Text('AXIOM Beta IP: ')],
              [sg.Input(data['beta_ip'], key='-beta_ip-', enable_events=True), sg.Button('Test Connection', key='-test-ssh-connection-')],
              [sg.Button('View Stream', key=view_stream),
               record_button],
              [sg.Text('Recording Directory: ')],
              [sg.Input(data['inputfolder'], key='-inputfolder-', enable_events=True), sg.FolderBrowse(target='-inputfolder-', initial_folder=data['inputfolder'])],
              [sg.Text('Recordings:'), sg.Button('Reload', key="Reload Recordings")],
              [sg.Listbox(values=('Loading...', 'Listbox 2', 'Listbox 3'), size=(
                  50, 10), key='-recordings-', enable_events=True), sg.Text('Clipinfo:\n', key='-clipinfo-')],
              [sg.Text('Free Disk Space: ' + str(space) + "GiB")],
              [sg.Button('Update Clipinfo'), sg.Button('Extract Frames', key='-extract-')],
              [sg.Text('Convert to:'),
               sg.Combo(['raw12', 'dng', 'raw12&dng'], default_value='raw12&dng', key='-conversion-target-'),
               sg.Button('Convert Frames', key='-convert-')],
              [sg.Button('Create Preview Video', key='-preview-'),
               sg.Button('Play Preview Video', key='-playpreview-')],
              [sg.Button('Exit')]]

    global window
    window = sg.Window('AXIOM Recorder', layout, resizable=True, finalize=True)

    update_recordings_list()


def main_loop():
    global clip_index
    # Event Loop to process "events" and get the "values" of the inputs
    while True:
        event, values = window.read()

        if callable(event):
            event()

         # if user closes window or clicks Exit
        if event == sg.WIN_CLOSED or event == 'Exit':  
            safetofile()
            break

        if event == '-recordings-':
            update_clip_info()

        if event == '-inputfolder-':
            data['inputfolder'] = window['-inputfolder-'].get()
            update_recordings_list()

        if event == 'Reload Recordings':
            update_recordings_list()

        if event == 'Update Clipinfo':
            update_clip_info()

        if event == '-beta-ip-':
            data['beta_ip'] = window['-beta_ip-'].get()

        if event == '-test-ssh-connection-':
            # todo: test ssh connection to beta
            # for now we do a ping
            print ('Testing SSH connection: ' + window['-beta_ip-'].get())
            stream = os.popen('ping ' + window['-beta-ip-'].get() + ' -c 1')
            print(stream.read())

        if event == '-extract-':
            foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
                0]]
            print('splitting selected clip: ' + get_rgb_file())
            # print ('split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
            #           '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5')

            p = Popen(['split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
                       '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5'], shell=True, stdout=PIPE,
                      bufsize=1, close_fds=ON_POSIX)
            q = Queue()
            t = Thread(target=enqueue_output, args=(p.stdout, q))
            t.daemon = True  # thread dies with the program
            t.start()

            # stream = os.popen('split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
            #                  '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5')
            # output = stream.read()

            update_clip_info()

        if event == '-convert-':
            target = (window['-conversion-target-'].Get())
            foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
            print('converting selected clip: ' + get_rgb_file())
            # print('python3 bgr-convert.py -i ' + foldername + '/ -t ' + target)

            p = Popen(['python3 bgr-convert.py -i ' + foldername + '/ -t ' + target], shell=True, stdout=PIPE,
                      bufsize=1,
                      close_fds=ON_POSIX)
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

        # if event == 'Start Recording':
        #     folderdir = "Clip_" + f'{clip_index:05d}'
        #     while 1:
        #         if not os.path.exists(folderdir):
        #             os.mkdir(folderdir)
        #             print("Directory ", folderdir, " Created ")
        #             break
        #         else:
        #             print("Directory ", folderdir, " already exists")
        #             clip_index += 1
        #             folderdir = "Clip_" + f'{clip_index:05d}'
        #
        #     print('ffmpeg -i ' + video_device + ' -map 0 ' +
        #           folderdir + '/' + folderdir + '.rgb')
        #     stream = os.popen('ffmpeg -i ' + video_device +
        #                       ' -map 0 ' + folderdir + '/' + folderdir + '.rgb')
        #     output = stream.read()


def main(argv):
    global video_device, window
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
            video_device = arg.strip()

    print('Using Video Device: ', video_device)

    setup()
    main_loop()
    # window.close()


if __name__ == "__main__":
    main(sys.argv[1:])
