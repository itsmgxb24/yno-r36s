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

#include <functional>
#include <memory>
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

	/** A downloaded file of a game: path relative to the game directory (also the path on the server). */
	struct LocalFile {
		std::string path;
		long long size = 0;
	};

	/** Lists what is on the card for a game (saves and half-downloaded files are left out). Thread-safe. */
	std::vector<LocalFile> ScanLocal(const std::string& id);

	/** Result of a verification / re-download run. Filled on the main thread. */
	struct Report {
		bool force = false;     // re-download (true) or verify (false)
		int total = 0;
		int finished = 0;
		int ok = 0;             // verified: same size as on the server
		int updated = 0;        // downloaded (again)
		int not_on_server = 0;
		int failed = 0;
		int cancelled = 0;
		bool done = false;
	};

	/**
	 * Checks every file of the game on the card against the server (size), downloading again what
	 * differs; with `force` everything is downloaded again. `on_progress` runs on the main thread after
	 * every file, the last call has report->done set. Progress is also shown by the progress bar.
	 */
	std::shared_ptr<Report> StartMaintenance(const std::string& id, bool force, std::function<void()> on_progress);

	/** Id of the game played last, "" if none. Remembered in <games_dir>/last-game.txt. */
	std::string LastPlayed();
	void SetLastPlayed(const std::string& id);
}

#endif
