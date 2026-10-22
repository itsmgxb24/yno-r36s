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
#include <iterator>
#include <sstream>

#include <nlohmann/json.hpp>

#include "yno_net.h"

namespace {
	namespace fs = std::filesystem;

	std::string Join(const std::string& a, const std::string& b) {
		if (a.empty()) return b;
		if (b.empty()) return a;
		return a + "/" + b;
	}

	// Every file named in index.json. Version 2 nests directories ("_dirname") below "cache",
	// version 1 is a flat map of names to paths. Same reading as AsyncHandler::CreateRequestMapping.
	bool ParseIndex(const std::string& text, std::vector<std::string>& files) {
		using json = nlohmann::json;
		json j = json::parse(text, nullptr, false);
		if (j.is_discarded() || !j.is_object()) {
			return false;
		}
		int version = 1;
		if (j.contains("metadata") && j["metadata"].is_object() && j["metadata"].contains("version") &&
				j["metadata"]["version"].is_number()) {
			version = j["metadata"]["version"].get<int>();
		}
		if (version <= 1) {
			for (const auto& item : j.items()) {
				if (item.value().is_string()) files.push_back(item.value().get<std::string>());
			}
			return true;
		}
		if (!j.contains("cache") || !j["cache"].is_object()) {
			return false;
		}
		struct Walker {
			static void Run(const json& obj, const std::string& path, std::vector<std::string>& out) {
				std::string dirname;
				if (obj.contains("_dirname") && obj["_dirname"].is_string()) {
					dirname = obj["_dirname"].get<std::string>();
				}
				dirname = Join(path, dirname);
				for (const auto& item : obj.items()) {
					if (item.key() == "_dirname") continue;
					if (item.value().is_object()) {
						Run(item.value(), dirname, out);
					} else if (item.value().is_string()) {
						out.push_back(Join(dirname, item.value().get<std::string>()));
					}
				}
			}
		};
		Walker::Run(j["cache"], "", files);
		return true;
	}

	std::string ReadFile(const std::string& path) {
		std::ifstream f(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	}
}

namespace YnoGames {

const std::vector<Entry>& All() {
	static const std::vector<Entry> games = {
		{"yume", "Yume Nikki", 62},
		{"2kki", "Yume 2kki", 1205},
		{"flow", ".flow", 18},
		{"unevendream", "Uneven Dream", 173},
		{"deepdreams", "Deep Dreams", 64},
		{"someday", "Someday", 56},
		{"amillusion", "Amillusion", 224},
		{"braingirl", "Braingirl", 25},
		{"muma", "Muma Rope", 129},
		{"genie", "Dream Genie", 26},
		{"mikan", "Mikan Muzou", 36},
		{"ultraviolet", "Ultra Violet", 23},
		{"sheawaits", "She Awaits", 93},
		{"oversomnia", "Oversomnia", 17},
		{"tsushin", "Yume Tsushin", 35},
		{"nostalgic", "nostAlgic", 50},
		{"oneshot", "OneShot", 20},
		{"if", "If", 26},
		{"unaccomplished", "Unaccomplished", 24},
		{"fog", "FOG", 35},
		{"cold", "[COLD]", 33},
		{"loveyou", "Love You", 32},
		{"okuri", "Okuri", 14},
		{"scarynikki", "Scary Nikki", 19},
		{"space", "Space Funeral", 72},
	};
	return games;
}

std::string Directory(const std::string& id) {
	return YnoNet::GetConfig().games_dir + "/" + id;
}

bool IsDownloaded(const std::string& id) {
	std::error_code ec;
	return fs::exists(Directory(id) + "/RPG_RT.ldb", ec);
}

bool IsComplete(const std::string& id) {
	std::error_code ec;
	return fs::exists(Directory(id) + "/.all-files", ec);
}

std::string ApproxSize(int mb) {
	if (mb >= 1000) {
		int tenths = (mb * 10 + 512) / 1024;
		return "about " + std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " GB";
	}
	return "about " + std::to_string(mb) + " MB";
}

std::vector<LocalFile> ScanLocal(const std::string& id) {
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
		if (save || part || name == ".all-files" || name == ".savesync") {
			continue;  // the player's own saves, half-downloaded files and our markers are not the server's business
		}
		LocalFile f;
		f.path = fs::relative(it->path(), root, e2).generic_string();
		f.size = static_cast<long long>(it->file_size(e2));
		files.push_back(std::move(f));
	}
	return files;
}

std::shared_ptr<Report> StartMaintenance(const std::string& id, Kind kind, std::function<void()> on_progress) {
	auto report = std::make_shared<Report>();
	report->kind = kind;
	const bool force = kind == Kind::Redownload || kind == Kind::DownloadAll;
	const std::string label = kind == Kind::Verify ? "Verifying files" :
			kind == Kind::Redownload ? "Re-downloading files" : "Downloading all files";
	const std::string base = YnoNet::GetConfig().data_url + id + "/";
	const std::string dir = Directory(id);

	// the files to work on, found on a worker thread (a big game has tens of thousands)
	struct Plan {
		std::vector<std::string> files;
		int present = 0;
		std::string error;
	};
	auto plan = std::make_shared<Plan>();

	YnoNet::BeginBatch("Scanning files", 0);
	YnoNet::RunAsync([=]() {
		if (kind != Kind::DownloadAll) {
			for (const auto& f : ScanLocal(id)) plan->files.push_back(f.path);
			return;
		}
		// the list of all files: the one the game saved, or a fresh one from the server
		std::string text = ReadFile(dir + "/index.json");
		std::vector<std::string> all;
		if (text.empty() || !ParseIndex(text, all)) {
			all.clear();
			long status = 0;
			text.clear();
			if (!YnoNet::HttpGet(base + "index.json", text, status) || status != 200 || !ParseIndex(text, all)) {
				plan->error = "Could not get the list of files";
				return;
			}
			std::error_code ec;
			fs::create_directories(dir, ec);
			std::ofstream(dir + "/index.json", std::ios::binary | std::ios::trunc) << text;
		}
		for (const auto& f : all) {
			std::error_code ec;
			auto size = fs::file_size(dir + "/" + f, ec);
			if (!ec && size > 0) {
				++plan->present;
			} else {
				plan->files.push_back(f);
			}
		}
	}, [=]() {
		report->present = plan->present;
		report->total = static_cast<int>(plan->files.size());
		report->error = plan->error;
		auto finish = [report, on_progress, kind, dir]() {
			YnoNet::EndBatch();
			report->done = true;
			if (kind == Kind::DownloadAll && report->error.empty() && report->failed == 0 && report->cancelled == 0) {
				std::ofstream(dir + "/.all-files") << "every file of index.json was on the card at the end of a download\n";
			}
			if (on_progress) on_progress();
		};
		if (plan->files.empty()) {
			finish();
			return;
		}
		YnoNet::BeginBatch(label, report->total);
		for (const auto& f : plan->files) {
			YnoNet::CheckFile(YnoNet::EncodeUrl(base + f), dir + "/" + f, force,
					[report, on_progress, finish](YnoNet::CheckResult r) {
				++report->finished;
				switch (r) {
				case YnoNet::CheckResult::Ok: ++report->ok; break;
				case YnoNet::CheckResult::Updated: ++report->updated; break;
				case YnoNet::CheckResult::NotOnServer: ++report->not_on_server; break;
				case YnoNet::CheckResult::Failed: ++report->failed; break;
				case YnoNet::CheckResult::Cancelled: ++report->cancelled; break;
				}
				if (report->finished == report->total) {
					finish();
				} else if (on_progress) {
					on_progress();
				}
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
