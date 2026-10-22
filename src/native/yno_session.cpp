/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_session.h"

#include <chrono>
#include <map>
#include <memory>

#include "../output.h"
#include "yno_net.h"
#include "yno_ws.h"

namespace {
	// the website joins the fields with U+FFFF (UTF-8: EF BF BF)
	const std::string kDelim = "\xEF\xBF\xBF";
	constexpr size_t kMaxChat = 200;

	using Clock = std::chrono::steady_clock;

	std::unique_ptr<YnoNet::WsClient> ws;
	bool wanted = false;
	bool connected = false;
	int attempt = 0;
	Clock::time_point retry_at;

	int online = -1;
	std::deque<YnoSession::ChatMessage> chat;
	uint32_t chat_version = 0;
	std::map<std::string, YnoSession::PlayerInfo> players;

	void Connect() {
		const auto& cfg = YnoNet::GetConfig();
		ws = std::make_unique<YnoNet::WsClient>();
		ws->Open("wss://" + cfg.server_host + "/" + cfg.game_id + "/session",
				cfg.auth_cookie.empty() ? std::string() : "auth=" + cfg.auth_cookie);
	}

	void AddChat(YnoSession::ChatType type, const std::string& uuid, const std::string& text, const std::string& msg_id) {
		if (!msg_id.empty()) {
			for (const auto& m : chat) {
				if (m.msg_id == msg_id) {
					return;  // history and live messages can overlap
				}
			}
		}
		YnoSession::ChatMessage m;
		m.type = type;
		m.uuid = uuid;
		auto it = players.find(uuid);
		m.name = it != players.end() && !it->second.name.empty() ? it->second.name : "?";
		m.text = text;
		m.msg_id = msg_id;
		Output::Debug("chat: [{}] {}: {}", static_cast<int>(type), m.name, text);
		chat.push_back(std::move(m));
		while (chat.size() > kMaxChat) {
			chat.pop_front();
		}
		++chat_version;
	}

	int ToInt(const std::vector<std::string>& v, size_t i) {
		return i < v.size() ? atoi(v[i].c_str()) : 0;
	}

	void Handle(const std::vector<std::string>& f) {
		using YnoSession::ChatType;
		if (f.empty()) {
			return;
		}
		const std::string& cmd = f[0];
		auto arg = [&](size_t i) -> std::string { return i + 1 < f.size() ? f[i + 1] : std::string(); };

		if (cmd == "pc") {
			online = atoi(arg(0).c_str());
		} else if (cmd == "p") {
			// uuid, name, system, rank, account, badge, 5 medals
			YnoSession::PlayerInfo p;
			p.name = arg(1);
			p.system_name = arg(2);
			p.rank = atoi(arg(3).c_str());
			p.account = arg(4) == "1";
			p.badge = arg(5) == "null" ? std::string() : arg(5);
			players[arg(0)] = std::move(p);
			// messages that arrived before the name did
			for (auto& m : chat) {
				if (m.uuid == arg(0) && m.name == "?") {
					m.name = players[arg(0)].name;
					++chat_version;
				}
			}
		} else if (cmd == "say") {
			AddChat(ChatType::Map, arg(0), arg(1), std::string());
		} else if (cmd == "gsay") {
			// uuid, mapId, prevMapId, prevLocations, x, y, msg, msgId
			AddChat(ChatType::Global, arg(0), arg(6), arg(7));
		} else if (cmd == "psay") {
			AddChat(ChatType::Party, arg(0), arg(1), arg(2));
		}
	}
}

namespace YnoSession {

std::vector<std::string> SplitMessage(const std::string& frame) {
	std::vector<std::string> out;
	size_t start = 0;
	for (;;) {
		size_t pos = frame.find(kDelim, start);
		if (pos == std::string::npos) {
			out.push_back(frame.substr(start));
			return out;
		}
		out.push_back(frame.substr(start, pos - start));
		start = pos + kDelim.size();
	}
}

void Start() {
	wanted = true;
	attempt = 0;
	Connect();
}

void Stop() {
	wanted = false;
	ws.reset();
	connected = false;
}

void Reset() {
	Stop();
	chat.clear();
	players.clear();
	online = -1;
	++chat_version;
}

void Update() {
	if (!wanted) {
		return;
	}
	if (!ws) {
		if (Clock::now() >= retry_at) {
			Connect();
		}
		return;
	}
	YnoNet::WsClient::Event ev;
	while (ws && ws->PollEvent(ev)) {
		switch (ev.type) {
		case YnoNet::WsClient::Event::Open:
			connected = true;
			attempt = 0;
			Output::Debug("session: connected");
			break;
		case YnoNet::WsClient::Event::Message:
			Handle(SplitMessage(ev.data));
			break;
		case YnoNet::WsClient::Event::Close: {
			connected = false;
			online = -1;
			Output::Debug("session: closed ({})", ev.code);
			ws.reset();
			if (ev.code == 1028) {
				wanted = false;  // the server told us to go away for good
			} else {
				++attempt;
				retry_at = Clock::now() + std::chrono::seconds(std::min(attempt * 5, 30));
			}
			break;
		}
		}
	}
}

bool IsConnected() {
	return connected;
}

int OnlineCount() {
	return online;
}

const std::deque<ChatMessage>& Chat() {
	return chat;
}

uint32_t ChatVersion() {
	return chat_version;
}

void AddLocalMessage(const std::string& text) {
	if (text.empty()) {
		return;
	}
	ChatMessage m;
	m.name = "YNO";
	m.text = text;
	Output::Debug("chat: [local] {}", text);
	chat.push_back(std::move(m));
	while (chat.size() > kMaxChat) {
		chat.pop_front();
	}
	++chat_version;
}

const PlayerInfo* FindPlayer(const std::string& uuid) {
	auto it = players.find(uuid);
	return it == players.end() ? nullptr : &it->second;
}

void Send(const std::string& command, const std::vector<std::string>& args) {
	if (!ws || !connected) {
		return;
	}
	std::string frame = command;
	for (const auto& a : args) {
		frame += kDelim;
		frame += a;
	}
	ws->Send(std::move(frame), false);
}

}
