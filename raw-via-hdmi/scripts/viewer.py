#!/usr/bin/python
import argparse
import io
import time
import glob, os
import PySimpleGUI as sg
import cv2
import numpy as np
from PIL import Image

RAW_WIDTH = 3840
RAW_HEIGHT = 2160

NUM_REGS = 128

window = None

mono_image_data = io.BytesIO()
color_image_data = io.BytesIO()

image_raw = sg.Image(size=(640, 480))


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


def read_uint8(data_chunk):
    data = np.frombuffer(data_chunk, dtype=np.uint8)
    fst_uint8, mid_uint8, lst_uint8 = np.reshape(data, (data.shape[0] // 3, 3)).astype(np.uint8).T
    fst_uint12 = fst_uint8
    snd_uint12 = ((mid_uint8 & 0x0F) << 4) | lst_uint8 >> 4
    data = np.concatenate((fst_uint12[:, None], snd_uint12[:, None]), axis=1)
    return data


def setup_images(image_path):
    with open(image_path, "rb") as f:
        raw_data = np.fromfile(f, dtype=np.uint8)
        image_data = read_uint8(raw_data)
        image_data = np.reshape(image_data, (RAW_HEIGHT, RAW_WIDTH))

    #cv2.imwrite("8bittest.png", image_data, [cv2.IMWRITE_PNG_COMPRESSION, 0])

    global color_image_data, mono_image_data

    monochrome_image = Image.frombytes('L', (RAW_WIDTH, RAW_HEIGHT), image_data)
    monochrome_image.thumbnail((RAW_WIDTH / 3, RAW_HEIGHT / 3))
    mono_image_data.seek(0)
    monochrome_image.save(mono_image_data, format="PNG")

    color_data = cv2.cvtColor(image_data, cv2.COLOR_BAYER_GR2RGB_EA)
    color_image = Image.frombytes('RGB', (RAW_WIDTH, RAW_HEIGHT), color_data)
    color_image.thumbnail((RAW_WIDTH / 3, RAW_HEIGHT / 3))
    color_image_data.seek(0)
    color_image.save(color_image_data, format="PNG")


def setup_window():
    sg.theme('Reddit')

    layout = [
        [
            [sg.Button('<-', key='-previous-image-'), sg.Text('Display:'),
             sg.Radio('monochrome', "display-mode", key='-display-mode-mono-', default=True, enable_events=True),
             sg.Radio('color', "display-mode", key='-display-mode-color-', default=False, enable_events=True),
             sg.Text('Resolution Decimation:'),
             sg.Radio('1:1', "decimation-mode", key='-decimation1-', default=False, enable_events=True),
             sg.Radio('1:2', "decimation-mode", key='-decimation2-', default=False, enable_events=True),
             sg.Radio('1:4', "decimation-mode", key='-decimation4-', default=True, enable_events=True),
             sg.Radio('1:8', "decimation-mode", key='-decimation8-', default=False, enable_events=True),
             sg.Button('->', key='-next-image-')],
            [image_raw]
        ]
    ]

    global window
    window = sg.Window("raw12 Viewer: " + args.raw_file, layout, element_justification='c', resizable=True, return_keyboard_events=True,
                       use_default_focus=False, finalize=True)
    window.Finalize()

def update_next_image_buttons():
    # Get list of all files in the same directory sorted by name
    list_of_files = sorted(filter(os.path.isfile, glob.glob('*.raw12')))

    window['-previous-image-'].Update(disabled=False)
    window['-next-image-'].Update(disabled=False)
    
    # if this is the first image in directory
    if list_of_files.index(current_image_name) == 0:
        #print('no further images in this directory')
        window['-previous-image-'].Update(disabled=True)

    # if this is the last image in the directory
    if len(list_of_files) == list_of_files.index(current_image_name)+1:
        #print('no further images in this directory')
        window['-next-image-'].Update(disabled=True)

def load_image_from_dir(index):
    global current_image_name
    # Get list of all files in the same directory sorted by name
    list_of_files = sorted(filter(os.path.isfile, glob.glob('*.raw12')))

    # First image
    if ((list_of_files.index(current_image_name) == 0) & (index < 0)):
        return

    # Last image
    if ((len(list_of_files) == list_of_files.index(current_image_name)+1) & (index > 0)):
        return

    
    next_image = list_of_files[list_of_files.index(current_image_name)+index]
    print('Switching to image: ' + next_image)

    # Update window title
    window.TKroot.title('raw12 Viewer: ' + next_image)

    setup_images(next_image)
    
    current_image_name = next_image

    if window['-display-mode-color-'].get():
        show_images(color_image_data)
    else:
        show_images(mono_image_data)

    update_next_image_buttons()

def main_loop():
    global window, current_image_name
    while True:
        event, values = window.Read()
        if event is None:
            break

        if event == 'Right:114':
            load_image_from_dir(1)

        if event == 'Left:113':
            load_image_from_dir(-1)

        if event == '-next-image-':
            load_image_from_dir(1)

        if event == '-previous-image-':
           load_image_from_dir(-1)

        if event == '-display-mode-mono-':
            # todo: switch to mono mode
            print('mono mode activated')
            show_images(mono_image_data)

        if event == '-display-mode-color-':
            # todo: switch to color mode
            print('color mode activated')
            show_images(color_image_data)

        if event == '-decimation1-':
            # todo: rescale to 1:1
            print('decimation change 1:1')

        if event == '-decimation2-':
            # todo: rescale to 1:2
            print('decimation change 1:2')

        if event == '-decimation4-':
            # todo: rescale to 1:4
            print('decimation change 1:4')

        if event == '-decimation8-':
            # todo: rescale to 1:8
            print('decimation change 1:8')


def show_images(data=None):
    if data is None:
        return

    # bio = io.BytesIO()
    # data.save(bio, format="PNG")
    global image_raw
    image_raw.update(data=data.getvalue())



def main():
    global current_image_name
    start_time = current_milli_time()
    current_image_name = "image1.raw12" #args.raw_file
    setup_images(current_image_name)
    read_time = current_milli_time()
    print('reading took: ' + str((read_time - start_time) / 1000) + ' s')

    setup_window()
    show_images(mono_image_data)
    display_time = current_milli_time()
    print('displaying took: ' + str((display_time - start_time) / 1000) + ' s')
    update_next_image_buttons()

    main_loop()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='raw image viewer for apertus° AXIOM images and sequences')
    parser.add_argument('raw_file', help='name/path of .raw12 file')
    args = parser.parse_args()

    main()
