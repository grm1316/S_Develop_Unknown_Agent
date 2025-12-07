#pragma once

#include <functional>
#include <cstdint>
#include <cstring>
#include <string>

// ----------------------------------------------------------------------------
// Mac
// ----------------------------------------------------------------------------
struct Mac final {
	static constexpr int Size = 6;

	// constructor
	Mac() {}
	Mac(const Mac& r) { memcpy(this->mac_, r.mac_, Size); }
	Mac(const uint8_t* r) { memcpy(this->mac_, r, Size); }
	Mac(const std::string& r) {
		std::string s;
		for(char ch: r) {
			if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'))
				s += ch;
		}
		int res = sscanf(s.c_str(), "%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx", &mac_[0], &mac_[1], &mac_[2], &mac_[3], &mac_[4], &mac_[5]);
		if (res != Size) {
			fprintf(stderr, "Mac::Mac sscanf return %d r=%s\n", res, r.c_str());
			return;
		}
}

	// assign operator
	Mac& operator = (const Mac& r) { memcpy(this->mac_, r.mac_, Size); return *this; }

	// casting operator
	explicit operator uint8_t*() const { return const_cast<uint8_t*>(mac_); }
	explicit operator std::string() const {
		char buf[20]; // enough size
		sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac_[0], mac_[1], mac_[2], mac_[3], mac_[4], mac_[5]);
		return std::string(buf);
	}

	// comparison operator
	bool operator == (const Mac& r) const { return memcmp(mac_, r.mac_, Size) == 0; }
	bool operator != (const Mac& r) const { return memcmp(mac_, r.mac_, Size) != 0; }
	bool operator < (const Mac& r) const { return memcmp(mac_, r.mac_, Size) < 0; }
	bool operator > (const Mac& r) const { return memcmp(mac_, r.mac_, Size) > 0; }
	bool operator <= (const Mac& r) const { return memcmp(mac_, r.mac_, Size) <= 0; }
	bool operator >= (const Mac& r) const { return memcmp(mac_, r.mac_, Size) >= 0; }
	bool operator == (const uint8_t* r) const { return memcmp(mac_, r, Size) == 0; }

	void clear() {
		*this = nullMac();
	}

	bool isNull() const {
		return *this == nullMac();
	}

	bool isBroadcast() const { // FF:FF:FF:FF:FF:FF
		return *this == broadcastMac();
	}

	bool isMulticast() const { // 01:00:5E:0*
		return mac_[0] == 0x01 && mac_[1] == 0x00 && mac_[2] == 0x5E && (mac_[3] & 0x80) == 0x00;
	}

	static Mac randomMac() {
		Mac res;
		for (int i = 0; i < Size; i++)
			res.mac_[i] = uint8_t(rand() % 256);
		res.mac_[0] &= 0x7F;
		return res;
	}

	static Mac& nullMac() {
		static uint8_t _value[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
		static Mac res(_value);
		return res;
	}

	static Mac& broadcastMac() {
		static uint8_t _value[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
		static Mac res(_value);
		return res;
	}


protected:
	uint8_t mac_[Size];
};

// ----------------------------------------------------------------------------
// GTEST
// ----------------------------------------------------------------------------

#ifdef GTEST
#include <gtest/gtest.h>

static constexpr uint8_t _temp[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};

TEST(Mac, ctorTest) {
	Mac mac1; // ()

	Mac mac2{mac1}; // (const Mac& r)

	Mac mac3(_temp); // (const uint8_t* r)

	Mac mac4(std::string("001122-334455")); // (const std::string& r)
	EXPECT_EQ(mac3, mac4);

	Mac mac5("001122-334455"); // (const std::string& r)
	EXPECT_EQ(mac3, mac5);
}

TEST(Mac, castingTest) {
	Mac mac("001122-334455");

	const uint8_t* uc = (uint8_t*)mac; // operator uint8_t*()
	uint8_t temp[Mac::Size];
	for (int i = 0; i < Mac::Size; i++)
		temp[i] = *uc++;
	EXPECT_TRUE(memcmp(&mac, temp, 6) == 0);

	std::string s2 = std::string(mac); // operator std::string()
	EXPECT_EQ(s2, "00:11:22:33:44:55");
}

TEST(Mac, funcTest) {
	Mac mac;

	mac.clear();
	EXPECT_TRUE(mac.isNull());

	mac = std::string("FF:FF:FF:FF:FF:FF");
	EXPECT_TRUE(mac.isBroadcast());

	mac = std::string("01:00:5E:00:11:22");
	EXPECT_TRUE(mac.isMulticast());
}

#include <map>
TEST(Mac, mapTest) {
	typedef std::map<Mac, int> MacMap;
	MacMap m;
	m.insert(std::make_pair(Mac("001122-334455"), 1));
	m.insert(std::make_pair(Mac("001122-334456"), 2));
	m.insert(std::make_pair(Mac("001122-334457"), 3));
	EXPECT_EQ(m.size(), 3);
	MacMap::iterator it = m.begin();
	EXPECT_EQ(it->second, 1); it++;
	EXPECT_EQ(it->second, 2); it++;
	EXPECT_EQ(it->second, 3);
}

#include <unordered_map>
TEST(Mac, unordered_mapTest) {
	typedef std::unordered_map<Mac, int> MacUnorderedMap;
	MacUnorderedMap m;
	m.insert(std::make_pair(Mac("001122-334455"), 1));
	m.insert(std::make_pair(Mac("001122-334456"), 2));
	m.insert(std::make_pair(Mac("001122-334457"), 3));
	//EXPECT_EQ(m.size(), 3);
}

#endif // GTEST


namespace std {
	template<>
	struct hash<Mac> {
		size_t operator() (const Mac& r) const {
			return hash<Mac>()(r);
		}
	};
};