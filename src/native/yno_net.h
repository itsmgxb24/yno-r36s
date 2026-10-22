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

	/**
	 * Percent-encodes everything in a URL that is not unreserved, '/' or ':'. The browser does this
	 * for the web player; libcurl refuses a URL with a space in it (file names have many).
	 */
	std::string EncodeUrl(const std::string& url);

	/** Called on the main thread when a download finished: ok + HTTP status (0 = network error). */
	using DoneFn = std::function<void(bool ok, long http_status)>;

	/**
	 * Downloads `url` into `dest_path` on a worker thread (retries on network errors, parent
	 * directories are created, the file appears atomically). `on_done` runs on the main thread.
	 */
	void DownloadFile(std::string url, std::string dest_path, DoneFn on_done);

	/** Number of downloads that are queued or running. */
	int PendingDownloads();

	enum class CheckResult {
		Ok,          // the file on the card has the size the server reports
		Updated,     // it was (re)downloaded
		NotOnServer, // the server has no such file (404): left alone
		Failed,      // network error or the download failed
		Cancelled,   // the batch was cancelled before this file was reached
	};
	using CheckFn = std::function<void(CheckResult)>;

	/**
	 * Compares the size of `dest_path` with what the server reports for `url` (a HEAD request) and
	 * downloads the file again when it differs. With `force` the file is always downloaded again.
	 * `on_done` runs on the main thread. The server publishes no checksums, the size is what can be checked.
	 */
	void CheckFile(std::string url, std::string dest_path, bool force, CheckFn on_done);

	/** What the progress bar shows. Main thread. */
	struct Progress {
		bool active = false;       // something is being transferred
		bool batch = false;        // an explicit batch (verify / re-download) rather than files the game asked for
		std::string label;         // batch: "Verifying files"; otherwise "Downloading"
		std::string current;       // name of the file that was started last
		int done = 0;
		int total = 0;
	};
	Progress GetProgress();

	/** Starts counting a batch of `total` CheckFile calls. Main thread. */
	void BeginBatch(const std::string& label, int total);
	void EndBatch();
	/** Queued batch jobs finish immediately as Cancelled; running ones complete. */
	void CancelBatch();
	bool IsBatchCancelled();

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
