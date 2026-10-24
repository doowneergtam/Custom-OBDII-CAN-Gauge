cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# LVGL queues at most LV_INV_BUF_SIZE separate dirty areas per frame, and redraws the whole
# screen when that overflows. The faces mark needles as chains of small boxes, so allow more.
idf_build_set_property(COMPILE_DEFINITIONS "LV_INV_BUF_SIZE=96" APPEND)
project(nb1_gauge)
