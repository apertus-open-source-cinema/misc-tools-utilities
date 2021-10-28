#!/usr/bin/python
import io
import sys
import time

import PySimpleGUI as sg
import numpy as np
from PIL import Image

RAW_WIDTH = 3840
RAW_HEIGHT = 2160

raw12_file = sys.argv[1]

NUM_REGS = 128


def current_milli_time():
    return round(time.time() * 1000)

starttime = current_milli_time()


def convert_I_to_L(img):
    array = np.uint8(np.array(img) / 256)
    return Image.fromarray(array)


def read_uint12(data_chunk):
    data = np.frombuffer(data_chunk, dtype=np.uint8)
    fst_uint8, mid_uint8, lst_uint8 = np.reshape(data, (data.shape[0] // 3, 3)).astype(np.uint16).T
    fst_uint12 = (fst_uint8 << 4) + (mid_uint8 >> 4)
    fst_uint12 <<= 4
    snd_uint12 = ((mid_uint8 & 0x0F) << 8) + lst_uint8
    snd_uint12 <<= 4
    return np.reshape(np.concatenate((fst_uint12[:, None], snd_uint12[:, None]), axis=1), 2 * fst_uint12.shape[0])


with open(raw12_file, "rb") as f:
    raw_data = np.fromfile(f, dtype=np.uint8)
    image_data = read_uint12(raw_data)
    image_data = np.reshape(image_data, (RAW_WIDTH, RAW_HEIGHT))

readtime = current_milli_time()
print('reading took: ' + str((readtime - starttime) / 1000) + ' s')

im = Image.frombytes('I;16', (RAW_WIDTH, RAW_HEIGHT), image_data)
im = convert_I_to_L(im)
#im.convert('L')
im.thumbnail((RAW_WIDTH / 3, RAW_HEIGHT / 3))
# im.save("conv_test.png")
# im.show()

bio = io.BytesIO()
im.save(bio, format="PNG")
image_raw = sg.Image(data=bio.getvalue())
sg.theme('Reddit')

layout = [
    [
        [sg.Button('<-' , key='-previous-image-'), sg.Text('Display:'), 
        sg.Radio('monochrome', "display-mode", key='-display-mode-mono-', default=True, enable_events=True), 
        sg.Radio('color', "display-mode", key='-display-mode-color-', default=False, enable_events=True), 
        sg.Text('Resolution Decimation:'), 
        sg.Radio('1:1', "decimation-mode", key='-decimation1-', default=False, enable_events=True), 
        sg.Radio('1:2', "decimation-mode", key='-decimation2-', default=False, enable_events=True),
        sg.Radio('1:4', "decimation-mode", key='-decimation4-', default=True, enable_events=True), 
        sg.Radio('1:8', "decimation-mode", key='-decimation8-', default=False, enable_events=True), 
        sg.Button('->' , key='-next-image-')],
        [image_raw]
    ]
]

global window
window = sg.Window("raw12 Viewer: " + raw12_file, layout, layout, element_justification='c', resizable=True, finalize=True)
#window.Finalize()

displaytime = current_milli_time()
print('displaying took: ' + str((displaytime - starttime) / 1000) + ' s')

while True:
    event, values = window.Read()
    if event is None:
        break

    if event == '-next-image-':
        #todo: switch to next image in same folder
        print ('next image')

    if event == '-previous-image-':
        #todo: switch to previous image in same folder
        print ('previous image')

    if event == '-display-mode-mono-':
        #todo: switch to mono mode
        print ('mono mode activated')

    if event == '-display-mode-mono-':
        #todo: switch to mono mode
        print ('mono mode activated')

    if event == '-display-mode-color-':
        #todo: switch to color mode
        print ('color mode activated')

    if event == '-decimation1-':
        #todo: rescale to 1:1
        print ('decimation change 1:1')

    if event == '-decimation2-':
        #todo: rescale to 1:2
        print ('decimation change 1:2')

    if event == '-decimation4-':
        #todo: rescale to 1:4
        print ('decimation change 1:4')

    if event == '-decimation8-':
        #todo: rescale to 1:8
        print ('decimation change 1:8')
