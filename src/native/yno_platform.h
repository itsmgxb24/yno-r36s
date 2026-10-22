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

// The YNO client fetches game files lazily over HTTP into the game directory, guided by index.json
// (the engine was written for the web player, where this happens in the browser's file system).
#ifdef YNO_NATIVE
#  define EP_WEBLIKE_FS 1
#endif

#endif
