/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_games.h"

#include <filesystem>
#include <fstream>

#include "yno_net.h"

namespace YnoGames {

const std::vector<Entry>& All() {
	static const std::vector<Entry> games = {
		{"yume", "Yume Nikki"},
		{"2kki", "Yume 2kki"},
		{"flow", ".flow"},
		{"unevendream", "Uneven Dream"},
		{"deepdreams", "Deep Dreams"},
		{"someday", "Someday"},
		{"amillusion", "Amillusion"},
		{"braingirl", "Braingirl"},
		{"muma", "Muma Rope"},
		{"genie", "Dream Genie"},
		{"mikan", "Mikan Muzou"},
		{"ultraviolet", "Ultra Violet"},
		{"sheawaits", "She Awaits"},
		{"oversomnia", "Oversomnia"},
		{"tsushin", "Yume Tsushin"},
		{"nostalgic", "nostAlgic"},
		{"oneshot", "OneShot"},
		{"if", "If"},
		{"unaccomplished", "Unaccomplished"},
		{"fog", "FOG"},
		{"cold", "[COLD]"},
		{"loveyou", "Love You"},
		{"okuri", "Okuri"},
		{"scarynikki", "Scary Nikki"},
		{"space", "Space Funeral"},
	};
	return games;
}

std::string Directory(const std::string& id) {
	return YnoNet::GetConfig().games_dir + "/" + id;
}

bool IsDownloaded(const std::string& id) {
	std::error_code ec;
	return std::filesystem::exists(Directory(id) + "/RPG_RT.ldb", ec);
}

std::string LastPlayed() {
	std::ifstream f(YnoNet::GetConfig().games_dir + "/last-game.txt");
	std::string id;
	std::getline(f, id);
	return id;
}

void SetLastPlayed(const std::string& id) {
	std::ofstream f(YnoNet::GetConfig().games_dir + "/last-game.txt", std::ios::trunc);
	f << id << '\n';
}

}
