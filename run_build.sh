#!/bin/bash
# -*- coding: utf-8 -*-

#west build -p auto -b native_posix_64 samples/basic/blinky
#west -v build -d build_posix -p auto -b native_posix_64 samples/hello_world

# build ppu32
west -v build -d build_ppu32 -p auto  -b ppu32 samples/hello_world
