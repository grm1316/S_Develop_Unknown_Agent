#pragma once

#include <cstdint>
#include <string>
#include <cstring>
#include <uchar.h>

struct Ip final {
	static const int Size = 4;

	// constructor
	Ip() {}
	Ip(const uint32_t r) : ip_(r) {}
	Ip(const std::string& r) {
		unsigned int a, b, c, d;
		int res = sscanf(r.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d);
		if (res != Size) {
			fprintf(stderr, "Ip::Ip sscanf return %d r=%s\n", res, r.c_str());
			return;
		}
		ip_ = (a << 24) | (b << 16) | (c << 8) | d;
	}
    Ip(uint8_t* r) { memcpy(&this->ip_, r, sizeof(uint32_t)); }

	// casting operator
	operator uint32_t() const { return ip_; } // default
	explicit operator std::string() const {
		char buf[32]; // enough size
		sprintf(buf, "%u.%u.%u.%u",
			(ip_ & 0xFF000000) >> 24,
			(ip_ & 0x00FF0000) >> 16,
			(ip_ & 0x0000FF00) >> 8,
			(ip_ & 0x000000FF));
		return std::string(buf);
	}

	// comparison operator
	bool operator == (const Ip& r) const { return ip_ == r.ip_; }
	bool operator == (const unsigned long& r) const { return ip_ == r; }

    bool isEmtpy() { return this->ip_ == NULL; }

	bool isLocalHost() const { // 127.*.*.*
		uint8_t prefix = (ip_ & 0xFF000000) >> 24;
		return prefix == 0x7F;
	}

	bool isBroadcast() const { // 255.255.255.255
		return ip_ == 0xFFFFFFFF;
	}

	bool isMulticast() const { // 224.0.0.0 ~ 239.255.255.255
		uint8_t prefix = (ip_ & 0xFF000000) >> 24;
		return prefix >= 0xE0 && prefix < 0xF0;
	}

protected:
    uint32_t ip_{};
};
