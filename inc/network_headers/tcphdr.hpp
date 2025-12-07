#pragma once

#include <cstdint>
#include <cstring>

#include "ip.h"
#include "iphdr.hpp"
#include "util.hpp"

#pragma pack(push, 1)
typedef struct TCP_HEADER {
    struct PseudoHdr {
        Ip sip_;
        Ip dip_;
        uint8_t reserved_;
        uint8_t protocol_;
        uint16_t len_;
    };

    enum Flag {
        Fin = 1,
        Syn = 1 << 1,
        Rst = 1 << 2,
        Psh = 1 << 3,
        Ack = 1 << 4,
        Urg = 1 << 5,
    };

    static void GetChecksum(const PIpHdr ipHeader, TCP_HEADER* tcpHeader) {
        uint16_t payloadLen = ipHeader->totalLen() - ipHeader->len() - tcpHeader->len();
        uint16_t len = tcpHeader->len() + payloadLen;

        tcpHeader->checksum_ = 0;

        TCP_HEADER::PseudoHdr pseudoHeader{};

        pseudoHeader.sip_ = ipHeader->sip_;
        pseudoHeader.dip_ = ipHeader->dip_;
        pseudoHeader.reserved_ = 0;
        pseudoHeader.protocol_ = ipHeader->protocolId_;
        pseudoHeader.len_ = htons(len);

        uint32_t acc = 0;


        uint8_t* pseudoHeaderPtr = reinterpret_cast<uint8_t*>(&pseudoHeader);

        for(int i = 0; i + 1 < sizeof(TCP_HEADER::PseudoHdr); i += 2)
            acc += MAKEWORD(pseudoHeaderPtr[i], pseudoHeaderPtr[i+1]);

        uint8_t* tcpHeaderPtr = reinterpret_cast<uint8_t*>(tcpHeader);

        for(int i = 0; i + 1 < len; i += 2)
            acc += MAKEWORD(tcpHeaderPtr[i], tcpHeaderPtr[i+1]);

        if(len & 1) acc += static_cast<uint16_t>(tcpHeaderPtr[len - 1] << 8);

        while(acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);

        tcpHeader->checksum_ = htons(~acc);
    }

	uint16_t sPort_;
	uint16_t dPort_;
    uint32_t seqNumber_;
    uint32_t ackNumber_;
	uint16_t headerLen_reserve_flags_;
	uint16_t windowSize_;
    uint16_t checksum_;
    uint16_t urgentPointer_;

    TCP_HEADER() {}
    TCP_HEADER(uint8_t* data) { memcpy(this, data, sizeof(TCP_HEADER)); }
    uint16_t sPort() { return ntohs(sPort_); }
    uint16_t dPort() { return ntohs(dPort_); }
    uint8_t len() { return ((ntohs(headerLen_reserve_flags_) & 0b1111000000000000) >> 12) * 4; }
    uint8_t flags() { return headerLen_reserve_flags_ & 0b0000000000111111; }
}TcpHdr;
typedef TcpHdr* PTcpHdr;
//typedef PseudoHdr* PPseudoHdr;
#pragma pack(pop)
