#include "pch.h"
#include "NetworkManager.h"

using namespace std;

NetworkManager::NetworkManager(const Type type, const Ip& ip, const uint16_t port)
    : type_(type), ip_(ip), port_(port) {
    try {
        if (!Init()) throw runtime_error("Failed to create NetworkManager");
    }
    catch (const exception& e) {
        WarningMsg(e.what());
        return;
    }
}

NetworkManager::~NetworkManager() {
    Stop();

#if _WIN64
    WSACleanup();
#endif
    closesocket(ni_.socket_);
}

//logging
void NetworkManager::WarningMsg(const string& msg) {
    cerr << "[NetworkManager]" << msg << endl;
}

void NetworkManager::ErrorMsg(const string& msg) {
    cerr << "[NetworkManager]" << msg << endl;
    exit(1);
}

//control
bool NetworkManager::Init() {
    try {
#if _WIN64
        WSAStartup(MAKEWORD(2, 2), &wsaData_);
#endif

        if ((ni_.socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == INVALID_SOCKET)
            throw runtime_error("Failed to create socket.");

        ni_.sockAddr_.sin_family = AF_INET;
        ni_.sockAddr_.sin_port = htons(port_);
        ni_.sockAddr_.sin_addr.S_un.S_addr = ip_;

        switch (type_) {
        case SERVER: {
            if (::bind(ni_.socket_, reinterpret_cast<sockaddr*>(&ni_.sockAddr_), sizeof(ni_.sockAddr_)) != 0)
                throw runtime_error("Failed to bind socket");

            if (::listen(ni_.socket_, SOMAXCONN) == SOCKET_ERROR)
                throw runtime_error("Failed to listen socket.");

            break;
        }
        case CLIENT: {
            if (::connect(ni_.socket_, reinterpret_cast<sockaddr*>(&ni_.sockAddr_), sizeof(ni_.sockAddr_)) < 0)
                throw runtime_error("Failed to connect.");

            break;
        }
        }
    }
    catch (const exception& e) {
        WarningMsg(string("<Init> ").append(e.what()));
        return false;
    }

    return true;
}

void NetworkManager::Register() {
    try {
        switch (type_) {
        case SERVER: {
            NetworkInformation tmp{};

            int addrLen = sizeof(sockaddr_in);
            tmp.socket_ = accept(ni_.socket_, reinterpret_cast<sockaddr*>(&tmp.sockAddr_), &addrLen);
            if (tmp.socket_ == INVALID_SOCKET)
                throw runtime_error("Failed to accept socket");

            cni_.push_back(tmp);
            std::thread(&NetworkManager::ServerCommWorker, this, tmp.socket_).detach();
            break;
        }
        case CLIENT: {
            auto sync = DataSync();
            if (dataList_.empty()) break;

            auto data = dataList_.front();
            dataList_.pop_front();

            switch (ClientCommWork(data)) {
            case RESET:
                throw runtime_error("Server error.");
                break;
            case FINISH:
                WarningMsg("Server connection exit.");
                return;
                break;
            default:
                break;
            }
            break;
        }
        }
    }
    catch (const exception& e) {
        ErrorMsg(string("<WorkerFunc> ").append(e.what()));
    }
}

//comm
int NetworkManager::ClientCommWork(DataType<char>& data) {
    int ret{};

    try {
        switch ((ret = send(ni_.socket_, reinterpret_cast<char*>(&data.len_), sizeof(data.len_), 0))) {
        case RESET: {
            throw runtime_error("Failed to send data, connection reset by peer.");
            break;
        }
        case FINISH: {
            break;
        }
        default: {
            ret = send(ni_.socket_, data.buffer_.data(), data.buffer_.size(), 0);
            break;
        }
        }
    }
    catch (const exception& e) {
        WarningMsg(string("<ClientCommWork> ").append(e.what()));
    }

    return ret;
}

void NetworkManager::ServerCommWorker(SOCKET socket) {
    bool isRunning = true;

    DataType<char> dt{};

    try {
        while (isRunning) {
            switch (recv(socket, reinterpret_cast<char*>(&dt.len_), sizeof(dt.len_), 0)) {
            case RESET: {
                throw runtime_error("Failed to recv data, connection reset by peer.");
                break;
            }
            case FINISH: {
                isRunning = false;
                break;
            }
            default: {
                dt.buffer_.resize(dt.len_);
                int recved{};

                do {
                    recved += recv(socket, dt.buffer_.data(), dt.buffer_.size(), 0);
                } while (recved < dt.len_);

                auto sync = DataSync();
                dataList_.push_back(dt);
                break;
            }
            }
        }
    }
    catch (const exception& e) {
        WarningMsg(string("<LogCommFunc> ").append(e.what()));
    }

    closesocket(socket);
    return;
}

//data
int NetworkManager::DataSize() {
    auto sync = DataSync();
    return dataList_.size();
}

void NetworkManager::PushData(const char* artifact, const char* detail, const char* buffer, const uint32_t len) {
    DataType<char> tmp{};

    tmp.artifact_ = artifact;
    tmp.detail_ = detail;

    tmp.buffer_.resize(len);
    memcpy(tmp.buffer_.data(), buffer, len);

    tmp.len_ = len;

    auto sync = DataSync();
    dataList_.push_back(tmp);
}

NetworkManager::DataType<char> NetworkManager::PopData() {
    DataType<char> ret{};

    auto sync = DataSync();

    ret = dataList_.front();
    dataList_.pop_front();

    return ret;
}
