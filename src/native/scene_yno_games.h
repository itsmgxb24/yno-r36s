/*
 * The game list of the YNO client: a plain text menu shown at start (and when a game is left through
 * the settings menu) so that any game of the YNOproject can be played, verified or downloaded again.
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_SCENE_YNO_GAMES_H
#define EP_NATIVE_SCENE_YNO_GAMES_H

#include <memory>
#include <string>

#include "../bitmap.h"
#include "../scene.h"
#include "yno_games.h"

class Scene_YnoGames : public Scene {
public:
	Scene_YnoGames();
	~Scene_YnoGames() override;

	void Start() override;
	void Continue(SceneType prev_scene) override;
	void vUpdate() override;
	void DrawBackground(Bitmap& dst) override;

private:
	enum class Mode { List, Options, Confirm, Busy, Report };

	class Screen;
	struct Shared;

	void DrawScreen(Bitmap& dst);
	void Render();
	void UpdateList();
	void UpdateOptions();
	void StartGame();
	void OpenOptions();
	void StartMaintenance(YnoGames::Kind kind);
	void DevHooks();

	Mode mode = Mode::List;
	int index = 0;         // selected game
	int top = 0;           // first visible row of the list
	int option = 0;        // selected entry of the options of a game
	YnoGames::Kind confirm_kind = YnoGames::Kind::Redownload;  // what the confirmation screen is about
	bool dirty = true;
	bool starting = false;

	std::unique_ptr<Screen> screen;
	BitmapRef image;
	std::shared_ptr<Shared> shared;
	std::shared_ptr<YnoGames::Report> report;
};

#endif
