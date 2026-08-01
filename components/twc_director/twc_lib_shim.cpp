// components/twc_director/twc_lib_shim.cpp
//
// Shim to make sure the pure-C TWC/HPWC library in lib/twc/*.c
// is compiled and linked into the ESPHome firmware.
//
// We compile the C files as C++ but give them C linkage with
// extern "C" so the symbols match the headers.

extern "C" {
// Pull C sources in via full ESPHome component paths (works with native ESP-IDF
// toolchain; short "twc/..." includes require -I flags that no longer apply).
#include "esphome/components/twc_director/twc/twc_core.c"
#include "esphome/components/twc_director/twc/twc_device.c"
#include "esphome/components/twc_director/twc/twc_frame.c"
#include "esphome/components/twc_director/twc/twc_protocol.c"
}
