#!/usr/bin/python

# SPDX-FileCopyrightText: © 2021 Sebastian Pichelhofer <sp@apertus.org>
# SPDX-FileCopyrightText: © 2021 Andrej Balyschew <red.falcon1983@googlemail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

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
import time
import PySimpleGUI as sg

ON_POSIX = 'posix' in sys.builtin_module_names

# config data
data = {}

# clip analysis data
analysis = {}

video_device = "/dev/video0"
total, used, free = shutil.disk_usage(os.getcwd())
space = free // (2 ** 30)
clip_index = 0
q = False
window = None
current_stream_process = None
last_recorded_clip = ""
gain_options = [1, 2, 3, 4]
gain_index = 0
shutter_options = ["1/696", "1/384", "1/192", "1/96","1/48", "1/32"]
shutter_values = [1.4, 2.6, 5.2, 10.41, 20.833, 31.25]
shutter_index = 5
hdr_slopes = 1
hdr_exp1 = 0
hdr_exp2 = 0
hdr_vtl2 = 0
hdr_vtl3 = 0

# load configs from last session


def safe_config_to_file():
    with open('recorder.json', 'w') as f:
        json.dump(data, f)


# check if recorder.json exists, create default one if not
if not os.path.exists('recorder.json'):
    data['beta_ip'] = "192.168.0.9"
    safe_config_to_file()

# load configs from last session
f = open('recorder.json')
data = json.load(f)

if 'inputfolder' in data:
    print('found inputfolder: ' + data['inputfolder'])
else:
    print('no input folder found in config, loading default')
    data['inputfolder'] = os.getcwd()

if 'recorderfolder' in data:
        print('found recorderfolder: ' + data['recorderfolder'])
else:
    print('no recorder folder found in config, loading default')
    data['recorderfolder'] = os.getcwd()


def enqueue_output(out, queue):
    for line in iter(out.readline, b''):
        queue.put(line)
    out.close()


def current_milli_time():
    return round(time.time() * 1000)


# Load the recorded clips
def update_recordings_list():
    directories = []

    for foldername in os.listdir(window['-inputfolder-'].get()):
        #print (window['-inputfolder-'].get() + "/" + foldername)
        if os.path.isdir(window['-inputfolder-'].get() + "/" + foldername):
            # print(foldername)
            # Check if there is a folder with an #.rgb file inside
            if glob.glob(window['-inputfolder-'].get() + "/" + foldername + '/*.rgb'):
                # print(foldername)
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
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
        if os.path.exists(window['-inputfolder-'].get() + '/' + foldername + "/analysis.json"):
            f = open(window['-inputfolder-'].get() + '/' + foldername + '/analysis.json')
            analysis = json.load(f)
        else:
            foldername = window['-inputfolder-'].get() + '/' + window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
            command = 'cd ../frame-info/ ; python3 frame-info.py -r -i ' + foldername + '/ > ' + foldername + '/analysis.json'
            print (command)
            p = Popen([command], shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
            q = Queue()
            t = Thread(target=enqueue_output, args=(p.stdout, q))
            t.daemon = True  # thread dies with the program
            t.start()
            print("analysis.json written")

        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
        for filename in os.listdir(window['-inputfolder-'].get() + "/" + foldername):
            if filename.endswith(".rgb"):
                #TODO check if there is more than 1 *.rgb file in that folder: throw warning

                # How many frames are inside one big .rgb file
                frames_rgb = int(os.path.getsize(window['-inputfolder-'].get() + "/" + foldername + '/' + filename) / 6220800)

                # How many extracted *.frame files are in that folder already
                p1 = subprocess.Popen('ls ' + window['-inputfolder-'].get() + "/" + foldername +
                                      '/*.frame | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate()
                bgr_frame_files_count = int(out)
                p1.stdout.close()

                # if all bgr files have been extracted disable the extract button
                if bgr_frame_files_count == frames_rgb:
                    window['-extract-'].Update(disabled=True)
                else:
                    window['-extract-'].Update(disabled=False)

                # How many *.raw12 files are in that folder already
                p1 = subprocess.Popen('ls ' + window['-inputfolder-'].get() + "/" + foldername +
                                      '/*.raw12 | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate()
                raw12_frame_files_count = int(out)
                p1.stdout.close()

                # How many *.DNG files are in that folder already
                p1 = subprocess.Popen('ls ' + window['-inputfolder-'].get() + "/" + foldername +
                                      '/*.DNG | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate()
                dng_frame_files_count = int(out)
                p1.stdout.close()

                # Preview video present?
                p1 = subprocess.Popen('ls ' + window['-inputfolder-'].get() + "/" + foldername +
                                      '/*.mp4 | wc -l', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                out, err = p1.communicate()
                preview_video = int(out)
                p1.stdout.close()
                if preview_video:
                    preview_video_string = foldername + '.mp4'
                else:
                    preview_video_string = 'none'

                # Read and display information about a specific clip
                if 'analysis' in locals():
                    window['-clipinfo-'].update('Clip: ' + filename + '\ncontains A/B-Frames: ' + str(frames_rgb) + '\nA/B frames (rgb) extracted: ' + 
                        str(bgr_frame_files_count) + '\nraw12 converted: ' + str(raw12_frame_files_count) + '\nDNG converted: ' + 
                        str(dng_frame_files_count) + '\nSkipped Frames: ' + analysis['skippedFrames'] + '\nDuplicate Frames: ' + analysis['duplicateFrames'])


def get_rgb_file():
    foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
    for filename in os.listdir(window['-inputfolder-'].get() + '/' + foldername):
        if filename.endswith(".rgb"):
            return window['-inputfolder-'].get() + '/' + foldername + '/' + filename


def view_stream():
    stream = os.popen('ffplay ' + video_device)
    stream.read()


def shutter_inc():
    global shutter_index, shutter_options, data, shutter_values
    shutter_index += 1 
    if shutter_index > 5:
        shutter_index = 5
    window['-shutter-'].update('Shutter: ' + str(shutter_options[shutter_index]))
    stream = os.popen('ssh root@' + data['beta_ip'] + ' "axiom_snap -e '+ str(shutter_values[shutter_index]) + 'ms -z"')
    print(stream.read())


def shutter_dec():
    global shutter_index, shutter_options, data, shutter_values
    shutter_index -= 1 
    if shutter_index < 0:
        shutter_index = 0
    window['-shutter-'].update('Shutter: ' + str(shutter_options[shutter_index]))
    stream = os.popen('ssh root@' + data['beta_ip'] + ' "axiom_snap -e '+ str(shutter_values[shutter_index]) + 'ms -z"')
    print(stream.read())


def gain_inc():
    global gain_index, gain_options, data
    gain_index +=1 
    if gain_index > 3:
        gain_index = 3
    window['-gain-'].update('Gain: ' + str(gain_options[gain_index]))
    stream = os.popen('ssh root@' + data['beta_ip'] + ' "axiom_set_gain.sh ' + str(gain_options[gain_index]) + '"')
    print(stream.read())


def gain_dec():
    global gain_index, gain_options, data
    gain_index -=1 
    if gain_index < 0:
        gain_index = 0
    window['-gain-'].update('Gain: ' + str(gain_options[gain_index]))
    stream = os.popen('ssh root@' + data['beta_ip'] + ' "axiom_set_gain.sh ' + str(gain_options[gain_index]) + '"')
    print(stream.read())


def hdr_slopes_dec():
    global hdr_slopes
    hdr_slopes -= 1
    if hdr_slopes < 1:
        hdr_slopes = 1
    window['-hdr-slopes-'].update('HDR Slopes: ' + str(hdr_slopes))
    stream = os.popen('ssh root@' + data['beta_ip'] + ' "axiom_cmv_reg 79 ' + str(hdr_slopes) + '"')
    print(stream.read())

def hdr_slopes_inc():
    global hdr_slopes
    hdr_slopes += 1
    if hdr_slopes > 3:
        hdr_slopes = 3
    window['-hdr-slopes-'].update('HDR Slopes: ' + str(hdr_slopes))
    stream = os.popen('ssh root@' + data['beta_ip'] + ' "axiom_cmv_reg 79 ' + str(hdr_slopes) + '"')
    print(stream.read())


record_button = None


def handle_recording():
    if current_stream_process is None:
        start_recording()
        record_button.update(image_filename="images/recording_button.png")
    else:
        stop_recording()
        record_button.update(image_filename="images/record_button.png")


def start_recording():
    global clip_index, last_recorded_clip
    folderdir = window['-inputfolder-'].get() + "/Clip_" + f'{clip_index:05d}'
    while 1:
        if not os.path.exists(folderdir):
            os.mkdir(folderdir)
            print("Directory ", folderdir, " Created ")
            break
        else:
            print("Directory ", folderdir, " already exists")
            clip_index += 1
            folderdir = window['-inputfolder-'].get() + "/Clip_" + f'{clip_index:05d}'

    print('ffmpeg -i ' + video_device + ' -map 0 -pix_fmt rgb24 ' +
          folderdir + '/' + 'Clip_' + f'{clip_index:05d}' + '.rgb')
    global current_stream_process
    current_stream_process = Popen('exec ffmpeg -i ' + video_device +
                                   ' -map 0 -pix_fmt rgb24 ' + folderdir + '/' + 'Clip_' + f'{clip_index:05d}' + '.rgb', shell=True)
    print("Recording started")
    last_recorded_clip = folderdir + '/' + 'Clip_' + f'{clip_index:05d}' + '.rgb'


def stop_recording():
    global current_stream_process, last_recorded_clip
    if current_stream_process is not None:
        current_stream_process.kill()
        current_stream_process.wait()

    print("Recording stopped")
    current_stream_process = None

    print('Downloading Sensor Registers from: ' + window['-beta-ip-'].get())
    write_sensor_registers(window['-beta-ip-'].get(), last_recorded_clip + ".registers")
    update_recordings_list()


def write_sensor_registers(BetaIP, filepath):
    stream = os.popen('ssh root@' + BetaIP + ' "axiom_snap -E -r -z" > ' + filepath)
    print(stream.read())


def setup():
    # Create the Window
    global record_button
    record_button = sg.Button('', key=handle_recording, button_color=sg.TRANSPARENT_BUTTON,
                              image_filename="images/record_button.png", size=(120, 60), border_width=0)

    sg.theme('Reddit')

    layout = [[sg.Text('AXIOM Beta IP: '), sg.Input(data['beta_ip'], key='-beta-ip-', size=(16, 10), enable_events=True), sg.Button('Test Connection', key='-test-ssh-connection-'),
              sg.Button('Init Raw Mode', key='-init-beta-raw-'), sg.Button('Download Sensor Registers', key='-sensor-registers-')],
              [sg.Button('-', key=gain_dec), sg.Text('Gain: 1', key='-gain-'), sg.Button('+', key=gain_inc), 
              sg.Button('-', key=shutter_dec), sg.Text('Shutter: 1/32', key='-shutter-'), sg.Button('+', key=shutter_inc),
              sg.Button('-', key=hdr_slopes_dec), sg.Text('HDR Slopes: 1', key='-hdr-slopes-'), sg.Button('+', key=hdr_slopes_inc),
              sg.Button('View Stream', key=view_stream), record_button],
              [sg.Text('Recording Directory: '), sg.Input(data['inputfolder'], key='-inputfolder-', enable_events=True),
               sg.FolderBrowse(target='-inputfolder-', initial_folder=data['inputfolder'])],
              [sg.Text('Recordings:'), sg.Button('Reload', key="-reload-recordings-")],
              [sg.Listbox(values=('Loading...', 'Listbox 2', 'Listbox 3'), size=(
                  30, 5), key='-recordings-', enable_events=True), sg.Text('Clipinfo:\n', key='-clipinfo-')],
              [sg.Text('Free Disk Space: ' + str(space) + "GiB"), sg.Button('Update Clipinfo'), sg.Button('Extract Frames', key='-extract-'),
              sg.Text('Convert to:'), sg.Combo(['raw12', 'dng', 'raw12&dng'],
                        default_value='raw12&dng', key='-conversion-target-'),
               sg.Button('Convert Frames', key='-convert-')],
              [sg.Text('Recorder Directory: '), sg.Input(data['recorderfolder'], key='-recorderfolder-', size=(35,10), enable_events=True),
               sg.FolderBrowse(target='-recorderfolder-', initial_folder=data['recorderfolder']), sg.Button('Show Preview', key='-show-preview-')]]

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
            safe_config_to_file()
            break

        if event == '-recordings-':
            update_clip_info()

        if event == '-inputfolder-':
            data['inputfolder'] = window['-inputfolder-'].get()
            update_recordings_list()

        if event == '-reload-recordings-':
            update_recordings_list()

        if event == 'Update Clipinfo':
            update_clip_info()

        if event == '-beta-ip-':
            data['beta_ip'] = window['-beta-ip-'].get()

        if event == '-recorderfolder-':
            data['recorderfolder'] = window['-recorderfolder-'].get()

        if event == '-test-ssh-connection-':
            # todo: test ssh connection to beta
            # for now we just do a ping
            print('Testing SSH connection: ' + window['-beta-ip-'].get())
            stream = os.popen('ping ' + window['-beta-ip-'].get() + ' -c 1')
            print(stream.read())

        if event == '-init-beta-raw-':
            print('Connecting to camera and initiatng raw output mode: ' +
                  window['-beta-ip-'].get())
            stream = os.popen('ssh root@' + window['-beta-ip-'].get(
            ) + ' "axiom_start.sh raw ; axiom_scn_reg 30 0x7000 ; axiom_raw_mark.sh ; axiom_scn_reg 2 0x100"')
            print(stream.read())

        if event == '-extract-':
            foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
                0]]
            print('splitting selected clip: ' + get_rgb_file())
            # print ('split ' + get_rgb_file() + ' ' + foldername + '/' + foldername +
            #           '_frame_ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5')

            p = Popen(['split ' + get_rgb_file() + ' ' + window['-inputfolder-'].get() + '/' + foldername + '/' + foldername +
                       '_frame_ --additional-suffix=.frame -d -b 6220800 --suffix-length=5'], shell=True, stdout=PIPE,
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
            foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
                0]]
            print('converting selected clip: ' + get_rgb_file())
            command = 'python3 ' + os.path.dirname(os.path.realpath(__file__)) + '/frame-convert.py -i ' + window['-inputfolder-'].get() + '/' + foldername + '/ -t ' + target
            print(command)
            p = Popen([command], shell=True, stdout=PIPE,
                      bufsize=1,
                      close_fds=ON_POSIX)
            q = Queue()
            t = Thread(target=enqueue_output, args=(p.stdout, q))
            t.daemon = True  # thread dies with the program
            t.start()

            update_clip_info()

        # if event == '-preview-':
        #   foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[
        #       0]]
#
 #           p = Popen(['dcraw -T -h ' + foldername + '/*.DNG && ffmpeg -r 30 -i ' + foldername + '/' +
 #                      foldername + '_%05d.tiff -c:v libx264 -vf fps=30 ' + foldername + '/' + foldername + '.mp4 && rm ' + foldername + '/*.tiff'],
 #                     shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
 #           q = Queue()
 #           t = Thread(target=enqueue_output, args=(p.stdout, q))
 #           t.daemon = True  # thread dies with the program
 #           t.start()

        if event == '-show-preview-':
            foldername = window['-inputfolder-'].get() + '/' + window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
            print ('cd ' + data['recorderfolder'] + " ; target/release/cli from-cli RawDirectoryReader --file-pattern '" + foldername + "/*.raw12'" +
                       ' --bit-depth 12 --height 2160 --width 3840 --red-in-first-row false --red-in-first-col true --fps 30  ! GpuBitDepthConverter ! Debayer ! Display --fullscreen true')
            p = Popen(['cd ' + data['recorderfolder'] + " ; target/release/cli from-cli RawDirectoryReader --file-pattern '" + foldername + "/*.raw12'" +
                       ' --bit-depth 12 --height 2160 --width 3840 --red-in-first-row false --red-in-first-col true --fps 30  ! GpuBitDepthConverter ! Debayer ! Display --fullscreen true'],
                      shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
            q = Queue()
            t = Thread(target=enqueue_output, args=(p.stdout, q))
            t.daemon = True  # thread dies with the program
            t.start()
            print("preview playback started")
            


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
