#!/usr/bin/python

# SPDX-FileCopyrightText: © 2021 Sebastian Pichelhofer <sp@apertus.org>
# SPDX-FileCopyrightText: © 2021 Andrej Balyschew <red.falcon1983@googlemail.com>
# SPDX-License-Identifier: GPL-2.0-or-later

import argparse
import io
import os
import time
from pathlib import Path

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

image_dir = ""
raw12_file_list = []
file_list_length = 0
current_image_index = 0

display_buffer = io.BytesIO()

decimation_factor = 4

current_cfa_pattern = cv2.COLOR_BAYER_GR2RGB

graph = sg.Graph(key="IMAGE", canvas_size=(RAW_WIDTH, RAW_HEIGHT), graph_bottom_left=(0, 0),
                 graph_top_right=(RAW_WIDTH, RAW_HEIGHT), enable_events=True, change_submits=True, drag_submits=True)

SIZE_RESOLUTION_MAP = {
    18874368: (4096, 3072),  # 4096*3072*12/8
    18874624: (4096, 3072),  # 4096*3072*12/8+128*2
    12441600: (3840, 2160),  # 3840*2160*12/8
    13271040: (4096, 2160)  # 4096*2160*12/8
}

BAYER_CV_MAP = {
    "RGGB": cv2.COLOR_BAYER_RG2RGB,
    "BGGR": cv2.COLOR_BAYER_BG2RGB,
    "GRBG": cv2.COLOR_BAYER_GR2RGB,
    "GBRG": cv2.COLOR_BAYER_GB2RGB
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
    global display_buffer

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
             sg.Combo(["RGGB", "BGGR", "GBRG", "GRBG"], key="-cfa-pattern-", default_value="GRBG", enable_events=True,
                      readonly=True),
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
    global file_list_length, current_image_index

    window['-previous-image-'].Update(disabled=False)
    window['-next-image-'].Update(disabled=False)

    # if this is the first image in directory
    if current_image_index == 0:
        window['-previous-image-'].Update(disabled=True)

    # if this is the last image in the directory
    if file_list_length == current_image_index + 1:
        window['-next-image-'].Update(disabled=True)


def main_loop():
    global window, color_image, decimation_factor, current_image, current_image_index, raw12_file_list, image_dir, \
        file_list_length, current_cfa_pattern

    while True:
        event, values = window.read()
        print(event)
        if event == sg.WINDOW_CLOSED:
            break

        elif event == 'Right:114' or event == '-next-image-':
            current_image_index += 1
            if current_image_index > file_list_length - 1:
                current_image_index = file_list_length - 1
            handle_image_switching()
            pass

        elif event == 'Left:113' or event == '-previous-image-':
            current_image_index -= 1
            if current_image_index < 0:
                current_image_index = 0
            handle_image_switching()
            pass

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

        elif event == "-cfa-pattern-":
            selected_pattern = values["-cfa-pattern-"]
            current_cfa_pattern = BAYER_CV_MAP[selected_pattern]
            handle_image_switching()


def handle_image_switching():
    global current_image_index, image_dir, raw12_file_list, window
    file_name = raw12_file_list[current_image_index]
    window.set_title('raw12 Viewer: ' + str(file_name))
    update_next_image_buttons()
    load_image(Path(image_dir, file_name))
    show_images()


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
    global display_buffer, decimation_factor, current_image
    current_image = mono_image
    if window['-display-mode-color-'].get():
        current_image = color_image

    image = scale_image_data(decimation_factor)
    convert_to_image_data(image)
    graph.erase()
    graph.draw_image(data=display_buffer.getvalue(), location=(0, RAW_HEIGHT))

    # Limit dragging to the image size, if bigger than display area, otherwise display area is used
    bounding_box = graph.tk_canvas.bbox("all")
    graph.tk_canvas.configure(scrollregion=bounding_box)

    # Center image
    x_center = int(bounding_box[2] / 2)
    y_center = int(bounding_box[3] / 2)
    graph.tk_canvas.xview_scroll(x_center, "units")
    graph.tk_canvas.yview_scroll(y_center, "units")


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


def enumerate_image_files(dir_path):
    file_list = [fn for fn in os.listdir(dir_path) if fn.endswith('.raw12')]
    if file_list is None:
        print("No RAW12 file/s found")
        exit(1)

    return sorted(file_list)


def load_image(path):
    global RAW_WIDTH, RAW_HEIGHT, color_image, color_image, mono_image, current_cfa_pattern

    with open(path, "rb") as f:

        file_size = os.path.getsize(path)
        if file_size in SIZE_RESOLUTION_MAP:
            RAW_WIDTH, RAW_HEIGHT = SIZE_RESOLUTION_MAP.get(file_size)
        else:
            print("Image size is not compliant")
            exit(1)

        raw_data = np.fromfile(f, dtype=np.uint8)
        image_data = read_uint8(raw_data, path)
        image_data = np.reshape(image_data, (RAW_HEIGHT, RAW_WIDTH))

    # Create monochrome image
    mono_image = Image.frombytes('L', (RAW_WIDTH, RAW_HEIGHT), image_data)

    # Debayer data and create color image
    color_data = cv2.cvtColor(image_data, current_cfa_pattern)
    color_image = Image.frombytes('RGB', (RAW_WIDTH, RAW_HEIGHT), color_data)


def get_available_raw12_files(image_dir, requested_path):
    global raw12_file_list, file_list_length, current_image_index

    if not image_dir:
        image_dir = os.getcwd()

    image_file_name = requested_path.name
    raw12_file_list = enumerate_image_files(image_dir)
    if raw12_file_list:
        current_image_index = raw12_file_list.index(image_file_name)
    else:
        raw12_file_list = [requested_path]

    file_list_length = len(raw12_file_list)


def main():
    global current_image, color_image, decimation_factor, image_dir, \
        raw12_file_list, current_image_index, file_list_length
    start_time = current_milli_time()

    requested_path = Path(args.raw_file)
    image_dir = os.path.dirname(requested_path)
    get_available_raw12_files(image_dir, requested_path)

    load_image(Path(image_dir, raw12_file_list[current_image_index]))
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
