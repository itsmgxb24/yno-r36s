/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "scene_yno_games.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <system_error>

#include <SDL.h>
#include <lcf/data.h>

#include "../async_handler.h"
#include "../audio.h"
#include "../audio_midi.h"
#include "../audio_secache.h"
#include "../cache.h"
#include "../drawable.h"
#include "../drawable_mgr.h"
#include "../filefinder.h"
#include "../font.h"
#include "../game_clock.h"
#include "../game_system.h"
#include "../input.h"
#include "../main_data.h"
#include "../multiplayer/game_multiplayer.h"
#include "../output.h"
#include "../player.h"
#include "../scene_logo.h"
#include "../text.h"
#include "yno_games.h"
#include "yno_net.h"
#include "yno_session.h"

namespace {
	constexpr int kRowHeight = 14;
	constexpr int kListTop = 24;
	constexpr int kMargin = 8;

	const Color kText(200, 200, 200, 255);
	const Color kBright(255, 255, 255, 255);
	const Color kDim(110, 110, 110, 255);
	const Color kAccent(120, 205, 150, 255);
	const Color kWarn(230, 170, 90, 255);

	std::string Files(int n) {
		return std::to_string(n) + (n == 1 ? " file" : " files");
	}

	std::string Megabytes(long long bytes) {
		long long tenths = (bytes * 10 + 524288) / 1048576;
		return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10) + " MB";
	}
}

// state shared with the threads that ask the server / scan the card
struct Scene_YnoGames::Shared {
	std::mutex mutex;
	std::vector<int> online;      // players per game, -1 = unknown
	std::string info_game;        // the game the numbers below belong to
	int info_files = -1;          // -1 = still scanning
	long long info_bytes = 0;
	std::atomic<bool> dirty{false};
};

// the drawable of the scene: all the text is drawn into one bitmap that is redrawn when something changed
class Scene_YnoGames::Screen final : public Drawable {
public:
	explicit Screen(Scene_YnoGames& s) : Drawable(Priority_Window), scene(s) {
		DrawableMgr::Register(this);
	}
	void Draw(Bitmap& dst) override {
		scene.DrawScreen(dst);
	}
private:
	Scene_YnoGames& scene;
};

Scene_YnoGames::Scene_YnoGames() {
	// the engine already knows what to do with a GameBrowser: leaving a game (settings -> Exit,
	// quitting the title) comes back here, and closing the list ends the program
	type = Scene::GameBrowser;
}

Scene_YnoGames::~Scene_YnoGames() = default;

static void ResetToDefaultSystem() {
	Main_Data::game_system = std::make_unique<Game_System>();
	Main_Data::game_system->SetSystemGraphic(CACHE_DEFAULT_BITMAP, lcf::rpg::System::Stretch_stretch, lcf::rpg::System::Font_gothic);
}

void Scene_YnoGames::DrawBackground(Bitmap& dst) {
	dst.Clear();
}

void Scene_YnoGames::Start() {
	ResetToDefaultSystem();
	screen = std::make_unique<Screen>(*this);

	const auto& games = YnoGames::All();
	std::string last = YnoGames::LastPlayed();
	for (size_t i = 0; i < games.size(); ++i) {
		if (last == games[i].id) index = static_cast<int>(i);
	}

	// how many players are in each game (the numbers on the hub page)
	shared = std::make_shared<Shared>();
	shared->online.assign(games.size(), -1);
	for (size_t i = 0; i < games.size(); ++i) {
		std::string id = games[i].id;
		auto state = shared;
		YnoNet::RunAsync([state, i, id]() {
			std::string body;
			long status = 0;
			const auto& cfg = YnoNet::GetConfig();
			if (YnoNet::HttpGet("https://" + cfg.server_host + "/" + id + "/api/players", body, status) && status == 200) {
				std::lock_guard<std::mutex> lock(state->mutex);
				state->online[i] = atoi(body.c_str());
			}
		}, [state]() {
			state->dirty = true;
		});
	}

	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_YnoGames::Continue(SceneType /* prev_scene */) {
	// back from a game: same clean-up as the engine's game browser
	Main_Data::game_system->BgmStop();

	Cache::ClearAll();
	AudioSeCache::Clear();
	MidiDecoder::Reset();
	lcf::Data::Clear();
	Player::ResetGameObjects();
	Player::RestoreBaseResolution();
	Player::game_title = "";
	Player::game_title_original = "";
	Player::translation.Reset();
	Font::ResetDefault();

	ResetToDefaultSystem();

	// ... and forget the game: its rooms and its chat
	GMI().Quit();
	YnoSession::Reset();
	YnoNet::GetConfig().game_id.clear();
	AsyncHandler::Reset();

	starting = false;
	mode = Mode::List;
	dirty = true;
}

// ---------------------------------------------------------------- drawing

void Scene_YnoGames::DrawScreen(Bitmap& dst) {
	if (!image || image->width() != dst.width() || image->height() != dst.height()) {
		image = Bitmap::Create(dst.width(), dst.height(), true);
		dirty = true;
	}
	// progress of a verification (and the end of a save check) comes without any key press
	if (mode == Mode::Busy || mode == Mode::Sync) {
		dirty = true;
	}
	if (dirty) {
		Render();
		dirty = false;
	}
	dst.Blit(0, 0, *image, image->GetRect(), Opacity::Opaque());
}

void Scene_YnoGames::Render() {
	const int w = image->width();
	const int h = image->height();
	FontRef font = Font::DefaultBitmapFont();
	const auto& games = YnoGames::All();

	image->FillRect(image->GetRect(), Color(0, 0, 0, 255));

	auto text = [&](int x, int y, const Color& c, const std::string& s) {
		Text::Draw(*image, x, y, *font, c, s);
	};
	auto right = [&](int y, const Color& c, const std::string& s) {
		text(w - kMargin - Text::GetSize(*font, s).width, y, c, s);
	};
	auto header = [&](const std::string& title) {
		text(kMargin, 4, kBright, title);
		image->FillRect(Rect(kMargin, 19, w - 2 * kMargin, 1), Color(70, 70, 70, 255));
	};
	auto footer = [&](const std::string& hints) {
		image->FillRect(Rect(kMargin, h - 18, w - 2 * kMargin, 1), Color(70, 70, 70, 255));
		text(kMargin, h - 15, kDim, hints);
	};

	std::lock_guard<std::mutex> lock(shared->mutex);
	const auto& game = games[index];

	switch (mode) {
	case Mode::List: {
		header("YNOproject - choose a game");
		int rows = (h - kListTop - 22) / kRowHeight;
		for (int r = 0; r < rows && top + r < static_cast<int>(games.size()); ++r) {
			int i = top + r;
			int y = kListTop + r * kRowHeight;
			bool selected = i == index;
			text(kMargin, y, selected ? kBright : kText, selected ? ">" : "");
			text(kMargin + 12, y, selected ? kBright : kText, games[i].name);
			if (!YnoGames::IsComplete(games[i].id)) {
				// no database yet / the game still fetches files while it runs: "Download all files" fixes that
				int name_w = Text::GetSize(*font, games[i].name).width;
				text(kMargin + 12 + name_w + 8, y, kDim, YnoGames::IsDownloaded(games[i].id) ? "(partial)" : "(download)");
			}
			int n = shared->online[i];
			right(y, n > 0 ? kAccent : kDim, n >= 0 ? std::to_string(n) : "-");
		}
		if (starting) {
			std::string loading = std::string("Loading ") + game.name + "...";
			image->FillRect(Rect(0, h / 2 - 16, w, 32), Color(0, 0, 0, 255));
			text((w - Text::GetSize(*font, loading).width) / 2, h / 2 - 6, kBright, loading);
		}
		if (top > 0) right(4, kDim, "^");
		if (top + rows < static_cast<int>(games.size())) right(h - 34, kDim, "v");
		footer("A play    Y options    B quit");
		break;
	}
	case Mode::Options: {
		header(game.name);
		std::string info;
		if (!YnoGames::IsDownloaded(game.id) && shared->info_files <= 0 && shared->info_game == game.id) {
			info = "Not downloaded yet";
		} else if (shared->info_game != game.id || shared->info_files < 0) {
			info = "Checking the card...";
		} else {
			info = "On the card: " + Files(shared->info_files) + ", " + Megabytes(shared->info_bytes);
			info += YnoGames::IsComplete(game.id) ? "  (complete)" : "  (of " + YnoGames::ApproxSize(game.approx_mb).substr(6) + ")";
		}
		text(kMargin, kListTop, kDim, info);
		const char* items[] = {"Play", "Download all files", "Verify files", "Re-download files", "Back"};
		const char* hints[][2] = {
			{"Start the game. Files it needs and does not", "have yet are downloaded while you play."},
			{"Gets every file now, so the game never waits", "for one. Resumes where it stopped."},
			{"Compares the size of every file on the card", "with the server and fixes the ones that differ."},
			{"Replaces every file on the card with a fresh", "copy from the server. Saves are kept."},
			{"", ""},
		};
		for (int i = 0; i < 5; ++i) {
			int y = kListTop + 24 + i * kRowHeight;
			text(kMargin, y, i == option ? kBright : kText, i == option ? ">" : "");
			text(kMargin + 12, y, i == option ? kBright : kText, items[i]);
		}
		text(kMargin, kListTop + 24 + 5 * kRowHeight + 10, kDim, hints[option][0]);
		text(kMargin, kListTop + 24 + 5 * kRowHeight + 24, kDim, hints[option][1]);
		footer("A select    B back");
		break;
	}
	case Mode::Confirm: {
		header(game.name);
		if (confirm_kind == YnoGames::Kind::DownloadAll) {
			text(kMargin, kListTop, kBright, "Download all files of this game?");
			text(kMargin, kListTop + 20, kText, "Size: " + YnoGames::ApproxSize(game.approx_mb) + ".");
			text(kMargin, kListTop + 34, kText, "On a slow connection this can take hours;");
			text(kMargin, kListTop + 48, kText, "stop it with B and continue another time.");
			text(kMargin, kListTop + 62, kText, "Files already on the card are kept.");
		} else {
			text(kMargin, kListTop, kBright, "Re-download " + Files(shared->info_files) + " (" + Megabytes(shared->info_bytes) + ")?");
			text(kMargin, kListTop + 20, kText, "Every file of this game on the card is replaced");
			text(kMargin, kListTop + 34, kText, "with a fresh copy from the server.");
			text(kMargin, kListTop + 48, kText, "Saves are kept.");
		}
		footer("A yes    B no");
		break;
	}
	case Mode::Busy: {
		header(game.name);
		text(kMargin, kListTop, kText, !report ? "Working..." : report->kind == YnoGames::Kind::Verify ? "Verifying files..." :
				report->kind == YnoGames::Kind::Redownload ? "Re-downloading files..." : "Downloading all files...");
		text(kMargin, kListTop + 16, kDim, "This can take a while on a slow connection.");
		text(kMargin, kListTop + 30, kDim, "Cancelling keeps what is already downloaded.");
		footer("B cancel");
		break;
	}
	case Mode::Report: {
		header(game.name);
		int y = kListTop;
		auto line = [&](const Color& c, const std::string& s) {
			text(kMargin, y, c, s);
			y += kRowHeight;
		};
		if (report) {
			using K = YnoGames::Kind;
			if (!report->error.empty()) {
				line(kWarn, report->error);
				y += 6;
				line(kDim, "Check the internet connection and try again.");
			} else {
				line(kBright, report->kind == K::Verify ? "Verification finished" :
						report->kind == K::Redownload ? "Re-download finished" : "Download finished");
				y += 6;
				if (report->kind == K::DownloadAll) {
					line(kText, "Already on the card: " + std::to_string(report->present));
					line(report->updated ? kAccent : kText, "Downloaded now:      " + std::to_string(report->updated));
				} else {
					line(kText, "Files checked:  " + std::to_string(report->total));
					if (report->kind == K::Verify) line(kText, "Correct:        " + std::to_string(report->ok));
					line(report->updated ? kAccent : kText, std::string(report->kind == K::Verify ? "Repaired:       " : "Downloaded:     ") + std::to_string(report->updated));
				}
				if (report->not_on_server) line(kDim, "Not on server:  " + std::to_string(report->not_on_server));
				if (report->failed) line(kWarn, "Failed:         " + std::to_string(report->failed));
				if (report->cancelled) line(kWarn, "Not done yet:    " + std::to_string(report->cancelled));
				if (report->failed || report->cancelled) {
					y += 6;
					line(kDim, "Run it again to continue.");
				} else if (report->kind == K::DownloadAll) {
					y += 6;
					line(kAccent, "The game is complete on the card.");
				}
			}
		}
		footer("A ok");
		break;
	}
	case Mode::Sync: {
		header(game.name);
		text(kMargin, kListTop, kText, "Checking the save on the server...");
		text(kMargin, kListTop + 16, kDim, "Slot 1 follows your YNO account.");
		footer("B back");
		break;
	}
	case Mode::Conflict: {
		header(game.name);
		text(kMargin, kListTop, kBright, "Slot 1 differs on the server and here,");
		text(kMargin, kListTop + 14, kBright, "and both changed since the last sync.");
		const std::string items[][2] = {
			{"Use the server save", "server:  " + (pull ? pull->server_time : std::string())},
			{"Keep the save on this console", "console: " + (pull ? pull->card_time : std::string())},
		};
		for (int i = 0; i < 2; ++i) {
			int y = kListTop + 36 + i * 32;
			text(kMargin, y, i == choice ? kBright : kText, i == choice ? ">" : "");
			text(kMargin + 12, y, i == choice ? kBright : kText, items[i][0]);
			text(kMargin + 12, y + 14, kDim, items[i][1]);
		}
		const char* hints[][2] = {
			{"The save of this console is kept on the card", "as Save01.before-sync.lsd."},
			{"It is uploaded. The server's save is kept on", "the card as Save01.server-copy.lsd."},
		};
		text(kMargin, kListTop + 112, kDim, hints[choice][0]);
		text(kMargin, kListTop + 126, kDim, hints[choice][1]);
		footer("A choose    B back");
		break;
	}
	}
}

// ---------------------------------------------------------------- input

void Scene_YnoGames::vUpdate() {
	if (starting) {
		StartGame();
		return;
	}

	DevHooks();

	if (shared->dirty.exchange(false)) {
		dirty = true;
	}

	switch (mode) {
	case Mode::List:
		UpdateList();
		break;
	case Mode::Options:
	case Mode::Confirm:
		UpdateOptions();
		break;
	case Mode::Busy:
		if (report && report->done) {
			mode = Mode::Report;
			dirty = true;
		} else if (Input::IsTriggered(Input::CANCEL)) {
			YnoNet::CancelBatch();
		}
		break;
	case Mode::Report:
		if (Input::IsTriggered(Input::DECISION) || Input::IsTriggered(Input::CANCEL)) {
			mode = Mode::Options;
			dirty = true;
		}
		break;
	case Mode::Sync:
	case Mode::Conflict:
		UpdateSync();
		break;
	}
}

void Scene_YnoGames::UpdateSync() {
	if (mode == Mode::Sync) {
		if (pull && pull->finished) {
			if (pull->result == YnoSaveSync::Result::Conflict) {
				mode = Mode::Conflict;
				choice = 1;
				dirty = true;
			} else {
				LaunchGame();
			}
		} else if (Input::IsTriggered(Input::CANCEL)) {
			// the check goes on in the background; a later start checks again
			pull.reset();
			mode = Mode::List;
			dirty = true;
		}
		return;
	}

	if (Input::IsRepeated(Input::DOWN) || Input::IsRepeated(Input::UP)) {
		choice = 1 - choice;
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
		dirty = true;
	}
	if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		Output::Debug("hub: save conflict of {}: {}", pull->game_id, choice == 0 ? "server" : "card");
		LaunchGame();
	} else if (Input::IsTriggered(Input::CANCEL)) {
		pull.reset();
		mode = Mode::List;
		dirty = true;
	}
}

void Scene_YnoGames::UpdateList() {
	const int count = static_cast<int>(YnoGames::All().size());
	const int rows = (image ? image->height() : Player::screen_height) - kListTop - 22;
	const int visible = std::max(1, rows / kRowHeight);

	// Only up and down move the cursor: a jump by a page on left/right is too easy to hit by accident
	// (a worn stick is enough) and silently changes which game the next button press is about.
	int old = index;
	if (Input::IsRepeated(Input::DOWN)) index = (index + 1) % count;
	if (Input::IsRepeated(Input::UP)) index = (index + count - 1) % count;
	if (index != old) {
		Output::Debug("hub: cursor {} -> {} ({})", old, index, YnoGames::All()[index].id);
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
		if (index < top) top = index;
		if (index >= top + visible) top = index - visible + 1;
		dirty = true;
	}
	if (top > std::max(0, count - visible)) top = std::max(0, count - visible);

	if (Input::IsTriggered(Input::CANCEL)) {
		Scene::Pop();
	} else if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		Output::Debug("hub: A on {} (index {})", YnoGames::All()[index].id, index);
		starting = true;  // the next frame starts the game; this one draws "Loading..."
		dirty = true;
	} else if (Input::IsTriggered(Input::SHIFT)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		OpenOptions();
	}
}

void Scene_YnoGames::OpenOptions() {
	mode = Mode::Options;
	option = 0;
	dirty = true;

	// count what is on the card (a big game has thousands of files)
	const std::string id = YnoGames::All()[index].id;
	Output::Debug("hub: options for {} (index {})", id, index);
	{
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->info_game = id;
		shared->info_files = -1;
	}
	auto state = shared;
	YnoNet::RunAsync([state, id]() {
		auto files = YnoGames::ScanLocal(id);
		long long bytes = 0;
		for (const auto& f : files) bytes += f.size;
		std::lock_guard<std::mutex> lock(state->mutex);
		if (state->info_game == id) {
			state->info_files = static_cast<int>(files.size());
			state->info_bytes = bytes;
		}
	}, [state]() {
		state->dirty = true;
	});
}

void Scene_YnoGames::UpdateOptions() {
	if (mode == Mode::Confirm) {
		if (Input::IsTriggered(Input::DECISION)) {
			StartMaintenance(confirm_kind);
		} else if (Input::IsTriggered(Input::CANCEL)) {
			mode = Mode::Options;
			dirty = true;
		}
		return;
	}

	int old = option;
	if (Input::IsRepeated(Input::DOWN)) option = (option + 1) % 5;
	if (Input::IsRepeated(Input::UP)) option = (option + 4) % 5;
	if (option != old) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cursor));
		dirty = true;
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		mode = Mode::List;
		dirty = true;
	} else if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		switch (option) {
		case 0:
			starting = true;
			mode = Mode::List;  // the loading line is drawn on the list
			dirty = true;
			break;
		case 1:
			confirm_kind = YnoGames::Kind::DownloadAll;
			mode = Mode::Confirm;
			dirty = true;
			break;
		case 2:
			StartMaintenance(YnoGames::Kind::Verify);
			break;
		case 3: {
			std::lock_guard<std::mutex> lock(shared->mutex);
			if (shared->info_files <= 0) {
				break;  // nothing on the card (or still counting)
			}
			confirm_kind = YnoGames::Kind::Redownload;
			mode = Mode::Confirm;
			dirty = true;
			break;
		}
		default:
			mode = Mode::List;
			dirty = true;
			break;
		}
	}
}

void Scene_YnoGames::StartMaintenance(YnoGames::Kind kind) {
	const std::string id = YnoGames::All()[index].id;
	auto state = shared;
	report = YnoGames::StartMaintenance(id, kind, [state]() {
		state->dirty = true;
	});
	mode = Mode::Busy;
	dirty = true;
}

void Scene_YnoGames::StartGame() {
	starting = false;
	if (!YnoSaveSync::Enabled()) {
		LaunchGame();
		return;
	}
	// slot 1 has to be settled before the game can load it
	pull = YnoSaveSync::StartPull(YnoGames::All()[index].id);
	mode = Mode::Sync;
	dirty = true;
}

void Scene_YnoGames::LaunchGame() {
	mode = Mode::List;
	dirty = true;
	auto check = std::move(pull);

	const auto& game = YnoGames::All()[index];
	const std::string dir = YnoGames::Directory(game.id);
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	auto fs = FileFinder::Root().Create(dir);
	if (!fs) {
		Output::Warning("Cannot open {}", dir);
		return;
	}

	YnoGames::SetLastPlayed(game.id);
	YnoNet::GetConfig().game_id = game.id;
	YnoSession::Reset();
	YnoSession::Start();
	AsyncHandler::Reset();
	Scene_Logo::ResetIndexRequest();
	FileFinder::SetGameFilesystem(fs);

	if (check) {
		bool upload_pending = false;
		if (check->result == YnoSaveSync::Result::Conflict) {
			YnoSaveSync::Resolve(*check, choice == 0);
			upload_pending = choice != 0;  // the upload reports itself when it is done
		}
		if (!upload_pending) YnoSession::AddLocalMessage(YnoSaveSync::Describe(*check));
	}

	// Scene_Logo downloads index.json and the startup files, then starts the game
	Scene::Push(std::make_shared<Scene_Logo>());
}

// development aids (headless tests); harmless otherwise
//   YNO_HUB_PICK=<id>               pick that game after a short while
//   YNO_HUB_ACTION=<id>:verify|redownload|all   run a maintenance and print the report
//   YNO_TEST_FETCH="<id>|<path>"    download one file the way the game would and print the result
//   YNO_HUB_UI=options|confirm      open that screen of the first game after a short while
//   YNO_HUB_KEYS="ddy"              press keys on the list: u d a b y (up, down, A, B, Y)
void Scene_YnoGames::DevHooks() {
	static const char* pick = getenv("YNO_HUB_PICK");
	static const char* action = getenv("YNO_HUB_ACTION");
	static const char* fetch = getenv("YNO_TEST_FETCH");
	static const char* ui = getenv("YNO_HUB_UI");
	static const char* keys = getenv("YNO_HUB_KEYS");
	static const char* sync_ui = getenv("YNO_SAVESYNC_UI");
	static int frames = 0;
	static bool fetched = false;
	if (!pick && !action && !fetch && !ui && !keys && !sync_ui) return;
	++frames;

	if (keys && frames >= 60) {
		// press one key of the list every 12 frames: down on the first frame, up on the fourth
		static std::vector<SDL_Scancode> script;
		if (script.empty()) {
			for (const char* c = keys; *c; ++c) {
				switch (*c) {
				case 'u': script.push_back(SDL_SCANCODE_UP); break;
				case 'd': script.push_back(SDL_SCANCODE_DOWN); break;
				case 'a': script.push_back(SDL_SCANCODE_RETURN); break;
				case 'b': script.push_back(SDL_SCANCODE_ESCAPE); break;
				case 'y': script.push_back(SDL_SCANCODE_LSHIFT); break;
				}
			}
		}
		size_t step = static_cast<size_t>(frames - 60) / 12;
		int phase = (frames - 60) % 12;
		if (step < script.size() && (phase == 0 || phase == 4)) {
			SDL_Event ev{};
			ev.type = phase == 0 ? SDL_KEYDOWN : SDL_KEYUP;
			ev.key.keysym.scancode = script[step];
			ev.key.state = phase == 0 ? SDL_PRESSED : SDL_RELEASED;
			SDL_PushEvent(&ev);
		}
	}

	if (sync_ui && frames == 50) {
		// for the screenshot of the conflict screen; nothing is written
		pull = std::make_shared<YnoSaveSync::Pull>();
		pull->game_id = YnoGames::All()[index].id;
		pull->finished = true;
		pull->result = YnoSaveSync::Result::Conflict;
		pull->server_time = "2026-10-21 19:42 UTC";
		pull->card_time = "2026-10-22 08:15 UTC";
		mode = std::string(sync_ui) == "sync" ? Mode::Sync : Mode::Conflict;
		if (mode == Mode::Sync) pull->finished = false;
		dirty = true;
	}
	if (ui && frames == 50) {
		OpenOptions();
	}
	if (ui && frames == 120 && std::string(ui) == "confirm-all") {
		option = 1;
		confirm_kind = YnoGames::Kind::DownloadAll;
		mode = Mode::Confirm;
		dirty = true;
	}
	if (ui && frames == 90 && std::string(ui) == "options-all") {
		option = 1;
		dirty = true;
	}
	if (ui && frames == 120 && std::string(ui) == "confirm") {
		std::lock_guard<std::mutex> lock(shared->mutex);
		shared->info_files = 3450;      // for the screenshot: pretend a big game is on the card
		shared->info_bytes = 220000000;
		mode = Mode::Confirm;
		dirty = true;
	}

	auto select = [&](const std::string& id) {
		const auto& games = YnoGames::All();
		for (size_t i = 0; i < games.size(); ++i) {
			if (id == games[i].id) index = static_cast<int>(i);
		}
	};

	if (pick && frames == 200) {
		select(pick);
		starting = true;
	}
	if (action && frames == 100) {
		std::string a = action;
		auto colon = a.find(':');
		select(a.substr(0, colon));
		std::string what = a.substr(colon + 1);
		StartMaintenance(what == "redownload" ? YnoGames::Kind::Redownload : what == "all" ? YnoGames::Kind::DownloadAll : YnoGames::Kind::Verify);
	}
	if (action && report && report->done) {
		Output::Debug("maintenance: present={} total={} ok={} updated={} not_on_server={} failed={} cancelled={} error='{}'",
			report->present, report->total, report->ok, report->updated, report->not_on_server, report->failed, report->cancelled, report->error);
		report.reset();
		Scene::Pop();
	}
	if (fetch && frames == 100 && !fetched) {
		fetched = true;
		std::string f = fetch;
		auto bar = f.find('|');
		std::string id = f.substr(0, bar), rel = f.substr(bar + 1);
		std::string url = YnoNet::EncodeUrl(YnoNet::GetConfig().data_url + id + "/" + rel);
		Output::Debug("fetch test: {}", url);
		YnoNet::DownloadFile(url, YnoGames::Directory(id) + "/" + rel, [rel](bool ok, long status) {
			Output::Debug("fetch test result: {} ok={} status={}", rel, ok, status);
			Scene::Pop();
		});
	}
}
