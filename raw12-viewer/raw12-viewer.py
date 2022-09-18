#!/usr/bin/python

# SPDX-FileCopyrightText: © 2021 Sebastian Pichelhofer <sp@apertus.org>
# SPDX-FileCopyrightText: © 2021 Andrej Balyschew <red.falcon1983@googlemail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import glob
import io
import os
import time
from enum import Enum

import PySimpleGUI as sg
import cv2
import numpy as np
from PIL import Image
from PySimpleGUI import Window

RAW_WIDTH = 3840
RAW_HEIGHT = 2160

NUM_REGS = 128

window: Window = None

color_image = None
mono_image = None
current_image = None

display_buffer = io.BytesIO()

decimation_factor = 4

graph = sg.Graph(key="IMAGE", canvas_size=(RAW_WIDTH, RAW_HEIGHT), graph_bottom_left=(0, 0),
                 graph_top_right=(RAW_WIDTH, RAW_HEIGHT), enable_events=True, change_submits=True, drag_submits=True)

SIZE_RESOLUTION_MAP = {
    18874368: (4096, 3072),  # 4096*3072*12/8
    18874624: (4096, 3072),  # 4096*3072*12/8+128*2
    12441600: (3840, 2160),  # 3840*2160*12/8
    13271040: (4096, 2160)  # 4096*2160*12/8
}

# Dragging
old_position = None
dragging = False


def current_milli_time():
    return round(time.time() * 1000)


def convert_int_to_lum(img):
    array = np.uint8(np.array(img) / 256)
    return Image.fromarray(array)


def read_uint12(data_chunk):
    data = np.frombuffer(data_chunk, dtype=np.uint8)
    fst_uint8, mid_uint8, lst_uint8 = np.reshape(data, (data.shape[0] // 3, 3)).astype(np.uint16).T
    fst_uint12 = (fst_uint8 << 8) | (mid_uint8 & 0xF0) << 4
    snd_uint12 = ((mid_uint8 & 0x0F) << 12) | lst_uint8 << 4
    return np.reshape(np.concatenate((fst_uint12[:, None], snd_uint12[:, None]), axis=1), 2 * fst_uint12.shape[0])


def read_uint8(data_chunk, image_path):
    data = np.frombuffer(data_chunk, dtype=np.uint8)

    file_size = os.path.getsize(image_path)
    if file_size == 18874624:  # 4096 * 3072 * 12 / 8 + 128 * 2:
        data = data[:-256]

    fst_uint8, mid_uint8, lst_uint8 = np.reshape(data, (data.shape[0] // 3, 3)).astype(np.uint8).T
    fst_uint12 = fst_uint8
    snd_uint12 = ((mid_uint8 & 0x0F) << 4) | lst_uint8 >> 4
    data = np.concatenate((fst_uint12[:, None], snd_uint12[:, None]), axis=1)
    return data


def convert_to_image_data(image):
    global display_buffer
    display_buffer.seek(0)
    image.save(display_buffer, format="PNG", compress_level=0)


def setup_images(image_path):
    global RAW_WIDTH, RAW_HEIGHT, color_image, color_image_data, mono_image_data, color_image, mono_image

    with open(image_path, "rb") as f:

        file_size = os.path.getsize(image_path)
        if file_size in SIZE_RESOLUTION_MAP:
            RAW_WIDTH, RAW_HEIGHT = SIZE_RESOLUTION_MAP.get(file_size)
        else:
            print("Image size is not compliant")
            exit(1)

        raw_data = np.fromfile(f, dtype=np.uint8)
        image_data = read_uint8(raw_data, image_path)
        image_data = np.reshape(image_data, (RAW_HEIGHT, RAW_WIDTH))

    # Create monochrome image
    mono_image = Image.frombytes('L', (RAW_WIDTH, RAW_HEIGHT), image_data)

    # Debayer data and create color image
    color_data = cv2.cvtColor(image_data, cv2.COLOR_BAYER_GR2RGB_EA)
    color_image = Image.frombytes('RGB', (RAW_WIDTH, RAW_HEIGHT), color_data)


def scale_image_data(factor):
    global display_mode, color_image_data, mono_image_data, display_buffer

    image = current_image.copy()
    if factor > 1:
        image.thumbnail((RAW_WIDTH / factor, RAW_HEIGHT / factor))
    return image


def setup_window():
    sg.theme('Reddit')

    layout = [
        [
            [sg.Button('<-', key='-previous-image-'), sg.Text('Display:'),
             sg.Radio('monochrome', "display-mode", key='-display-mode-mono-', default=False, enable_events=True),
             sg.Radio('color', "display-mode", key='-display-mode-color-', default=True, enable_events=True),
             sg.Text('Resolution Decimation:'),
             sg.Radio('1:1', "decimation-mode", key='-decimation1-', default=False, enable_events=True),
             sg.Radio('1:2', "decimation-mode", key='-decimation2-', default=False, enable_events=True),
             sg.Radio('1:4', "decimation-mode", key='-decimation4-', default=True, enable_events=True),
             sg.Radio('1:8', "decimation-mode", key='-decimation8-', default=False, enable_events=True),
             sg.Button('->', key='-next-image-')],
            graph
        ]
    ]

    global window
    window = sg.Window("raw12 Viewer: " + args.raw_file, layout, element_justification='c', resizable=True,
                       return_keyboard_events=True, use_default_focus=False, finalize=True)

    graph.tk_canvas.configure(xscrollincrement="1", yscrollincrement="1")


def update_next_image_buttons():
    # Get list of all files in the same directory sorted by name
    subfolder = os.path.dirname(os.path.abspath(current_image_name))
    list_of_files = sorted(filter(os.path.isfile, glob.glob(subfolder + '/*.raw12')))

    window['-previous-image-'].Update(disabled=False)
    window['-next-image-'].Update(disabled=False)

    # extract indexes with image name match
    index = [i for i, s in enumerate(list_of_files) if os.path.abspath(current_image_name) in s][0]

    # if this is the first image in directory
    if index == 0:
        window['-previous-image-'].Update(disabled=True)

    # if this is the last image in the directory
    if len(list_of_files) == index + 1:
        window['-next-image-'].Update(disabled=True)


def load_image_from_dir(targetindex):
    global current_image_name

    # Get list of all files in the same directory sorted by name
    subfolder = os.path.dirname(os.path.abspath(current_image_name))
    list_of_files = sorted(filter(os.path.isfile, glob.glob(subfolder + '/*.raw12')))

    # extract indexes with image name match
    index = [i for i, s in enumerate(list_of_files) if os.path.abspath(current_image_name) in s][0]

    # First image
    if (index == 0) & (targetindex < 0):
        return

    # Last image
    if (len(list_of_files) == index + 1) & (targetindex > 0):
        return

    next_image = list_of_files[index + targetindex]
    print('Switching to image: ' + next_image)

    # Update window title
    window.set_title('raw12 Viewer: ' + next_image)

    setup_images(next_image)

    current_image_name = next_image

    if window['-display-mode-color-'].get():
        show_images()

    update_next_image_buttons()


def main_loop():
    global window, current_image_name, color_image, color_image_data, color_data, decimation_factor, current_image_data, display_mode, current_image

    while True:
        event, values = window.read()
        print(event)
        if event == sg.WINDOW_CLOSED:
            break

        elif event == 'Right:114' or event == '-next-image-':
            load_image_from_dir(1)

        elif event == 'Left:113' or event == '-previous-image-':
            load_image_from_dir(-1)

        elif event == '-display-mode-mono-':
            print('mono mode activated')
            current_image = mono_image
            show_images()

        elif event == '-display-mode-color-':
            print('color mode activated')
            current_image = color_image
            show_images()

        elif event == '-decimation1-':
            print('decimation change 1:1')
            decimation_factor = 1
            show_images()

        elif event == '-decimation2-':
            print('decimation change 1:2')
            decimation_factor = 2
            show_images()

        elif event == '-decimation4-':
            print('decimation change 1:4')
            decimation_factor = 4
            show_images()

        elif event == '-decimation8-':
            print('decimation change 1:8')
            decimation_factor = 8
            show_images()

        elif event.startswith("IMAGE"):
            handle_image_dragging(event, values)


def handle_image_dragging(event, values):
    global dragging
    if dragging is False:
        global old_position
        old_position = values["IMAGE"]
        dragging = True
    move_image(values["IMAGE"])
    if event.endswith("+UP"):
        dragging = False


def show_images():
    global display_mode, display_buffer, decimation_factor
    image = scale_image_data(decimation_factor)
    convert_to_image_data(image)
    graph.erase()
    graph.draw_image(data=display_buffer.getvalue(), location=(0, RAW_HEIGHT))


def move_image(values):
    global old_position
    if old_position is None:
        old_position = values

    print(old_position)
    print(values)
    print(old_position[0] - values[0])

    graph.tk_canvas.xview_scroll(old_position[0] - values[0], "units")
    graph.tk_canvas.yview_scroll(-(old_position[1] - values[1]), "units")
    old_position = values

    print("TEST CLICK")


def start_dragging(event, values):
    global old_position
    old_position = values


def main():
    global current_image_name, current_image_data, current_image, color_image, decimation_factor
    start_time = current_milli_time()

    current_image_name = args.raw_file
    setup_images(current_image_name)
    current_image = color_image
    image = scale_image_data(decimation_factor)
    convert_to_image_data(image)

    read_time = current_milli_time()
    print('reading took: ' + str((read_time - start_time) / 1000) + 's')

    setup_window()
    show_images()
    display_time = current_milli_time()

    print('displaying took: ' + str((display_time - read_time) / 1000) + 's')
    print('total time: ' + str((display_time - start_time) / 1000) + 's')

    update_next_image_buttons()
    main_loop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='raw image viewer for apertus° AXIOM recorded images and sequences')
    parser.add_argument('raw_file', help='name/path of .raw12 file')
    args = parser.parse_args()

    main()
