#pragma once

// Injected by CI via PLATFORMIO_BUILD_FLAGS as -DFIRMWARE_VERSION='"YYYYMMDD-sha"'
// Falls back to "dev" for local builds.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "dev"
#endif
