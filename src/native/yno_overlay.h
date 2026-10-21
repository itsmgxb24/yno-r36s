/*
 * In-game overlay of the native YNO client: the latest chat messages and the player count.
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_OVERLAY_H
#define EP_NATIVE_YNO_OVERLAY_H

namespace YnoOverlay {
	/** Creates the overlay drawable (after the graphics are initialized). */
	void Init();
	void Shutdown();
	/** Keeps the chat on screen (true) or lets it fade out after a few seconds (false). */
	void SetChatPinned(bool pinned);
	bool IsChatPinned();
}

#endif
