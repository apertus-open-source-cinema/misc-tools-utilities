#!/usr/bin/python

import PySimpleGUI as sg
import itertools
import numpy as np
import time
import sys

RAW_WIDTH = 3840
RAW_HEIGHT = 2160

raw12_file = sys.argv[1]
 
NUM_REGS = 128

def current_milli_time():
    return round(time.time() * 1000)
 
def raw12_reader(path):
   with open(path, 'rb') as f:
     for row in range(RAW_HEIGHT):
        for col in range(RAW_WIDTH >> 1):
           val = f.read(3)
           yield (val[0] << 4) | (val[1] >> 4)
           yield ((val[1] & 0xF) << 8) | val[2]

starttime = current_milli_time()

reader = raw12_reader(raw12_file)
data = itertools.islice(reader, RAW_WIDTH * RAW_HEIGHT)
#regs = itertools.islice(reader, NUM_REGS)
dataarray = np.fromiter(data, np.int)
raw = np.reshape(dataarray, (RAW_HEIGHT, RAW_WIDTH), 'C')

readtime = current_milli_time()
print ('reading took: ' + str((readtime - starttime)/1000) + ' s')

layout = [
    [
        sg.Graph(
            canvas_size=(960, 540),
            graph_bottom_left=(0, 0),
            graph_top_right=(960, 540),
            key="graph"
        )
    ]
]

sg.theme('Reddit')
window = sg.Window("raw12 Viewer", layout)
window.Finalize()
graph = window.Element("graph")

#background
graph.DrawRectangle((0, 0), (1024, 540), fill_color= 'black', line_color='black')

zoom_factor = 4

for x in range(960):
    for y in range(540):
        color = format(int(raw[y*zoom_factor][x*zoom_factor]/256), 'x')
        graph.DrawPoint((x,y), 2, '#'+color+color+color)

displaytime = current_milli_time()
print ('displaying took: ' + str((displaytime - starttime)/1000) + ' s')

while True:
    event, values = window.Read()
    if event is None:
        break
