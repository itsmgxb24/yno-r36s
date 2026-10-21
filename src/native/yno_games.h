/*
 * The games of the YNOproject hub (ynoproject.net) that the native client can play.
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_GAMES_H
#define EP_NATIVE_YNO_GAMES_H

#include <string>
#include <vector>

namespace YnoGames {
	struct Entry {
		const char* id;    // the path on ynoproject.net: ynoproject.net/<id>/
		const char* name;  // title shown in the list
	};

	/** In the order of the hub page. */
	const std::vector<Entry>& All();

	/** <games_dir>/<id> */
	std::string Directory(const std::string& id);

	/** The database of the game is on the card (it was played / downloaded before). */
	bool IsDownloaded(const std::string& id);

	/** Id of the game played last, "" if none. Remembered in <games_dir>/last-game.txt. */
	std::string LastPlayed();
	void SetLastPlayed(const std::string& id);
}

#endif
