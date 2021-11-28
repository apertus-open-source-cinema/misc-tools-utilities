# frame-merge

A+B frame (from raw HDMI recordng) converter to apertus° AXIOM .raw12 files

## Dependencies

On Ubuntu you need the `build-essential` package.

## Notes

Currently it assumes the default resolution of 1920x1080 for A+B frames. This shall be extended in the future by an additional parameter.

## Usage

```
frame-merge a-frame.frame b-frame.frame output.raw12
```

## Build

```
make
```

## Install

```
make install
```
