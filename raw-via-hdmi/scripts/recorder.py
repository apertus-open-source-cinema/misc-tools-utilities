import PySimpleGUI as sg
import os
import sys
import getopt
import shutil
import glob


videodevice = "/dev/video0"
total, used, free = shutil.disk_usage(os.getcwd())
space = free // (2**30)
clipindex = 0

def main(argv):
    global videodevice
    try:
        opts, args = getopt.getopt(argv, "d:",
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


layout = [  [sg.Button('View Stream'), sg.Button('Start Recording')],
            [sg.Text('Free Disk Space: ' + str(space) + "GiB")],
            [sg.Text('Recording Directory: ')],
            [sg.Input(), sg.FileBrowse()],
            [sg.Text('Recordings:')],
            [sg.Listbox(values=('Loading...', 'Listbox 2', 'Listbox 3'), size=(35, 3), key='-recordings-'), sg.Button('Reload Recordings')],
            [sg.Button('Exit')]]

# Create the Window
window = sg.Window('AXIOM Recorder', layout)

# Load the recorded clips
def update_recordings_list():
    directories = []
    frames = 0

    for foldername in os.listdir("."):
        if os.path.isdir(foldername):
            # Check if there is a folder with an #.rgb file inside
            if (glob.glob(foldername + '/*.rgb')):
                #print(foldername) 
                # check size of *.rgb file to estimate number of frames in it
                for filename in os.listdir(foldername):
                    if filename.endswith(".rgb"):
                        frames = int(os.path.getsize(foldername + '/' + filename)/6220800)
                directories.append(foldername + '( ' + str(frames) + ' frames)')

    try:
        window['-recordings-'].update(values=directories)
    except:
        window['-recordings-'].update(values=directories)



# Event Loop to process "events" and get the "values" of the inputs
while True:
    event, values = window.read()
    if event == sg.WIN_CLOSED or event == 'Exit': # if user closes window or clicks Exit
        break
    if event == 'View Stream':
        stream = os.popen('ffplay ' + videodevice)
        output = stream.read()    
    if event == 'Reload Recordings':
        update_recordings_list()
    if event == 'Start Recording':
        folderdir = "Clip_" + f'{clipindex:05d}'
        while 1:
            if not os.path.exists(folderdir):
                os.mkdir(folderdir)
                print("Directory " , folderdir ,  " Created ")
                break
            else:    
                print("Directory " , folderdir ,  " already exists")
                clipindex+=1
                folderdir = "Clip_" + f'{clipindex:05d}'

        print('ffmpeg -i ' + videodevice + ' -map 0 ' + folderdir + '/' + folderdir + '.rgb')
        stream = os.popen('ffmpeg -i ' + videodevice + ' -map 0 ' + folderdir + '/' + folderdir + '.rgb')
        output = stream.read()


print("Total: %d GiB" % (total // (2**30)))
print("Used: %d GiB" % (used // (2**30)))
space = print("Free: %d GiB" % (free // (2**30)))

window.close()