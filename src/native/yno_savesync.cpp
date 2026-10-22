/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_savesync.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>

#include "../output.h"
#include "yno_games.h"
#include "yno_net.h"
#include "yno_session.h"

namespace fs = std::filesystem;

namespace {
	constexpr const char* kSaveName = "Save01.lsd";
	constexpr const char* kStateName = ".savesync";              // what the last sync agreed on
	constexpr const char* kCardBackup = "Save01.before-sync.lsd";  // the engine only lists SaveNN.lsd
	constexpr const char* kServerBackup = "Save01.server-copy.lsd";

	// the game whose saves may be uploaded: set once the check at game start has passed
	std::string armed_game;

	// two saves in a row must reach the server in order: the second upload reads the file after the first finished
	std::mutex upload_mutex;

	// what the card and the server held after the last successful sync
	struct State {
		std::string stamp;  // the server's timestamp (RFC 3339, it only changes on push)
		std::string hash;   // of the save that was then on both sides
	};

	std::string Hash(const std::string& data) {
		// FNV-1a: only tells "is this the save we synced" apart, no security involved
		uint64_t h = 1469598103934665603ULL;
		for (unsigned char c : data) {
			h = (h ^ c) * 1099511628211ULL;
		}
		char buf[17];
		snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(h));
		return buf;
	}

	bool ReadFile(const fs::path& path, std::string& out) {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			return false;
		}
		out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		return !in.bad();
	}

	// the save is replaced in one step: a power cut on the handheld must not leave half a save
	bool WriteFile(const fs::path& path, const std::string& data) {
		fs::path part = path;
		part += ".part";
		{
			std::ofstream out(part, std::ios::binary | std::ios::trunc);
			if (!out || !out.write(data.data(), static_cast<std::streamsize>(data.size())) || !out.flush()) {
				return false;
			}
		}
		std::error_code ec;
		fs::rename(part, path, ec);
		if (ec) {
			Output::Warning("savesync: cannot write {}: {}", path.string(), ec.message());
			fs::remove(part, ec);
			return false;
		}
		return true;
	}

	State ReadState(const fs::path& dir) {
		State s;
		std::ifstream in(dir / kStateName);
		std::getline(in, s.stamp);
		std::getline(in, s.hash);
		return s;
	}

	void WriteState(const fs::path& dir, const State& s) {
		WriteFile(dir / kStateName, s.stamp + "\n" + s.hash + "\n");
	}

	std::string ApiUrl(const std::string& game_id, const char* command) {
		return "https://" + YnoNet::GetConfig().server_host + "/" + game_id + "/api/savesync?command=" + command;
	}

	enum class Answer { Ok, Offline, LoginRejected, Failed };

	Answer Call(const std::string& method, const std::string& game_id, const char* command,
			const std::string& body, std::string& response, std::string& error) {
		long status = 0;
		response.clear();
		if (!YnoNet::HttpRequest(method, ApiUrl(game_id, command), body,
				body.empty() ? std::string() : "application/octet-stream", response, status)) {
			return Answer::Offline;
		}
		if (status == 200) {
			return Answer::Ok;
		}
		// the server answers every error with 400 and a short text; an auth cookie it cannot verify
		// ends in "failed to get player data" before the command is even looked at
		if (response.find("failed to get player data") != std::string::npos) {
			return Answer::LoginRejected;
		}
		error = "HTTP " + std::to_string(status);
		Output::Warning("savesync: {} {}: {} {}", method, command, status, response.substr(0, 80));
		return Answer::Failed;
	}

	YnoSaveSync::Result ToResult(Answer a) {
		switch (a) {
		case Answer::Offline: return YnoSaveSync::Result::Offline;
		case Answer::LoginRejected: return YnoSaveSync::Result::LoginRejected;
		default: return YnoSaveSync::Result::Failed;
		}
	}

	// "" when the server has no save: it answers either nothing or the Unix epoch then
	std::string Stamp(std::string body) {
		while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' ')) body.pop_back();
		return body.rfind("1970-", 0) == 0 ? std::string() : body;
	}

	// "2026-10-22T15:04:05Z" -> "2026-10-22 15:04 UTC"
	std::string ShowStamp(const std::string& stamp) {
		if (stamp.size() < 16) return stamp;
		return stamp.substr(0, 10) + " " + stamp.substr(11, 5) + " UTC";
	}

	std::string ShowFileTime(const fs::path& path) {
		std::error_code ec;
		auto t = fs::last_write_time(path, ec);
		if (ec) return "?";
		auto sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
				t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
		std::time_t tt = std::chrono::system_clock::to_time_t(sys);
		std::tm tm{};
		gmtime_r(&tt, &tm);
		char buf[32];
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M UTC", &tm);
		return buf;
	}

	// pushes `data` and returns the new server timestamp ("" on failure). Worker thread.
	YnoSaveSync::Result Upload(const std::string& game_id, const std::string& data, std::string& stamp, std::string& error) {
		std::string response;
		Answer a = Call("POST", game_id, "push", data, response, error);
		if (a != Answer::Ok) return ToResult(a);
		a = Call("GET", game_id, "timestamp", std::string(), response, error);
		if (a != Answer::Ok) return ToResult(a);
		stamp = Stamp(response);
		return YnoSaveSync::Result::Uploaded;
	}

	// the whole check, on a worker thread
	void DoPull(YnoSaveSync::Pull& p) {
		using R = YnoSaveSync::Result;
		const fs::path dir = YnoGames::Directory(p.game_id);
		const fs::path save = dir / kSaveName;
		std::error_code ec;
		fs::create_directories(dir, ec);

		std::string local;
		const bool have_local = ReadFile(save, local);
		const std::string local_hash = have_local ? Hash(local) : std::string();
		const State state = ReadState(dir);

		std::string response;
		Answer a = Call("GET", p.game_id, "timestamp", std::string(), response, p.error);
		if (a != Answer::Ok) {
			p.result = ToResult(a);
			return;
		}
		const std::string stamp = Stamp(response);
		Output::Debug("savesync: {} server={} last={} card={}", p.game_id, stamp.empty() ? "-" : stamp,
				state.stamp.empty() ? "-" : state.stamp, have_local ? local_hash : "-");

		auto upload_local = [&]() {
			std::string new_stamp;
			p.result = Upload(p.game_id, local, new_stamp, p.error);
			if (p.result == R::Uploaded) WriteState(dir, {new_stamp, local_hash});
		};

		if (stamp.empty()) {
			// nothing on the server yet: the card save becomes the server's
			if (have_local) upload_local();
			else p.result = R::NoServerSave;
			return;
		}

		if (stamp == state.stamp && have_local) {
			// the server still has what we synced last; only the card may have moved on (saved offline)
			if (local_hash == state.hash) p.result = R::UpToDate;
			else upload_local();
			return;
		}

		a = Call("GET", p.game_id, "get", std::string(), p.server_data, p.error);
		if (a != Answer::Ok) {
			p.server_data.clear();
			p.result = ToResult(a);
			return;
		}
		const std::string server_hash = Hash(p.server_data);
		if (have_local && local_hash == server_hash) {
			WriteState(dir, {stamp, server_hash});
			p.server_data.clear();
			p.result = R::UpToDate;
			return;
		}
		if (!have_local || (!state.hash.empty() && local_hash == state.hash)) {
			// the card has nothing new since the last sync: take the server's
			if (have_local) WriteFile(dir / kCardBackup, local);
			if (!WriteFile(save, p.server_data)) {
				p.error = "cannot write the save to the card";
				p.result = R::Failed;
				return;
			}
			WriteState(dir, {stamp, server_hash});
			p.server_data.clear();
			p.result = R::Downloaded;
			return;
		}

		// both changed (or this console never synced and has its own save): only the player knows which one to keep
		p.server_stamp = stamp;
		p.server_time = ShowStamp(stamp);
		p.card_time = ShowFileTime(save);
		p.result = R::Conflict;
	}

	void Arm(const YnoSaveSync::Pull& p) {
		using R = YnoSaveSync::Result;
		bool ok = p.result == R::UpToDate || p.result == R::Downloaded || p.result == R::Uploaded || p.result == R::NoServerSave;
		armed_game = ok ? p.game_id : std::string();
	}
}

namespace YnoSaveSync {

bool Enabled() {
	const auto& cfg = YnoNet::GetConfig();
	return cfg.save_sync && !cfg.auth_cookie.empty();
}

std::shared_ptr<Pull> StartPull(const std::string& game_id) {
	auto p = std::make_shared<Pull>();
	p->game_id = game_id;
	armed_game.clear();
	YnoNet::RunAsync([p]() {
		std::lock_guard<std::mutex> lock(upload_mutex);
		DoPull(*p);
	}, [p]() {
		p->finished = true;
		Output::Debug("savesync: check of {} ended: {} {}", p->game_id, static_cast<int>(p->result), p->error);
		Arm(*p);
	});
	return p;
}

void Resolve(Pull& p, bool use_server) {
	const fs::path dir = YnoGames::Directory(p.game_id);
	const std::string data = std::move(p.server_data);
	p.server_data.clear();
	if (use_server) {
		std::string local;
		if (ReadFile(dir / kSaveName, local)) WriteFile(dir / kCardBackup, local);
		if (WriteFile(dir / kSaveName, data)) {
			WriteState(dir, {p.server_stamp, Hash(data)});
			p.result = Result::Downloaded;
		} else {
			p.result = Result::Failed;
			p.error = "cannot write the save to the card";
		}
		Arm(p);
		return;
	}

	// keep the card's: the server copy stays on the card in case the choice was wrong
	WriteFile(dir / kServerBackup, data);
	p.result = Result::Uploaded;
	armed_game = p.game_id;
	OnSaved(1);
}

std::string Describe(const Pull& p) {
	switch (p.result) {
	case Result::Offline: return "Save sync: server not reachable, using the save on the card.";
	case Result::LoginRejected: return "Save sync: the server did not accept the login (AUTH in yno.conf expired?).";
	case Result::Failed: return "Save sync failed: " + p.error + ".";
	case Result::UpToDate: return "Save sync: slot 1 is up to date.";
	case Result::Downloaded: return "Save sync: slot 1 downloaded from the server.";
	case Result::Uploaded: return "Save sync: slot 1 uploaded to the server.";
	default: return "";
	}
}

void OnSaved(int slot) {
	const std::string game_id = YnoNet::GetConfig().game_id;
	if (slot != 1 || !Enabled() || game_id.empty()) {
		return;
	}
	if (armed_game != game_id) {
		// no successful check at game start (offline, or started without the game list): uploading now
		// could replace a newer save on the server, so this one waits for the next start
		Output::Debug("savesync: slot 1 of {} saved, not uploaded (no check at start)", game_id);
		return;
	}

	struct Job {
		Result result = Result::Pending;
		std::string error;
	};
	auto job = std::make_shared<Job>();
	YnoNet::RunAsync([job, game_id]() {
		std::lock_guard<std::mutex> lock(upload_mutex);
		const fs::path dir = YnoGames::Directory(game_id);
		std::string data;
		if (!ReadFile(dir / kSaveName, data)) {
			job->result = Result::Failed;
			job->error = "cannot read the save";
			return;
		}
		std::string stamp;
		job->result = Upload(game_id, data, stamp, job->error);
		if (job->result == Result::Uploaded) WriteState(dir, {stamp, Hash(data)});
	}, [job, game_id]() {
		Output::Debug("savesync: upload of {} ended: {} {}", game_id, static_cast<int>(job->result), job->error);
		if (YnoNet::GetConfig().game_id != game_id) return;  // the player already left the game
		Pull shown;
		shown.result = job->result;
		shown.error = job->error;
		if (job->result == Result::Offline) {
			YnoSession::AddLocalMessage("Save sync: not uploaded (offline), it will be at the next start.");
		} else {
			YnoSession::AddLocalMessage(Describe(shown));
		}
	});
}

}
