/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_net.h"

#include <curl/curl.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include "output.h"

namespace {
	constexpr int kWorkers = 3;
	constexpr int kMaxAttempts = 5;

	std::mutex work_mutex;
	std::condition_variable work_cv;
	std::deque<std::function<void()>> work_queue;
	std::vector<std::thread> workers;
	std::atomic<bool> quitting{false};
	std::atomic<int> pending{0};

	std::mutex main_mutex;
	std::deque<std::function<void()>> main_queue;

	YnoNet::Config config;

	void WorkerLoop() {
		for (;;) {
			std::function<void()> job;
			{
				std::unique_lock<std::mutex> lock(work_mutex);
				work_cv.wait(lock, [] { return quitting || !work_queue.empty(); });
				if (quitting) {
					return;
				}
				job = std::move(work_queue.front());
				work_queue.pop_front();
			}
			job();
		}
	}

	void Enqueue(std::function<void()> job) {
		{
			std::lock_guard<std::mutex> lock(work_mutex);
			work_queue.push_back(std::move(job));
		}
		work_cv.notify_one();
	}

	size_t WriteToFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
		return fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata));
	}

	size_t WriteToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
		static_cast<std::string*>(userdata)->append(ptr, size * nmemb);
		return size * nmemb;
	}

	int Progress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
		return quitting ? 1 : 0;  // abort running transfers on shutdown
	}

	void Common(CURL* h, const std::string& url) {
		curl_easy_setopt(h, CURLOPT_URL, url.c_str());
		curl_easy_setopt(h, CURLOPT_USERAGENT, config.user_agent.c_str());
		curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
		curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
		curl_easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1L);   // give up when nothing arrives for 40 s
		curl_easy_setopt(h, CURLOPT_LOW_SPEED_TIME, 40L);
		curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, Progress);
	}

	bool DownloadOnce(const std::string& url, const std::string& tmp, long& status) {
		FILE* f = fopen(tmp.c_str(), "wb");
		if (!f) {
			status = 0;
			return false;
		}
		CURL* h = curl_easy_init();
		Common(h, url);
		curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToFile);
		curl_easy_setopt(h, CURLOPT_WRITEDATA, f);
		CURLcode rc = curl_easy_perform(h);
		status = 0;
		curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
		curl_easy_cleanup(h);
		bool flushed = fclose(f) == 0;
		return rc == CURLE_OK && status == 200 && flushed;
	}
}

namespace YnoNet {

Config& GetConfig() {
	return config;
}

void Init() {
	if (!workers.empty()) {
		return;
	}
	if (const char* lv = getenv("YNO_LOG")) {   // YNO_LOG=debug|info: what the client says about itself
		std::string v = lv;
		Output::SetLogLevel(v == "debug" ? LogLevel::Debug : v == "info" ? LogLevel::Info : LogLevel::Warning);
	}
	curl_global_init(CURL_GLOBAL_DEFAULT);
	quitting = false;
	for (int i = 0; i < kWorkers; ++i) {
		workers.emplace_back(WorkerLoop);
	}
}

void Shutdown() {
	quitting = true;
	work_cv.notify_all();
	for (auto& t : workers) {
		t.join();
	}
	workers.clear();
	curl_global_cleanup();
}

void Poll() {
	for (;;) {
		std::function<void()> fn;
		{
			std::lock_guard<std::mutex> lock(main_mutex);
			if (main_queue.empty()) {
				return;
			}
			fn = std::move(main_queue.front());
			main_queue.pop_front();
		}
		fn();
	}
}

void PostToMain(std::function<void()> fn) {
	std::lock_guard<std::mutex> lock(main_mutex);
	main_queue.push_back(std::move(fn));
}

int PendingDownloads() {
	return pending;
}

void DownloadFile(std::string url, std::string dest_path, DoneFn on_done) {
	++pending;
	Enqueue([url = std::move(url), dest = std::move(dest_path), on_done = std::move(on_done)]() {
		bool ok = false;
		long status = 0;

		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(dest).parent_path(), ec);
		const std::string tmp = dest + ".part";

		for (int attempt = 0; attempt < kMaxAttempts && !quitting; ++attempt) {
			ok = DownloadOnce(url, tmp, status);
			if (ok || (status >= 400 && status < 500)) {
				break;  // done, or the file does not exist: retrying does not help
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(500 << attempt));
		}

		if (ok) {
			std::filesystem::rename(tmp, dest, ec);
			ok = !ec;
		} else {
			std::filesystem::remove(tmp, ec);
		}

		PostToMain([ok, status, on_done = std::move(on_done)]() {
			--pending;
			if (on_done) {
				on_done(ok, status);
			}
		});
	});
}

bool HttpRequest(const std::string& method, const std::string& url, const std::string& request_body,
		const std::string& content_type, std::string& response_body, long& http_status,
		const std::string& cookie) {
	CURL* h = curl_easy_init();
	if (!h) {
		return false;
	}
	Common(h, url);
	curl_easy_setopt(h, CURLOPT_CUSTOMREQUEST, method.c_str());
	curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToString);
	curl_easy_setopt(h, CURLOPT_WRITEDATA, &response_body);

	struct curl_slist* headers = nullptr;
	if (!content_type.empty()) {
		headers = curl_slist_append(headers, ("Content-Type: " + content_type).c_str());
	}
	if (!headers) {
		headers = nullptr;
	}
	if (headers) {
		curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
	}
	if (!request_body.empty() || method == "POST" || method == "PUT") {
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, request_body.c_str());
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
	}
	const std::string& jar = cookie.empty() && !config.auth_cookie.empty() ? "auth=" + config.auth_cookie : cookie;
	if (!jar.empty()) {
		curl_easy_setopt(h, CURLOPT_COOKIE, jar.c_str());
	}

	CURLcode rc = curl_easy_perform(h);
	http_status = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_status);
	if (headers) {
		curl_slist_free_all(headers);
	}
	curl_easy_cleanup(h);
	return rc == CURLE_OK;
}

bool HttpGet(const std::string& url, std::string& body, long& http_status, const std::string& cookie) {
	return HttpRequest("GET", url, std::string(), std::string(), body, http_status, cookie);
}

void RunAsync(std::function<void()> work, std::function<void()> done) {
	Enqueue([work = std::move(work), done = std::move(done)]() {
		work();
		if (done) {
			PostToMain(done);
		}
	});
}

}
