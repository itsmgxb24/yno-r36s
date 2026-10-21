/*
 * The game list of the YNO client: shown at start (and when a game is left through the settings menu)
 * so that any game of the YNOproject can be played.
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

#include "../scene.h"
#include "../window_help.h"
#include "../window_selectable.h"

class Window_YnoGames : public Window_Selectable {
public:
	Window_YnoGames(int x, int y, int width, int height);

	/** Redraws the whole list. */
	void Refresh();
	void DrawItem(int index);

	/** Number of players online in game `index` (-1 = unknown). */
	void SetOnline(int index, int count);
	int GetOnline(int index) const { return online[index]; }

private:
	std::vector<int> online;
};

class Scene_YnoGames : public Scene {
public:
	Scene_YnoGames();
	~Scene_YnoGames() override;

	void Start() override;
	void Continue(SceneType prev_scene) override;
	void vUpdate() override;

private:
	struct Shared;

	void StartGame();

	std::unique_ptr<Window_Help> help_window;
	std::unique_ptr<Window_YnoGames> list_window;
	std::unique_ptr<Window_Help> load_window;
	std::shared_ptr<Shared> shared;
	bool starting = false;
};

#endif
