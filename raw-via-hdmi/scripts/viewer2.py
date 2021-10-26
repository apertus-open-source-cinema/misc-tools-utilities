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


#def raw12_reader(path):
#    with open(path, 'rb') as f:
#        for row in range(RAW_HEIGHT):
#            for col in range(RAW_WIDTH >> 1):
#                val = f.read(3)
#                yield (val[0] << 4) | (val[1] >> 4)
#                yield ((val[1] & 0xF) << 8) | val[2]


starttime = current_milli_time()


def read_uint12(data_chunk):
    data = np.frombuffer(data_chunk, dtype=np.uint8)
    fst_uint8, mid_uint8, lst_uint8 = np.reshape(data, (data.shape[0] // 3, 3)).astype(np.uint16).T
    fst_uint12 = (fst_uint8 << 4) | (mid_uint8 >> 4)
    fst_uint12 >>= 4
    snd_uint12 = ((mid_uint8 & 0xF) << 8) | lst_uint8
    snd_uint12 >>= 4
    return np.reshape(np.concatenate((fst_uint12[:, None], snd_uint12[:, None]), axis=1), 2 * fst_uint12.shape[0])


with open(raw12_file, "rb") as f:
    raw_data = np.fromfile(f, dtype=np.uint8)
    image_data = read_uint12(raw_data)
    image_data = np.reshape(image_data, (RAW_WIDTH, RAW_HEIGHT))

readtime = current_milli_time()
print('reading took: ' + str((readtime - starttime) / 1000) + ' s')

im = Image.frombytes('L', (RAW_WIDTH, RAW_HEIGHT), image_data)
im.thumbnail((RAW_WIDTH / 4, RAW_HEIGHT / 4))
bio = io.BytesIO()
im.save(bio, format="PNG")
image_raw = sg.Image(data=bio.getvalue())

layout = [
    [
        [image_raw]
    ]
]

sg.theme('Reddit')
window = sg.Window("raw12 Viewer", layout)
window.Finalize()

displaytime = current_milli_time()
print('displaying took: ' + str((displaytime - starttime) / 1000) + ' s')

while True:
    event, values = window.Read()
    if event is None:
        break
