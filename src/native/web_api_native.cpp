/*
 * Web_API for the native client: the engine reports what happens (map loaded, player connected,
 * ...) to the host. In the browser that is the page's JavaScript, here it is C++ (yno_host).
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../web_api.h"

#include "../output.h"
#include "yno_net.h"

using namespace Web_API;

std::string Web_API::GetSocketURL() {
	const auto& cfg = YnoNet::GetConfig();
	if (cfg.room_key.empty()) {
		return std::string();  // no signing key: the game rooms cannot be joined
	}
	return "wss://" + cfg.server_host + "/" + cfg.game_id + "/";
}

void Web_API::OnLoadMap(std::string_view name) {
	Output::Debug("host: map loaded: {}", name);
}

void Web_API::OnRoomSwitch() {
}

void Web_API::SyncPlayerData(std::string_view uuid, int rank, int, std::string_view, const int[5], int id) {
	Output::Debug("host: player data uuid={} rank={} id={}", uuid, rank, id);
}

void Web_API::OnPlayerDisconnect(int) {
}

void Web_API::OnPlayerNameUpdated(std::string_view, int) {
}

void Web_API::OnPlayerSystemUpdated(std::string_view, int) {
}

void Web_API::UpdateConnectionStatus(int status) {
	Output::Debug("host: room connection status {}", status);
}

void Web_API::ReceiveInputFeedback(int) {
}

void Web_API::NametagModeUpdated(int) {
}

void Web_API::OnPlayerSpriteUpdated(std::string_view, int, int) {
}

void Web_API::OnPlayerTeleported(int, int, int) {
}

void Web_API::OnUpdateSystemGraphic(std::string_view) {
}

void Web_API::OnRequestBadgeUpdate() {
}

void Web_API::ShowToastMessage(std::string_view msg, std::string_view) {
	Output::Debug("host: toast: {}", msg);
}

bool Web_API::ShouldConnectPlayer(std::string_view) {
	return true;
}

void Web_API::OnRequestFile(std::string_view) {
}
