#!/bin/bash
# -*- coding: utf-8 -*-

#west build -p auto -b native_posix_64 samples/basic/blinky
#west -v build -d build_posix -t menuconfig -p auto -b native_posix_64 samples/hello_world
#west -v build -d build_posix -p auto -b native_posix_64 samples/hello_world
#west -v build -d build_posix_blinky -p auto -b native_posix_64 samples/basic/blinky_pwm
west -v build -d build_posix_gem5 -p auto -b native_posix_64_gem5 samples/hello_world

# build ppu32
#west -v build -d build_ppu32 -p auto  -b ppu32 samples/hello_world


#west -v build -d build_hifive -p auto -b hifive1 samples/hello_world

# tests
#west -v build -d build_tests_posix -p auto -b native_posix_64 tests/boards/native_posix/native_tasks
#west -v build -d build_tests_posix -p auto -b native_posix_64 tests/boards/native_posix/rtc
