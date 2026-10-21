/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_PLATFORM_H
#define EP_NATIVE_YNO_PLATFORM_H

// The YNO builds fetch game files lazily over HTTP into the game directory, guided by index.json.
// On the web the directory is Emscripten's file system, on the handheld it is a normal one.
#if defined(__EMSCRIPTEN__) || defined(YNO_NATIVE)
#  define EP_WEBLIKE_FS 1
#endif

#endif
