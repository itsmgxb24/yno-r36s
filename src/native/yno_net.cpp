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

	std::mutex main_mutex;
	std::deque<std::function<void()>> main_queue;

	YnoNet::Config config;

	// progress bookkeeping, main thread only (except the cancel flag)
	int pending = 0;                 // downloads the game asked for that are not finished
	int lazy_total = 0, lazy_done = 0;
	std::string current_name;
	bool batch_active = false;
	int batch_total = 0, batch_done = 0;
	std::string batch_label;
	std::atomic<bool> batch_cancelled{false};

	// One libcurl handle per worker thread: it keeps the connection (and the TLS session) open
	// between files, which matters a lot on a slow link.
	struct ThreadHandle {
		CURL* h = nullptr;
		~ThreadHandle() {
			if (h) curl_easy_cleanup(h);
		}
		CURL* Get() {
			if (!h) {
				h = curl_easy_init();
			} else {
				curl_easy_reset(h);
			}
			return h;
		}
	};
	thread_local ThreadHandle thread_handle;

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

	int XferInfo(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
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
		curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, XferInfo);
	}

	bool DownloadOnce(const std::string& url, const std::string& tmp, long& status) {
		FILE* f = fopen(tmp.c_str(), "wb");
		if (!f) {
			status = 0;
			return false;
		}
		CURL* h = thread_handle.Get();
		Common(h, url);
		curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, WriteToFile);
		curl_easy_setopt(h, CURLOPT_WRITEDATA, f);
		CURLcode rc = curl_easy_perform(h);
		status = 0;
		curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
		bool flushed = fclose(f) == 0;
		return rc == CURLE_OK && status == 200 && flushed;
	}

	// downloads with retries; the file appears atomically at `dest`
	bool DownloadWithRetry(const std::string& url, const std::string& dest, long& status) {
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(dest).parent_path(), ec);
		const std::string tmp = dest + ".part";

		bool ok = false;
		status = 0;
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
		return ok;
	}

	// size of the file on the server (-1 = not reported)
	bool HeadSize(const std::string& url, long long& size, long& status) {
		CURL* h = thread_handle.Get();
		Common(h, url);
		curl_easy_setopt(h, CURLOPT_NOBODY, 1L);
		CURLcode rc = curl_easy_perform(h);
		status = 0;
		curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
		curl_off_t length = -1;
		curl_easy_getinfo(h, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
		size = length;
		return rc == CURLE_OK;
	}

	std::string BaseName(const std::string& path) {
		auto slash = path.find_last_of('/');
		return slash == std::string::npos ? path : path.substr(slash + 1);
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

std::string EncodeUrl(const std::string& url) {
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	out.reserve(url.size() + 16);
	for (unsigned char c : url) {
		bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
				c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':';
		if (plain) {
			out += static_cast<char>(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

int PendingDownloads() {
	return pending;
}

void DownloadFile(std::string url, std::string dest_path, DoneFn on_done) {
	++pending;
	++lazy_total;
	current_name = BaseName(dest_path);
	Enqueue([url = std::move(url), dest = std::move(dest_path), on_done = std::move(on_done)]() {
		long status = 0;
		bool ok = DownloadWithRetry(url, dest, status);
		PostToMain([ok, status, on_done = std::move(on_done)]() {
			--pending;
			++lazy_done;
			if (pending == 0) {
				lazy_total = lazy_done = 0;  // the bar starts from zero for the next burst of files
			}
			if (on_done) {
				on_done(ok, status);
			}
		});
	});
}

void CheckFile(std::string url, std::string dest_path, bool force, CheckFn on_done) {
	current_name = BaseName(dest_path);
	Enqueue([url = std::move(url), dest = std::move(dest_path), force, on_done = std::move(on_done)]() {
		CheckResult result = CheckResult::Ok;

		if (batch_cancelled || quitting) {
			result = CheckResult::Cancelled;
		} else {
			bool download = force;
			if (!force) {
				std::error_code ec;
				auto local = std::filesystem::file_size(dest, ec);
				long long remote = -1;
				long status = 0;
				bool reached = false;
				for (int attempt = 0; attempt < 3 && !quitting; ++attempt) {
					reached = HeadSize(url, remote, status);
					if (reached) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(500 << attempt));
				}
				if (!reached) {
					result = CheckResult::Failed;
				} else if (status == 404) {
					result = CheckResult::NotOnServer;
				} else if (status != 200) {
					result = CheckResult::Failed;
				} else {
					download = ec || (remote >= 0 && static_cast<long long>(local) != remote);
				}
			}
			if (download && (result == CheckResult::Ok)) {
				long status = 0;
				if (DownloadWithRetry(url, dest, status)) {
					result = CheckResult::Updated;
				} else {
					result = status == 404 ? CheckResult::NotOnServer : CheckResult::Failed;
				}
			}
		}

		PostToMain([result, on_done = std::move(on_done)]() {
			if (batch_active) {
				++batch_done;
			}
			if (on_done) {
				on_done(result);
			}
		});
	});
}

Progress GetProgress() {
	Progress p;
	p.current = current_name;
	if (batch_active) {
		p.active = true;
		p.batch = true;
		p.label = batch_label;
		p.done = batch_done;
		p.total = batch_total;
	} else if (pending > 0) {
		p.active = true;
		p.label = "Downloading";
		p.done = lazy_done;
		p.total = lazy_total;
	}
	return p;
}

void BeginBatch(const std::string& label, int total) {
	batch_active = true;
	batch_label = label;
	batch_total = total;
	batch_done = 0;
	batch_cancelled = false;
}

void EndBatch() {
	batch_active = false;
	batch_total = batch_done = 0;
	batch_cancelled = false;
}

void CancelBatch() {
	batch_cancelled = true;
}

bool IsBatchCancelled() {
	return batch_cancelled;
}

bool HttpRequest(const std::string& method, const std::string& url, const std::string& request_body,
		const std::string& content_type, std::string& response_body, long& http_status,
		const std::string& cookie) {
	CURL* h = thread_handle.Get();
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
		curl_easy_setopt(h, CURLOPT_HTTPHEADER, headers);
	}
	if (!request_body.empty() || method == "POST" || method == "PUT") {
		curl_easy_setopt(h, CURLOPT_POSTFIELDS, request_body.c_str());
		curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(request_body.size()));
	}
	std::string jar = cookie.empty() && !config.auth_cookie.empty() ? "auth=" + config.auth_cookie : cookie;
	if (!jar.empty()) {
		curl_easy_setopt(h, CURLOPT_COOKIE, jar.c_str());
	}

	CURLcode rc = curl_easy_perform(h);
	http_status = 0;
	curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &http_status);
	if (headers) {
		curl_slist_free_all(headers);
	}
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
