/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "scene_yno_games.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <system_error>

#include <lcf/data.h>

#include "../async_handler.h"
#include "../audio.h"
#include "../audio_midi.h"
#include "../audio_secache.h"
#include "../cache.h"
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

// ---------------------------------------------------------------- window

Window_YnoGames::Window_YnoGames(int x, int y, int width, int height) :
	Window_Selectable(x, y, width, height),
	online(YnoGames::All().size(), -1) {
	column_max = 1;
}

void Window_YnoGames::Refresh() {
	item_max = static_cast<int>(YnoGames::All().size());
	CreateContents();
	contents->Clear();
	for (int i = 0; i < item_max; ++i) {
		DrawItem(i);
	}
}

void Window_YnoGames::DrawItem(int index) {
	Rect rect = GetItemRect(index);
	contents->ClearRect(rect);

	const auto& game = YnoGames::All()[index];
	contents->TextDraw(rect.x, rect.y, Font::ColorDefault, game.name);

	if (!YnoGames::IsDownloaded(game.id)) {
		// not on the card yet: it is downloaded when started
		int name_width = Text::GetSize(*Font::Default(), game.name).width;
		contents->TextDraw(rect.x + name_width + 8, rect.y, Font::ColorDisabled, "(download)");
	}

	std::string count = online[index] >= 0 ? std::to_string(online[index]) : "-";
	contents->TextDraw(rect.width, rect.y, online[index] > 0 ? Font::ColorHeal : Font::ColorDisabled, count, Text::AlignRight);
}

void Window_YnoGames::SetOnline(int index, int count) {
	if (index < 0 || index >= static_cast<int>(online.size()) || online[index] == count) {
		return;
	}
	online[index] = count;
	if (contents) {
		DrawItem(index);
	}
}

// ---------------------------------------------------------------- scene

// state shared with the threads that ask the server how many players each game has
struct Scene_YnoGames::Shared {
	std::mutex mutex;
	std::vector<int> online;
	std::atomic<bool> dirty{false};
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

void Scene_YnoGames::Start() {
	ResetToDefaultSystem();

	const int w = Player::screen_width;
	const int h = Player::screen_height;

	help_window = std::make_unique<Window_Help>(0, 0, w, 32);
	help_window->SetText("Yume Nikki Online - choose a game");

	list_window = std::make_unique<Window_YnoGames>(0, 32, w, h - 32);
	list_window->Refresh();
	list_window->SetActive(true);
	std::string last = YnoGames::LastPlayed();
	const auto& games = YnoGames::All();
	int start = 0;
	for (size_t i = 0; i < games.size(); ++i) {
		if (last == games[i].id) start = static_cast<int>(i);
	}
	list_window->SetIndex(start);

	load_window = std::make_unique<Window_Help>(w / 4, h / 2 - 16, w / 2, 32);
	load_window->SetText("Loading...", Font::ColorDefault, Text::AlignCenter);
	load_window->SetVisible(false);

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
	load_window->SetVisible(false);
	list_window->Refresh();
	list_window->SetActive(true);
}

void Scene_YnoGames::vUpdate() {
	if (starting) {
		StartGame();
		return;
	}

	if (shared && shared->dirty.exchange(false)) {
		std::lock_guard<std::mutex> lock(shared->mutex);
		for (size_t i = 0; i < shared->online.size(); ++i) {
			list_window->SetOnline(static_cast<int>(i), shared->online[i]);
		}
	}

	help_window->Update();
	list_window->Update();

	// development aid: YNO_HUB_PICK=<game id> picks that game after a short while (no input needed)
	static const char* auto_pick = getenv("YNO_HUB_PICK");
	static int auto_frames = 0;
	bool picked = false;
	if (auto_pick && ++auto_frames == 200) {
		const auto& games = YnoGames::All();
		for (size_t i = 0; i < games.size(); ++i) {
			if (std::string(auto_pick) == games[i].id) list_window->SetIndex(static_cast<int>(i));
		}
		picked = true;
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
	} else if (picked || Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		load_window->SetVisible(true);
		list_window->SetActive(false);
		starting = true;  // draw "Loading..." first
	}
}

void Scene_YnoGames::StartGame() {
	starting = false;

	const auto& game = YnoGames::All()[list_window->GetIndex()];
	const std::string dir = YnoGames::Directory(game.id);
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	auto fs = FileFinder::Root().Create(dir);
	if (!fs) {
		Output::Warning("Cannot open {}", dir);
		load_window->SetVisible(false);
		list_window->SetActive(true);
		return;
	}

	YnoGames::SetLastPlayed(game.id);
	YnoNet::GetConfig().game_id = game.id;
	YnoSession::Reset();
	YnoSession::Start();
	AsyncHandler::Reset();
	Scene_Logo::ResetIndexRequest();
	FileFinder::SetGameFilesystem(fs);

	// Scene_Logo downloads index.json and the startup files, then starts the game
	Scene::Push(std::make_shared<Scene_Logo>());
}
