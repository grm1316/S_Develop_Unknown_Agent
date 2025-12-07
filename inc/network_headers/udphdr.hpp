#ifndef UDPHDR_HPP
#define UDPHDR_HPP

#include <cstdint>
#include <uchar.h>

#include "iphdr.hpp"
#include "util.hpp"

struct UdpHdr {
    //8bytes
    static constexpr int UDP_HEADER_LEN = 8;

    uint16_t sPort_{};
    uint16_t dPort_{};
    uint16_t totallenghth_{};
    uint16_t checksum_{};

    struct PseudoHdr {
        uint32_t sip_{};
        uint32_t dip_{};
        uint8_t zero_{};
        uint8_t protocol_{};
        uint16_t length_{};
    };

    static void GetChecksum(PIpHdr ipHeader, UdpHdr* udpHeader) {
        uint32_t ret{};

        PseudoHdr pseudoHeader{};

        pseudoHeader.sip_ = ipHeader->sip_;
        pseudoHeader.dip_ = ipHeader->dip_;
        pseudoHeader.zero_ = 0;
        pseudoHeader.protocol_ = ipHeader->protocolId_;
        pseudoHeader.length_ = udpHeader->totallenghth_;

        uint8_t* dataPtr{};

        udpHeader->checksum_ = 0;

        //pseudo header sum
        dataPtr = reinterpret_cast<uint8_t*>(&pseudoHeader);

        for(int i=0; i+1<sizeof(PseudoHdr); i+=2)
              ret += MAKEWORD(dataPtr[i], dataPtr[i+1]);


        //udp header sum
        dataPtr = reinterpret_cast<uint8_t*>(udpHeader);

        for(int i=0; i+1<UDP_HEADER_LEN; i+=2)
              ret += MAKEWORD(dataPtr[i], dataPtr[i+1]);

        //udp data sum
        int dataSize = ntohs(udpHeader->totallenghth_) - UDP_HEADER_LEN;
        dataPtr = reinterpret_cast<uint8_t*>(udpHeader) + UDP_HEADER_LEN;

        for(int i=0; i+1<dataSize; i+=2)
              ret += MAKEWORD(dataPtr[i], dataPtr[i+1]);

        if(dataSize & 1) ret += dataPtr[dataSize-1] << 8;

        while(ret >> 16) ret = (ret & 0xffff) + (ret >> 16);

        udpHeader->checksum_ = htons(~ret);
    }
};

using PUdpHdr = UdpHdr*;

#endif // UDPHDR_HPP
