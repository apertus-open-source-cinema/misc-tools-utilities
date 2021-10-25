#!/usr/bin/python

import os

import subprocess

p1 = subprocess.Popen('ls *.bgr', shell=True, stdin=None, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

out, err = p1.communicate() 
print(out)
p1.stdout.close()
