#!/bin/bash

(cd ../build && make -j)
rm -rf ../results
./copy-modules.py all -d stretch.img
./create-overlay-image.py all -d stretch.img
