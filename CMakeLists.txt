cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Only build main and its declared transitive dependencies. This keeps unused
# Wi-Fi/network stacks out of an otherwise BLE-only firmware and makes config
# changes substantially faster to rebuild.
idf_build_set_property(MINIMAL_BUILD ON)
project(Mote-Wand)
