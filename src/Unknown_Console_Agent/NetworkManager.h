#pragma once
#include "abstractworker.h"

class NetworkManager : public AbstractWorker {
public:
    enum Type {
        SERVER,
        CLIENT
    };

    void Register() override;

protected:
    enum status {
        STATUS_IDLE = 0,
        STATUS_RUNNING = 1,
        STATUS_STOPPED = 2,
        STATUS_ENDED = 3,
        STATUS_ERROR = 4,
        STATUS_MAX = 5
    };

    enum {
        RESET = -1,
        FINISH,
    };

private:
    struct NetworkInformation {
        SOCKET socket_{};
        sockaddr_in sockAddr_{};
    };

    template<typename T>
    struct DataType {
        std::string artifact_{};
        std::string detail_{};
        uint32_t len_{};
        std::vector<T> buffer_{};

        bool isEmpty() const {
            DataType<T> tmp{};

            if (memcmp(this, &tmp, sizeof(DataType)) == 0)
                return true;
            return false;
        }

    };


#if _WIN64
    WSADATA wsaData_ {};
#endif

    Ip ip_{};
    uint16_t port_{};
    NetworkInformation ni_{};
    std::vector<NetworkInformation> cni_{};

    std::list<DataType<char>> dataList_{};

    //control
    Type type_{};
    status status_{ STATUS_IDLE };

    //Logging
    void WarningMsg(const std::string& msg);
    void ErrorMsg(const std::string& msg);

    //control
    bool Init();



protected:
    //comm
    int ClientCommWork(DataType<char>& data);
    void ServerCommWorker(SOCKET socket);

public:
    static constexpr int BUFFER_SIZE = 1024;

    explicit NetworkManager(const Type type, const Ip& ip, const uint16_t port);
    virtual ~NetworkManager();

    //data
    int DataSize();
    void PushData(const char* artifact, const char* detail, const char* buffer, const uint32_t len);
    DataType<char> PopData();
};
