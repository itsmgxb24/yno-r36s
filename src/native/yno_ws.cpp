/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "yno_ws.h"

#include <curl/curl.h>
#include <poll.h>

#include <chrono>
#include <thread>

#include "yno_net.h"

namespace YnoNet {

WsClient::~WsClient() {
	Close();
}

void WsClient::Push(Event ev) {
	std::lock_guard<std::mutex> lock(mutex);
	in.push_back(std::move(ev));
}

void WsClient::Open(const std::string& url, const std::string& cookie) {
	Close();
	stop = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		out.clear();
		in.clear();
	}
	thread = std::thread([this, url, cookie] { Run(url, cookie); });
}

void WsClient::Send(std::string data, bool binary) {
	if (!open) {
		return;
	}
	std::lock_guard<std::mutex> lock(mutex);
	out.emplace_back(std::move(data), binary);
}

bool WsClient::PollEvent(Event& ev) {
	std::lock_guard<std::mutex> lock(mutex);
	if (in.empty()) {
		return false;
	}
	ev = std::move(in.front());
	in.pop_front();
	return true;
}

void WsClient::Close() {
	stop = true;
	if (thread.joinable()) {
		thread.join();
	}
	open = false;
}

void WsClient::Run(std::string url, std::string cookie) {
	CURL* h = curl_easy_init();
	if (!h) {
		Event ev;
		ev.type = Event::Close;
		ev.code = 1006;
		Push(std::move(ev));
		return;
	}

	curl_easy_setopt(h, CURLOPT_URL, url.c_str());
	curl_easy_setopt(h, CURLOPT_USERAGENT, GetConfig().user_agent.c_str());
	curl_easy_setopt(h, CURLOPT_CONNECT_ONLY, 2L);  // websocket: do the handshake, then send/recv by hand
	curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 20L);
	if (!cookie.empty()) {
		curl_easy_setopt(h, CURLOPT_COOKIE, cookie.c_str());
	}

	Event closed;
	closed.type = Event::Close;
	closed.code = 1006;

	if (curl_easy_perform(h) != CURLE_OK) {
		Push(closed);
		curl_easy_cleanup(h);
		return;
	}

	curl_socket_t sock = CURL_SOCKET_BAD;
	curl_easy_getinfo(h, CURLINFO_ACTIVESOCKET, &sock);
	if (sock == CURL_SOCKET_BAD) {
		Push(closed);
		curl_easy_cleanup(h);
		return;
	}

	open = true;
	{
		Event ev;
		ev.type = Event::Open;
		Push(std::move(ev));
	}

	std::string message;
	bool message_binary = false;
	bool lost = false;
	bool server_closed = false;

	while (!stop && !lost && !server_closed) {
		// 1. everything the main thread queued
		for (;;) {
			std::pair<std::string, bool> item;
			{
				std::lock_guard<std::mutex> lock(mutex);
				if (out.empty()) {
					break;
				}
				item = std::move(out.front());
				out.pop_front();
			}
			size_t done = 0;
			while (done < item.first.size() && !lost) {
				size_t sent = 0;
				CURLcode rc = curl_ws_send(h, item.first.data() + done, item.first.size() - done, &sent, 0,
						item.second ? CURLWS_BINARY : CURLWS_TEXT);
				done += sent;
				if (rc == CURLE_AGAIN) {
					struct pollfd pfd = {sock, POLLOUT, 0};
					poll(&pfd, 1, 50);
				} else if (rc != CURLE_OK) {
					lost = true;
				}
			}
		}

		// 2. everything that arrived (TLS may hold data poll() cannot see, so read until AGAIN)
		for (;;) {
			char buffer[16384];
			size_t got = 0;
			const struct curl_ws_frame* meta = nullptr;
			CURLcode rc = curl_ws_recv(h, buffer, sizeof(buffer), &got, &meta);
			if (rc == CURLE_AGAIN) {
				break;
			}
			if (rc != CURLE_OK || !meta) {
				lost = true;
				break;
			}
			if (meta->flags & CURLWS_CLOSE) {
				closed.code = got >= 2 ? ((static_cast<unsigned char>(buffer[0]) << 8) | static_cast<unsigned char>(buffer[1])) : 1005;
				server_closed = true;
				break;
			}
			if (meta->flags & (CURLWS_PING | CURLWS_PONG)) {
				continue;  // libcurl answers pings itself
			}
			if (meta->flags & (CURLWS_TEXT | CURLWS_BINARY)) {
				message_binary = (meta->flags & CURLWS_BINARY) != 0;
			}
			message.append(buffer, got);
			if (meta->bytesleft == 0 && !(meta->flags & CURLWS_CONT)) {
				Event ev;
				ev.type = Event::Message;
				ev.binary = message_binary;
				ev.data = std::move(message);
				message.clear();
				Push(std::move(ev));
			}
		}

		// 3. sleep until the socket is readable (or a short tick, to notice the out queue)
		if (!lost && !server_closed) {
			struct pollfd pfd = {sock, POLLIN, 0};
			poll(&pfd, 1, 20);
		}
	}

	if (!lost && !server_closed) {
		size_t sent = 0;
		curl_ws_send(h, "", 0, &sent, 0, CURLWS_CLOSE);  // we asked for the close: no event for the caller
	} else {
		Push(closed);
	}
	open = false;
	curl_easy_cleanup(h);
}

}
