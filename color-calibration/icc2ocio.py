#!/usr/bin/python2
#
# Convert ICC profile to OCIO stanza
# Limited to TRCs + XYZ matrix for now
# Requires:
# - python2
# - iccdump from ArgyllCMS
# - colour (optional, for CIE plots)
# - numpy/matplotlib (optional, for TRC plots)

import os, sys, re, subprocess

def run(cmd):
    try:
        p = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        out = p.communicate()
        if p.returncode:
            print cmd
            print out[0]
            print out[1]
            raise SystemExit
            
        return out[0]
    except:
        print sys.exc_info()

# split iccdump output into tags
def split_tags(icc):
    tags = {}
    tag = None
    acc = ""
    for l in icc.split("\n"):
        m = re.match("tag ([0-9]+):", l)
        if m:
            if tag is not None:
                tags[tag] = acc
            tag = int(m.groups()[0])
            acc = ""
        else:
            acc += l + "\n"
    tags[tag] = acc
    return tags

# extract tag signature
def tag_sig(tag):
    sig_line = tag.split("\n")[0]
    m = re.match(" *sig +\'(.*)\'", sig_line)
    if m:
        return m.groups()[0]

# extract tag type
def tag_type(tag):
    type_line = tag.split("\n")[1]
    m = re.match(" *type +\'(.*)\'", type_line)
    if m:
        return m.groups()[0]

# extract curve data from ICC tag
def tag_curve(tag):
    if tag_type(tag) == 'curv':
        curve = []
        for l in tag.split("\n"):
            m = re.match(" *([0-9]+): *([0-9.\-]+)", l)
            if m:
                id, value = m.groups()
                id = int(id)
                value = float(value)
                assert id == len(curve)
                curve.append(value)
        return curve

# extract XYZ data from ICC tag
def tag_xyz(tag):
    if tag_type(tag) == 'XYZ ':
        for l in tag.split("\n"):
            m = re.match(" *0: *([0-9.\-]+), *([0-9.\-]+), *([0-9.\-]+) *", l)
            if m:
                x,y,z = [float(v) for v in m.groups()]
                return x,y,z

# extract ASCII data from ICC tag
def tag_ascii(tag):
    if tag_type(tag) in ['text', 'desc']:
        for l in tag.split("\n"):
            m = re.match(" *0x0000: (.*)", l)
            if m:
                return m.groups()[0]

# extract the info we are interested in,
# for describing the color profile
def extract_profile(icc):
    profile = {}
    tags = split_tags(icc)
    for t in tags.itervalues():
        sig = tag_sig(t)
        if sig in ['rTRC', 'gTRC', 'bTRC']:
            profile[sig] = tag_curve(t)
        if sig in ['rXYZ', 'gXYZ', 'bXYZ']:
            profile[sig] = tag_xyz(t)
        if sig in ['dmnd', 'dmdd', 'desc']:
            profile[sig] = tag_ascii(t).strip()
    return profile

if len(sys.argv) != 2:
    print "Usage: %s file.icc" % sys.argv[0]
    raise SystemExit

icc = run("iccdump -v3 '%s'" % sys.argv[1])

profile = extract_profile(icc)

# print description
desc = '%s %s' % (profile['dmnd'], profile['dmdd'])
if 'desc' in profile and profile['desc'] and len(profile['desc']):
    desc += ' (%s)' % profile['desc']

# print the RGB to XYZ matrix (which uses D50 white point)
matrix = [
    profile['rXYZ'][0], profile['gXYZ'][0], profile['bXYZ'][0], \
    profile['rXYZ'][1], profile['gXYZ'][1], profile['bXYZ'][1], \
    profile['rXYZ'][2], profile['gXYZ'][2], profile['bXYZ'][2] ];

print
print "cam2xyz_d50 = [ %8.5f, %8.5f, %8.5f\n" \
      "                %8.5f, %8.5f, %8.5f\n" \
      "                %8.5f, %8.5f, %8.5f  ]" % tuple(matrix)
print

# optional 1D LUT for each channel
file_transform = ""

if len(profile['rTRC']):
    base = os.path.splitext(sys.argv[1])[0]
    spi = base + '.spi1d'
    file_transform = "- !<FileTransform> {src: %s, interpolation: best}\n        " % spi
    f = open(spi, 'w')
    assert len(profile['rTRC']) == 256
    print >> f, "Version 1\n" \
                "From 0.0 1.0\n" \
                "Length 256\n" \
                "Components 3\n" \
                "{"
    for i in range(256):
        print >> f, "   %8.5f %8.5f %8.5f" % \
                (profile['rTRC'][i], profile['gTRC'][i], profile['bTRC'][i])

    print >> f, "}"
    f.close()

# print OCIO stanza
print """
  - !<ColorSpace>
    name: %s
    family: Spaces
    equalitygroup:
    bitdepth: 32f
    isdata: false
    allocation: uniform
    allocationvars: [0, 1]
    to_reference: !<GroupTransform>
      children:
        %s- !<MatrixTransform> {matrix: [%g, %g, %g, 0, %g, %g, %g, 0, %g, %g, %g, 0, 0, 0, 0, 1]}
        - !<MatrixTransform> {matrix: [3.3921940, -1.8264027, -0.5385522, 0, -1.0770996, 2.0213975, 0.0207989, 0, 0.0723073, -0.2217902, 1.3960932, 0, 0, 0, 0, 1]}
""" % tuple([desc] + [file_transform] + matrix)

# optional: plot the results, if you have numpy
from pylab import *
from colorpy.colormodels import *
from colorpy.plots import *

# plot our camera matrix instead of sRGB, in xy space
import colorpy.colormodels
colorpy.colormodels.PhosphorRed   = xyz_color(profile['rXYZ'][0], profile['rXYZ'][1], profile['rXYZ'][2])
colorpy.colormodels.PhosphorGreen = xyz_color(profile['gXYZ'][0], profile['gXYZ'][1], profile['gXYZ'][2])
colorpy.colormodels.PhosphorBlue  = xyz_color(profile['bXYZ'][0], profile['bXYZ'][1], profile['bXYZ'][2])
colorpy.colormodels.PhosphorWhite = xyz_color(0.9642, 1.0000, 0.8249)

clf()
shark_fin_plot()
axis('auto')
axis('equal')
savefig('matrix-xy.png')

def log2x(x):
    return log2(maximum(x,1));

# plot shaper curves (linear and log)
if len(profile['rTRC']):
    clf()
    x = linspace(0,4095,256);
    plot(x, profile['rTRC'], 'r')
    plot(x, profile['gTRC'], 'g')
    plot(x, profile['bTRC'], 'b')
    savefig('curves-lin.png')

    clf()
    x = log2x(linspace(0,4095,256)) - 12;
    plot(x, log2x(array(profile['rTRC']) * 4095) - 12, 'r')
    plot(x, log2x(array(profile['gTRC']) * 4095) - 12, 'g')
    plot(x, log2x(array(profile['bTRC']) * 4095) - 12, 'b')
    savefig('curves-log.png')
