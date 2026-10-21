/*
 * Native networking for the YNO client (replaces what the browser provides):
 * HTTP downloads on worker threads and the main-thread mailbox that delivers the results.
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_NET_H
#define EP_NATIVE_YNO_NET_H

#include <functional>
#include <string>

namespace YnoNet {
	struct Config {
		std::string game_id;                                  // ynoproject game id, e.g. "yume", "2kki"; empty = show the game list
		std::string server_host = "connect.ynoproject.net";    // game / session / api server
		std::string data_url = "https://ynoproject.net/data/"; // game files: <data_url><game_id>/<file>
		std::string auth_cookie;                               // JWT of a logged-in account; empty = guest
		std::string room_key;                                  // secret that signs /room packets; empty = no room multiplayer
		std::string games_dir;                                 // absolute path; <games_dir>/<game id>/ holds one game
		std::string user_agent = "EasyRPG-YNO-native/0.1 (R36S handheld; unofficial client)";
	};

	Config& GetConfig();

	void Init();
	void Shutdown();

	/** Runs the callbacks queued by worker threads. Call once per frame from the main thread. */
	void Poll();

	/** Queues a closure for the main thread (the next Poll). Thread-safe. */
	void PostToMain(std::function<void()> fn);

	/** Called on the main thread when a download finished: ok + HTTP status (0 = network error). */
	using DoneFn = std::function<void(bool ok, long http_status)>;

	/**
	 * Downloads `url` into `dest_path` on a worker thread (retries on network errors, parent
	 * directories are created, the file appears atomically). `on_done` runs on the main thread.
	 */
	void DownloadFile(std::string url, std::string dest_path, DoneFn on_done);

	/** Number of downloads that are queued or running. */
	int PendingDownloads();

	/** Blocking GET into memory. Worker threads only. */
	bool HttpGet(const std::string& url, std::string& body, long& http_status, const std::string& cookie = std::string());

	/** Blocking request with a body (POST / PUT ...). Worker threads only. */
	bool HttpRequest(const std::string& method, const std::string& url, const std::string& request_body,
			const std::string& content_type, std::string& response_body, long& http_status,
			const std::string& cookie = std::string());

	/** Runs `work` on a worker thread, then `done` on the main thread. */
	void RunAsync(std::function<void()> work, std::function<void()> done);
}

#endif
