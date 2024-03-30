#!/usr/bin/python

# SPDX-FileCopyrightText: © 2021-2024 Sebastian Pichelhofer <sp@apertus.org>
# SPDX-FileCopyrightText: © 2021 Andrej Balyschew <red.falcon1983@googlemail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

import getopt
import glob
import os
import signal
import subprocess
import shutil
import sys
import json
from queue import Queue
from subprocess import PIPE, Popen
from threading import Thread
import time
import dearpygui.dearpygui as dpg
from dearpygui_ext.themes import create_theme_imgui_light

dpg.create_context()

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

video_device_input_text = dpg.generate_uuid()
axiom_recorder_path = dpg.generate_uuid()
axiom_recording_path = dpg.generate_uuid()


def safe_config_to_file():
    with open('recorder.json', 'w') as f:
        json.dump(data, f)
    print('recorder.json written')

# check if recorder.json exists, create default one if not
if not os.path.exists('recorder.json'):
    data['beta_ip'] = "192.168.0.9"
    safe_config_to_file()

# load configs from last session
f = open('recorder.json')
data = json.load(f)

if 'recordingfolder' in data:
    print('found recordingfolder: ' + data['recordingfolder'])
else:
    print('no recordingfolder found in config, loading default')
    data['recordingfolder'] = os.getcwd()

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
            # Check if there is a folder with an #.raw12 file inside
            if glob.glob(window['-inputfolder-'].get() + "/" + foldername + '/*.raw12'):
                # print(foldername)
                directories.append(foldername)
            # Check if there is a folder with an #.data file inside
            elif glob.glob(window['-inputfolder-'].get() + "/" + foldername + '/*.data'):
                # print(foldername)
                directories.append(foldername)
            # Check if there is a folder with an #.dng file inside
            elif glob.glob(window['-inputfolder-'].get() + "/" + foldername + '/*.dng'):
                # print(foldername)
                directories.append(foldername)
    directories.sort()
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
                #if bgr_frame_files_count == frames_rgb:
                #    window['-extract-'].Update(disabled=True)
                #else:
                #    window['-extract-'].Update(disabled=False)

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

def view_stream_color():
    stream = os.popen('ffplay ' + video_device)
    stream.read()

def view_stream_raw():
    p = Popen(['cd ' + data['recorderfolder'] + " ; target/release/cli from-cli WebcamInput --device 0 ! DualFrameRawDecoder ! GpuBitDepthConverter ! Debayer ! Display --fullscreen true"],
                      shell=True, stdout=PIPE, bufsize=1, close_fds=ON_POSIX)
    q = Queue()
    t = Thread(target=enqueue_output, args=(p.stdout, q))
    t.daemon = True  # thread dies with the program
    t.start()
    print("live raw preview started")

record_button = None


def handle_recording():
    if current_stream_process is None:
        start_recording()
        record_button.update(image_filename="images/recording_button.png")
    else:
        stop_recording()
        record_button.update(image_filename="images/record_button.png")


def start_recording():

    # create new folder with highest not existing clip index
    global clip_index, last_recorded_clip
    folderdir = window['-inputfolder-'].get() + "/Clip_" + f'{clip_index:05d}'
    while 1:
        if not os.path.exists(folderdir):
            #os.mkdir(folderdir)
            #print("Directory ", folderdir, " Created ")
            break
        else:
            print("Directory ", folderdir, " already exists")
            clip_index += 1
            folderdir = window['-inputfolder-'].get() + "/Clip_" + f'{clip_index:05d}'

    #print('ffmpeg -i ' + video_device + ' -map 0 -pix_fmt rgb24 ' +
    #      folderdir + '/' + 'Clip_' + f'{clip_index:05d}' + '.rgb')
    #global current_stream_process
    #current_stream_process = Popen('exec ffmpeg -i ' + video_device +
     #                              ' -map 0 -pix_fmt rgb24 ' + folderdir + '/' + 'Clip_' + f'{clip_index:05d}' + '.rgb', shell=True)
    global current_stream_process

    if (window['-rec-format-'].get() is "DNG"):
        print('cd ' + data['recorderfolder'] + " ; target/release/cli from-cli WebcamInput --device 0 ! DualFrameRawDecoder ! CinemaDngWriter --path "  + folderdir + "/")
        current_stream_process = Popen('cd ' + data['recorderfolder'] + " ; target/release/cli from-cli WebcamInput --device 0 ! DualFrameRawDecoder ! CinemaDngWriter --path " + folderdir + "/", shell=True, preexec_fn=os.setsid)
        print("DNG recording started")
        last_recorded_clip = folderdir + '/*.dng'
    else:
        print('cd ' + data['recorderfolder'] + " ; target/release/cli from-cli WebcamInput --device 0 ! DualFrameRawDecoder ! RawDirectoryWriter --path "  + folderdir + "/")
        current_stream_process = Popen('cd ' + data['recorderfolder'] + " ; target/release/cli from-cli WebcamInput --device 0 ! DualFrameRawDecoder ! RawDirectoryWriter --path " + folderdir + "/", shell=True, preexec_fn=os.setsid)
        print("raw12 recording started")
        last_recorded_clip = folderdir + '/*.data'


def stop_recording():
    global current_stream_process, last_recorded_clip
    if current_stream_process is not None:
        os.killpg(os.getpgid(current_stream_process.pid), signal.SIGTERM)

    print("Recording stopped")
    current_stream_process = None

    #print('Downloading Sensor Registers from: ' + window['-beta-ip-'].get())
    #write_sensor_registers(window['-beta-ip-'].get(), last_recorded_clip + ".registers")
    
    update_recordings_list()


def write_sensor_registers(BetaIP, filepath):
    stream = os.popen('ssh root@' + BetaIP + ' "axiom_snap -E -r -z" > ' + filepath)
    print(stream.read())


def test_button_callback(sender, app_data):
    print(f"sender is: {sender}")
    print(f"app_data is: {app_data}")

def recorder_file_dialog_callback(sender, app_data):
    #print("App Data: ", app_data)
    data['recorderfolder'] = app_data['file_path_name']
    dpg.set_value(axiom_recorder_path, app_data['file_path_name'])


def recording_file_dialog_callback(sender, app_data):
    #print("App Data: ", app_data)
    data['recordingfolder'] = app_data['file_path_name']
    dpg.set_value(axiom_recording_path, app_data['file_path_name'])


dpg.add_file_dialog(
    directory_selector=True, show=False, callback=recorder_file_dialog_callback, tag="recorder_file_dialog_id", width=700 ,height=400)

dpg.add_file_dialog(
    directory_selector=True, show=False, callback=recording_file_dialog_callback, tag="recording_file_dialog_id", width=700 ,height=400)
    
with dpg.font_registry():
    default_font = dpg.add_font(os.path.dirname(os.path.realpath(__file__)) + "/OpenSans-Regular.ttf", 22)

with dpg.window(tag="AXIOM Recorder GUI"):
    dpg.bind_font(default_font)

    with dpg.tab_bar():
        with dpg.tab(label="Record"):
            dpg.add_button(label="Callback Test", callback=test_button_callback, width=200, height=50)
            dpg.add_button(label="REC", width=200, height=50)
            dpg.add_button(label="Play Last Clip", width=200, height=50)
            dpg.add_button(label="View HDMI Signal (color)", callback=view_stream_color, width=200, height=50)
            dpg.add_button(label="View HDMI Signal (raw)", callback=view_stream_raw, width=200, height=50)
        with dpg.tab(label="Clips"):
            dpg.add_listbox(label="Clips", width=500)
        with dpg.tab(label="Config"):
            dpg.add_input_text(label="Video Device", tag=video_device_input_text)
            dpg.set_value(video_device_input_text, video_device)
            #dpg.add_input_text(label="AXIOM Beta IP")
            dpg.add_input_text(label="AXIOM Recorder Path", tag=axiom_recorder_path)
            dpg.set_value(axiom_recorder_path, data['recorderfolder'])
            dpg.add_button(label="Browse AXIOM Recorder Directory", callback=lambda: dpg.show_item("recorder_file_dialog_id"))
            dpg.add_input_text(label="Recording Folder", tag=axiom_recording_path)
            dpg.set_value(axiom_recording_path, data['recordingfolder'])
            dpg.add_button(label="Browse Recording Directory", callback=lambda: dpg.show_item("recording_file_dialog_id"))
            dpg.add_button(label="Write Config", width=200, height=50, callback=safe_config_to_file)



dpg.set_primary_window("AXIOM Recorder GUI", True)

light_theme = create_theme_imgui_light()
dpg.bind_theme(light_theme)

dpg.create_viewport(title='AXIOM Recorder GUI', width=800, height=600)

dpg.setup_dearpygui()
dpg.show_viewport()
dpg.start_dearpygui()
dpg.destroy_context()           


def main(argv):
    global video_device, window
    try:
        opts, args = getopt.getopt(argv, "d:h:",
                                   ["help", "video-device"])
    except getopt.GetoptError:
        print('recorder2.py -d <video-device>')
        window.close()
    for opt, arg in opts:
        if opt in ("-h", "--help"):
            print('recorder2.py -d <video-device>')
            sys.exit()
        elif opt in ("-d", "--video-device"):
            video_device = arg.strip()

    print('Using Video Device: ', video_device)

if __name__ == "__main__":
    main(sys.argv[1:])