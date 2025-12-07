#ifndef PROTOCOLBUFFER_H
#define PROTOCOLBUFFER_H

#pragma pack(push, 1)

namespace SNSS {
struct Tab {
    struct ChromeString {
        uint32_t str_len{};
        std::vector<uint8_t> raw_data{};

        std::string DecodeUTF8() const {
            return std::string(raw_data.begin(), raw_data.begin() + str_len);
        }

        std::wstring DecodeUTF16LE() const {
            std::wstring result;
            for (size_t i = 0; i < str_len; i += 2) {
                result += wchar_t(raw_data[i] | (raw_data[i + 1] << 8));
            }
            return result;
        }
    };

    int32_t id{};
    int32_t index{};

    ChromeString url{};
    ChromeString title{}; // wide = true
    ChromeString content_state{};

    int32_t transition_type{};
    int32_t type_mask{};

    ChromeString referrer{};
    int32_t referrer_policy{};
};

struct Bounds {
    int32_t window_id{};
    int32_t x{};
    int32_t y{};
    int32_t w{};
    int32_t h{};
    int32_t show_state{};
};

struct Pair {
    int32_t id{};
    int32_t index{};
};

struct CommandHeader {
    enum SNSS_CommandID : uint8_t {
        SetTabWindow = 0,
        SetTabIndexInWindow = 2,
        TabNavigationPathPrunedFromBack = 5,
        UpdateTabNavigation = 6,
        SetSelectedNavigationIndex = 7,
        SetSelectedTabInIndex = 8,
        SetWindowType = 9,
        TabNavigationPathPrunedFromFront = 11,
        SetPinnedState = 12,
        SetExtensionAppID = 13,
        SetWindowBounds3 = 14,
        SetWindowAppName = 15,
        TabClosed = 16,
        WindowClosed = 17,
        SetTabUserAgentOverride = 18,
        CommandSessionStorageAssociated = 19,
    };

    uint16_t length{};
    uint8_t cmd_id{};
};

struct Command {
    CommandHeader header_{};
    std::vector<uint8_t> data_{};
};

struct SNSSHeader {
    char magic[4];
    int32_t version;
};

using SNSSHeaderPtr = SNSSHeader*;
using CommandHeaderPtr = CommandHeader*;
}
#pragma pack(pop)


#endif // PROTOCOLBUFFER_H
