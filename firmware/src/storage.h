// SPDX-License-Identifier: MIT
// Settings persistence in flash sector 5 (docs/midi-settings-spec.md, section 6).
#pragma once
#include <stdbool.h>

bool storage_load(void);   // newest valid record -> settings; false = none found (defaults stay)
bool storage_save(void);   // append the current settings; may erase sector 5 when full (CPU stalls ~1-2 s)
