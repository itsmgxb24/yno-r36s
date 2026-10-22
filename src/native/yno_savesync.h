/*
 * Save synchronisation with the YNO server, the same one the web client (forest-orb) uses:
 * <server>/<game>/api/savesync?command=timestamp|get|push. The server keeps one save per account and
 * game, and like the web client only slot 1 (Save01.lsd) is synchronised. Works only with an account
 * (the "auth" cookie): guests are told apart by IP address, which changes too easily.
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_SAVESYNC_H
#define EP_NATIVE_YNO_SAVESYNC_H

#include <memory>
#include <string>

namespace YnoSaveSync {
	/** Logged in and not switched off (--yno-no-save-sync). Main thread. */
	bool Enabled();

	enum class Result {
		Pending,
		Offline,        // the server did not answer: the card save is used, nothing is uploaded this time
		LoginRejected,  // the server does not accept the auth cookie (expired or wrong)
		Failed,         // another error (see `error`)
		NoServerSave,   // nothing on the server and nothing on the card
		UpToDate,
		Downloaded,     // the server save replaced the one on the card
		Uploaded,       // the card save was newer and went to the server
		Conflict,       // both changed since the last sync: the player has to choose (Resolve)
	};

	/** A check done before a game starts. Read its fields on the main thread only after `finished`. */
	struct Pull {
		std::string game_id;
		bool finished = false;
		Result result = Result::Pending;
		std::string error;
		// for the conflict screen
		std::string server_time;  // "2026-10-22 15:04 UTC"
		std::string card_time;
		std::string server_data;
		std::string server_stamp;
	};

	/** Compares the slot 1 save of the card with the server's and downloads or uploads it. Main thread. */
	std::shared_ptr<Pull> StartPull(const std::string& game_id);

	/**
	 * Settles a conflict. use_server: the server save goes to the card (the card one is kept as
	 * Save01.before-sync.lsd); otherwise the card save is uploaded (the server one is kept as
	 * Save01.server-copy.lsd). Main thread.
	 */
	void Resolve(Pull& pull, bool use_server);

	/** A line for the chat overlay about how the check ended, "" if there is nothing to say. */
	std::string Describe(const Pull& pull);

	/**
	 * Called after the game saved into `slot`. Slot 1 is uploaded in the background when the check at
	 * game start succeeded (so an old save on the card never overwrites a newer one on the server). Main thread.
	 */
	void OnSaved(int slot);
}

#endif
