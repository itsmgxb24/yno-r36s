/*
 * Minimal WebSocket client on top of libcurl (curl_ws_*). One thread per socket;
 * the main thread talks to it through two queues.
 *
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_NATIVE_YNO_WS_H
#define EP_NATIVE_YNO_WS_H

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace YnoNet {

class WsClient {
public:
	struct Event {
		enum Type { Open, Message, Close };
		Type type = Open;
		bool binary = false;
		int code = 0;          // Close: websocket close code (1006 = connection lost)
		std::string data;      // Message: the payload
	};

	WsClient() = default;
	WsClient(const WsClient&) = delete;
	WsClient& operator=(const WsClient&) = delete;
	~WsClient();

	/** Starts connecting on a background thread. `cookie` is sent as the Cookie header. */
	void Open(const std::string& url, const std::string& cookie);

	/** Queues a message. Dropped while the socket is not open. */
	void Send(std::string data, bool binary);

	/** Takes the next event, if any. Main thread. */
	bool PollEvent(Event& ev);

	/** Closes the socket and joins the thread. */
	void Close();

	bool IsOpen() const { return open; }

private:
	void Run(std::string url, std::string cookie);
	void Push(Event ev);

	std::thread thread;
	std::atomic<bool> stop{false};
	std::atomic<bool> open{false};
	std::mutex mutex;
	std::deque<std::pair<std::string, bool>> out;
	std::deque<Event> in;
};

}

#endif
