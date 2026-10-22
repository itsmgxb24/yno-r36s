#include "yno_connection.h"
#include "../external/TinySHA1.hpp"

#include "../native/yno_net.h"
#include "../native/yno_ws.h"

struct YNOConnection::IMPL {
	YnoNet::WsClient ws;
	uint32_t msg_count;
	bool closed;
};

const size_t YNOConnection::MAX_QUEUE_SIZE{ 4088 };



YNOConnection::YNOConnection() : impl(new IMPL) {
	impl->msg_count = 0;
	impl->closed = true;
}

YNOConnection::YNOConnection(YNOConnection&& o)
	: Connection(std::move(o)), impl(std::move(o.impl)) {
}
YNOConnection& YNOConnection::operator=(YNOConnection&& o) {
	Connection::operator=(std::move(o));
	if (this != &o) {
		Close();
		impl = std::move(o.impl);
	}
	return *this;
}

YNOConnection::~YNOConnection() {
	if (impl)
		Close();
}

void YNOConnection::Open(std::string_view uri) {
	if (!impl->closed) {
		Close();
	}
	impl->msg_count = 0;
	impl->closed = false;
	const auto& auth = YnoNet::GetConfig().auth_cookie;
	impl->ws.Open(std::string(uri), auth.empty() ? std::string() : "auth=" + auth);
}

void YNOConnection::Close() {
	Multiplayer::Connection::Close();
	if (impl->closed)
		return;
	impl->closed = true;
	impl->ws.Close();
}

void YNOConnection::Poll() {
	YnoNet::WsClient::Event ev;
	while (impl && impl->ws.PollEvent(ev)) {
		switch (ev.type) {
		case YnoNet::WsClient::Event::Open:
			SetConnected(true);
			DispatchSystem(SystemMessage::OPEN);
			break;
		case YnoNet::WsClient::Event::Close:
			SetConnected(false);
			impl->closed = true;
			DispatchSystem(ev.code == 1028 ? SystemMessage::EXIT : SystemMessage::CLOSE);
			break;
		case YnoNet::WsClient::Event::Message: {
			if (!ev.binary) {
				break;
			}
			std::string_view cstr(ev.data);
			std::vector<std::string_view> mstrs = Split(cstr, Multiplayer::Packet::MSG_DELIM);
			for (auto& mstr : mstrs) {
				auto p = mstr.find(Multiplayer::Packet::PARAM_DELIM);
				if (p == mstr.npos) {
					Dispatch(mstr);
				} else {
					auto namestr = mstr.substr(0, p);
					auto argstr = mstr.substr(p + Multiplayer::Packet::PARAM_DELIM.size());
					Dispatch(namestr, Split(argstr));
				}
			}
			break;
		}
		}
	}
}


template<typename T>
std::string_view as_bytes(const T& v) {
	static_assert(sizeof(v) % 2 == 0, "Unsupported numeric type");
	return std::string_view(
		reinterpret_cast<const char*>(&v),
		sizeof(v)
	);
}

// poor method to test current endian
// need improvement
bool is_big_endian() {
	union endian_tester {
		uint16_t num;
		char layout[2];
	};

	endian_tester t;
	t.num = 1;
	return t.layout[0] == 0;
}

std::string reverse_endian(std::string src) {
	if (src.size() % 2 == 1)
		return src;

	size_t it1{0}, it2{src.size() - 1}, itend{src.size() / 2};
	while (it1 != itend) {
		std::swap(src[it1], src[it2]);
		++it1;
		--it2;
	}
	return src;
}

template<typename T>
std::string as_big_endian_bytes(T v) {
	auto r = as_bytes<T>(v);
	std::string sr{r.data(), r.size()};
	if (is_big_endian())
		return sr;
	else
		return reverse_endian(sr);
}


std::string calculate_header(uint32_t key, uint32_t count, std::string_view msg) {
	std::string hashmsg{YnoNet::GetConfig().room_key};
	hashmsg += as_big_endian_bytes(key);
	hashmsg += as_big_endian_bytes(count);
	hashmsg += msg;

	sha1::SHA1 digest;
	uint32_t digest_result[5];
	digest.processBytes(hashmsg.data(), hashmsg.size());
	digest.getDigest(digest_result);

	std::string r{as_big_endian_bytes(digest_result[0])};
	r += as_big_endian_bytes(count);
	return r;
}

void YNOConnection::Send(std::string_view data) {
	if (!IsConnected())
		return;
	if (impl->ws.IsOpen()) {
		++impl->msg_count;
		auto sendmsg = calculate_header(GetKey(), impl->msg_count, data);
		sendmsg += data;
		impl->ws.Send(std::move(sendmsg), true);
	}
}

void YNOConnection::FlushQueue() {
	auto namecmp = [] (std::string_view v, bool include) {
		return (v != "sr") == include;
	};

	bool include = false;
	while (!m_queue.empty()) {
		std::string bulk;
		while (!m_queue.empty()) {
			auto& e = m_queue.front();
			if (namecmp(e->GetName(), include))
				break;
			auto data = e->ToBytes();
			// send before overflow
			if (bulk.size() + data.size() > MAX_QUEUE_SIZE) {
				Send(bulk);
				bulk.clear();
			}
			if (!bulk.empty())
				bulk += Multiplayer::Packet::MSG_DELIM;
			bulk += data;
			m_queue.pop();
		}
		if (!bulk.empty())
			Send(bulk);
		include = !include;
	}
}
