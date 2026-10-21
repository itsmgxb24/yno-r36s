/*
 * Client of the YNO "session" socket: online player count, player names, chat messages.
 * (In the browser this is session.js / chat.js / playerlist.js.)
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_SESSION_H
#define EP_NATIVE_YNO_SESSION_H

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace YnoSession {
	enum class ChatType { System, Map, Global, Party };

	struct ChatMessage {
		ChatType type = ChatType::System;
		std::string uuid;
		std::string name;     // resolved from the player table, "?" until the server told us
		std::string text;
		std::string msg_id;   // empty for map chat
	};

	struct PlayerInfo {
		std::string name;
		std::string system_name;
		int rank = 0;
		bool account = false;
		std::string badge;
	};

	/** Opens the session socket for YnoNet::GetConfig().game_id and keeps it open (reconnects). */
	void Start();
	void Stop();

	/** Stop() and forget chat and players (another game is selected). */
	void Reset();

	/** Main thread, once per frame. */
	void Update();

	bool IsConnected();
	int OnlineCount();  // players online in this game, -1 = unknown

	/** Oldest first, capped. */
	const std::deque<ChatMessage>& Chat();
	/** Increases whenever Chat() changes (so the overlay knows when to redraw). */
	uint32_t ChatVersion();

	const PlayerInfo* FindPlayer(const std::string& uuid);

	/** Sends "command<U+FFFF>arg<U+FFFF>arg...". */
	void Send(const std::string& command, const std::vector<std::string>& args = {});

	// pure parsing, exposed for tests
	std::vector<std::string> SplitMessage(const std::string& frame);
}

#endif
