#!/bin/sh
tail $1 -c 256 | ./metadatareader $2
