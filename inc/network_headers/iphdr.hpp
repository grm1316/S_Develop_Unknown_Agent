#pragma once

#ifdef __linux__
#include <arpa/inet.h>
#endif

#include <cstring>

#include "ip.h"
#include "util.hpp"

#pragma pack(push, 1)
typedef struct IP_HEADER {
    enum PROTOCOL_ID_TYPE {
        HOPOST = 0,
        ICMP,
        IGMP,
        GGP,
        IPv4,
        ST,
        TCP,
        UDP = 17,
    };

    enum IP_FLAGS_TYPE {
        RESORVED = 0,
        DF = (1<<14),
        MF = (1<<13)
    };

    static void GetChecksum(IP_HEADER* ipHeader) {
        uint8_t* data = reinterpret_cast<uint8_t*>(ipHeader);
        uint32_t len = ipHeader->len();

        ipHeader->headerChecksum_ = 0;

        uint32_t acc = 0;

        for(int i=0; i + 1< len; i+=2)
            acc += MAKEWORD(data[i], data[i+1]);


        if(len & 1) acc+= static_cast<uint16_t>(data[len-1] << 8);
        while(acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);

        ipHeader->headerChecksum_ = htons(~acc);
    }

    uint8_t version_headerLen_;
    uint8_t TOS_;
    uint16_t totalPacketLen_;
    uint16_t id_;

    uint16_t flags_fragOffset_;

    uint8_t ttl_;
    uint8_t protocolId_;
    uint16_t headerChecksum_;

    Ip sip_;
    Ip dip_;

    IP_HEADER() {}
    IP_HEADER(uint8_t* data) { memcpy(this, data, sizeof(IP_HEADER)); }

    uint8_t version() { return (version_headerLen_ & 0b11110000) >> 4; }
    uint16_t totalLen() { return ntohs(totalPacketLen_); }
    uint8_t len() { return (version_headerLen_ & 0b00001111) * 4; }
    uint8_t flags() { return (ntohs(flags_fragOffset_) & 0b1110000000000000) >> 13; }
    uint16_t fragOffset() { return ntohs(flags_fragOffset_) & 0b0001111111111111; }

    std::string sip() {
        char buf[INET_ADDRSTRLEN]{};

        inet_ntop(AF_INET, &sip_, buf, sizeof(buf));
        return std::string(buf);
    }

    std::string dip() {
        char buf[INET_ADDRSTRLEN]{};

        inet_ntop(AF_INET, &dip_, buf, sizeof(buf));
        return std::string(buf);
    }

}IpHdr;
//typedef IpHdr *PIpHdr;
using PIpHdr = IpHdr*;
#pragma pack(pop)
