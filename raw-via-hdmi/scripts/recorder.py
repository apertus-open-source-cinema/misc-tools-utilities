from posixpath import abspath
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
            [sg.Input(os.getcwd()), sg.FileBrowse()],
            [sg.Text('Recordings:')],
            [sg.Listbox(values=('Loading...', 'Listbox 2', 'Listbox 3'), size=(50, 10), key='-recordings-'), sg.Text('Clipinfo:\n', key='-clipinfo-')],
            [sg.Button('Reload Recordings'), sg.Button('Update Clipinfo'), sg.Button('Extract Frames'), sg.Button('Convert Frames')],
            [sg.Button('Exit')]]

# Create the Window
window = sg.Window('AXIOM Recorder', layout, resizable=True,)


# Load the recorded clips
def update_recordings_list():
    directories = []

    for foldername in os.listdir("."):
        if os.path.isdir(foldername):
            # Check if there is a folder with an #.rgb file inside
            if (glob.glob(foldername + '/*.rgb')):
                #print(foldername) 
                # check size of *.rgb file to estimate number of frames in it
                
                directories.append(foldername)
    try:
        window['-recordings-'].update(values=directories)
    except:
        window['-recordings-'].update(values=directories)


# Read and display information about a specific clip
def update_clip_info():
    frames = 0
    frames_extracted = False
    window['-clipinfo-'].update('Clipinfo:')

    foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]

    for filename in os.listdir(foldername):
        if filename.endswith(".rgb"):
            # How many frames are inside one big .rgb file
            frames = int(os.path.getsize(foldername + '/' + filename)/6220800)

            # Hoiw many extracted *.bgr files are in that folder already
            stream = os.popen('ls ' + foldername + '/*.bgr | wc -l')
            framefilescount = int(stream.read())
            #print('bgr files found in current folder: ' + framefilescount)

            if (frames == framefilescount):
                frames_extracted = True

            if (frames_extracted):
                frames_extracted_string = 'yes'
            else:
                frames_extracted_string = 'no'

            window['-clipinfo-'].update('Clipinfo:\nClip: ' + filename +'\nFrames: ' + str(frames) + '\nFrames extracted: ' + frames_extracted_string)
            
# Read and display information about a specific clip
def get_rgb_file():
    foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
    for filename in os.listdir(foldername):
        if filename.endswith(".rgb"):
            return foldername + '/' + filename

init = True

# Event Loop to process "events" and get the "values" of the inputs
while True:
    event, values = window.read()
    if (init):
        update_recordings_list() # fixme, does not work
        init = False
    if event == sg.WIN_CLOSED or event == 'Exit': # if user closes window or clicks Exit
        break
    if event == 'View Stream':
        stream = os.popen('ffplay ' + videodevice)
        output = stream.read() 
    if event == 'Reload Recordings':
        update_recordings_list()
    if event == 'Update Clipinfo':
        update_clip_info()
    if event == 'Extract Frames':
        foldername = window['-recordings-'].Values[window['-recordings-'].Widget.curselection()[0]]
        stream = os.popen('split ' + get_rgb_file() + ' ' + foldername + '/test_frame__ --additional-suffix=.bgr -d -b 6220800 --suffix-length=5')
        output = stream.read()
        try:
            print('splitting selected clip: ' + get_rgb_file())
        except:
            print('splitting selected clip: ' + get_rgb_file())
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

window.close()