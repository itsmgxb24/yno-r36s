/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_games.h"

#include <cctype>
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

std::vector<LocalFile> ScanLocal(const std::string& id) {
	namespace fs = std::filesystem;
	std::vector<LocalFile> files;
	const fs::path root = Directory(id);
	std::error_code ec;
	if (!fs::is_directory(root, ec)) {
		return files;
	}
	auto lower = [](std::string v) {
		for (auto& c : v) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
		return v;
	};
	for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
			!ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
		std::error_code e2;
		if (!it->is_regular_file(e2)) {
			continue;
		}
		std::string name = it->path().filename().string();
		std::string low = lower(name);
		bool save = low.rfind("save", 0) == 0 && low.size() > 4 && low.compare(low.size() - 4, 4, ".lsd") == 0;
		bool part = low.size() > 5 && low.compare(low.size() - 5, 5, ".part") == 0;
		if (save || part) {
			continue;  // the player's own saves and half-downloaded files are not the server's business
		}
		LocalFile f;
		f.path = fs::relative(it->path(), root, e2).generic_string();
		f.size = static_cast<long long>(it->file_size(e2));
		files.push_back(std::move(f));
	}
	return files;
}

std::shared_ptr<Report> StartMaintenance(const std::string& id, bool force, std::function<void()> on_progress) {
	auto report = std::make_shared<Report>();
	report->force = force;
	auto files = std::make_shared<std::vector<LocalFile>>();
	const std::string label = force ? "Re-downloading files" : "Verifying files";
	const std::string base = YnoNet::GetConfig().data_url + id + "/";
	const std::string dir = Directory(id);

	YnoNet::BeginBatch("Scanning files", 0);
	YnoNet::RunAsync([id, files]() {
		*files = ScanLocal(id);
	}, [report, files, force, label, base, dir, on_progress]() {
		report->total = static_cast<int>(files->size());
		if (files->empty()) {
			YnoNet::EndBatch();
			report->done = true;
			if (on_progress) on_progress();
			return;
		}
		YnoNet::BeginBatch(label, report->total);
		for (const auto& f : *files) {
			YnoNet::CheckFile(YnoNet::EncodeUrl(base + f.path), dir + "/" + f.path, force,
					[report, on_progress](YnoNet::CheckResult r) {
				++report->finished;
				switch (r) {
				case YnoNet::CheckResult::Ok: ++report->ok; break;
				case YnoNet::CheckResult::Updated: ++report->updated; break;
				case YnoNet::CheckResult::NotOnServer: ++report->not_on_server; break;
				case YnoNet::CheckResult::Failed: ++report->failed; break;
				case YnoNet::CheckResult::Cancelled: ++report->cancelled; break;
				}
				if (report->finished == report->total) {
					YnoNet::EndBatch();
					report->done = true;
				}
				if (on_progress) on_progress();
			});
		}
	});
	return report;
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
