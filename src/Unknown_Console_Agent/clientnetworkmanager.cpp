// ClientNetworkManager.cpp - 프로덕션 레벨 구현
// 단순하고 안정적인 클라이언트 네트워크 통신

#include "ClientNetworkManager.h"
#include <QHostInfo>
#include <QNetworkInterface>
#include <QCoreApplication>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <QSysInfo>
#include <QProcess>
#include <QThread>
#include <QTcpSocket>
#include <iostream>
#include <iostream>
#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#endif

// =================================================================
// 생성자/소멸자
// =================================================================

ClientNetworkManager::ClientNetworkManager(QObject* parent)
    : QObject(parent)
    , socket_(nullptr)
    , serverIP_("")
    , serverPort_(8443)
    , status_(Disconnected)
    , expectedMessageSize_(0)
    , waitingForHeader_(true)
    , registrationComplete_(false)
    , ownerIdNeeded_(false)
    , reconnectTimer_(nullptr)
    , heartbeatTimer_(nullptr)
    , autoReconnectEnabled_(true)
    , heartbeatEnabled_(true)
    , reconnectAttempts_(0)
{
    logInfo("ClientNetworkManager 초기화 시작");

    // TCP 소켓 생성
    socket_ = new QTcpSocket(this);

    // 시그널 연결
    connect(socket_, &QTcpSocket::connected, this, &ClientNetworkManager::onConnected);
    connect(socket_, &QTcpSocket::disconnected, this, &ClientNetworkManager::onDisconnected);
    connect(socket_, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
            this, &ClientNetworkManager::onError);
    connect(socket_, &QTcpSocket::readyRead, this, &ClientNetworkManager::onDataReceived);

    // 타이머 생성
    reconnectTimer_ = new QTimer(this);
    reconnectTimer_->setSingleShot(true);
    connect(reconnectTimer_, &QTimer::timeout, this, &ClientNetworkManager::onReconnectTimer);

    heartbeatTimer_ = new QTimer(this);
    connect(heartbeatTimer_, &QTimer::timeout, this, &ClientNetworkManager::onHeartbeatTimer);

    // PC 정보 미리 수집
    currentPCInfo_ = collectPCInfo();
    logInfo(QString("PC 정보 수집 완료 - ID: %1, Name: %2").arg(currentPCInfo_.pcId, currentPCInfo_.pcName));
}

ClientNetworkManager::~ClientNetworkManager() {
    logInfo("ClientNetworkManager 소멸 시작");
    disconnectFromServer();
}

// =================================================================
// 연결 관리
// =================================================================

bool ClientNetworkManager::connectToServer(const QString& serverIP, uint16_t port) {
    if (status_ == Connected || status_ == Connecting) {
        logInfo("이미 연결되었거나 연결 중입니다");
        return status_ == Connected;
    }

    // 서버 IP 결정
    QString targetIP = serverIP;
    if (targetIP.isEmpty()) {
        targetIP = findServerIP();
        if (targetIP.isEmpty()) {
            targetIP = "13.124.25.47"; // 기본 서버 IP
        }
    }

    serverIP_ = targetIP;
    serverPort_ = port;

    logInfo(QString("서버 연결 시도: %1:%2").arg(serverIP_).arg(serverPort_));
    setConnectionStatus(Connecting);

    socket_->connectToHost(serverIP_, serverPort_);

    // 동기 연결 대기 (10초)
    if (socket_->waitForConnected(10000)) {
        return true;
    } else {
        QString error = socket_->errorString();
        logError(QString("연결 실패: %1").arg(error));
        setConnectionStatus(Error);
        emit errorOccurred(error);
        return false;
    }
}

void ClientNetworkManager::disconnectFromServer() {
    if (status_ == Disconnected) return;

    logInfo("서버 연결 해제 중...");

    stopReconnectProcess();
    if (heartbeatTimer_) heartbeatTimer_->stop();

    if (socket_ && socket_->state() != QAbstractSocket::UnconnectedState) {
        socket_->disconnectFromHost();
        if (socket_->state() != QAbstractSocket::UnconnectedState) {
            socket_->waitForDisconnected(3000);
        }
    }

    setConnectionStatus(Disconnected);
}

bool ClientNetworkManager::isConnected() const {
    return status_ == Connected || status_ == Ready;
}

ClientNetworkManager::ConnectionStatus ClientNetworkManager::getConnectionStatus() const {
    return status_;
}

QString ClientNetworkManager::getStatusText() const {
    switch (status_) {
    case Disconnected: return "연결 끊김";
    case Connecting: return "연결 중";
    case Connected: return "연결됨";
    case Registering: return "PC 등록 중";
    case WaitingOwnerID: return "Owner_ID 대기";
    case Ready: return "준비 완료";
    case Error: return "오류";
    default: return "알 수 없음";
    }
}

// =================================================================
// PC 등록 프로세스
// =================================================================

bool ClientNetworkManager::startRegistration() {
    if (!isConnected()) {
        logError("서버에 연결되지 않음 - 등록 불가");
        emit registrationFailed("서버에 연결되지 않음");
        return false;
    }

    if (registrationComplete_) {
        logInfo("이미 등록이 완료됨");
        return true;
    }

    logInfo("PC 등록 프로세스 시작");
    setConnectionStatus(Registering);
    emit registrationStarted();

    // PC 정보 메시지 생성 및 전송
    QByteArray payload = createPCInfoMessage();
    if (sendBinaryMessage(MessageType::PC_INFO, payload)) {
        logInfo("PC 정보 전송 완료 - 서버 응답 대기 중");
        return true;
    } else {
        logError("PC 정보 전송 실패");
        setConnectionStatus(Error);
        emit registrationFailed("PC 정보 전송 실패");
        return false;
    }
}

bool ClientNetworkManager::submitOwnerID(const QString& ownerID) {
    if (ownerID.isEmpty()) {
        logError("Owner_ID가 비어있음");
        emit registrationFailed("Owner_ID가 비어있음");
        return false;
    }

    if (!ownerIdNeeded_) {
        logWarning("Owner_ID가 요청되지 않았음");
        return false;
    }

    logInfo(QString("Owner_ID 제출: %1").arg(ownerID));

    QByteArray payload = createOwnerIdMessage(ownerID);
    if (sendBinaryMessage(MessageType::PC_INFO, payload)) {
        logInfo("Owner_ID 전송 완료 - 검증 대기 중");
        return true;
    } else {
        logError("Owner_ID 전송 실패");
        emit registrationFailed("Owner_ID 전송 실패");
        return false;
    }
}

bool ClientNetworkManager::isRegistrationComplete() const {
    return registrationComplete_;
}

QString ClientNetworkManager::getCurrentPCId() const {
    return currentPCInfo_.pcId;
}

// =================================================================
// 포렌식 데이터 전송
// =================================================================

void ClientNetworkManager::pushData(const QString& moduleType, const QString& fileName, const QByteArray& jsonData) {
    QMutexLocker locker(&queueMutex_);

    QueuedData data;
    data.moduleType = moduleType;
    data.fileName = fileName;
    data.data = jsonData;
    // taskId는 비워둠 (일반 데이터 전송)

    dataQueue_.append(data);

    logInfo(QString("데이터 큐에 추가: %1/%2 (%3 bytes, 큐 크기: %4)")
                .arg(moduleType, fileName).arg(jsonData.size()).arg(dataQueue_.size()));
}

bool ClientNetworkManager::sendQueuedData() {
    QMutexLocker locker(&queueMutex_);

    if (dataQueue_.isEmpty()) {
        logInfo("전송할 큐 데이터 없음");
        return true;
    }

    if (!isConnected()) {
        logError("서버에 연결되지 않음 - 데이터 전송 불가");
        return false;
    }

    int successCount = 0;
    int totalCount = dataQueue_.size();

    while (!dataQueue_.isEmpty()) {
        QueuedData data = dataQueue_.takeFirst();

        // 일반 데이터 패킷으로 전송
        if (sendBinaryMessage(MessageType::DATA_PACKET, data.data)) {
            successCount++;
            emit dataTransmitted(data.moduleType, data.data.size());
            logInfo(QString("데이터 전송 성공: %1/%2").arg(data.moduleType, data.fileName));
        } else {
            logError(QString("데이터 전송 실패: %1/%2").arg(data.moduleType, data.fileName));
        }
    }

    logInfo(QString("큐 데이터 전송 완료: %1/%2 성공").arg(successCount).arg(totalCount));
    return successCount > 0;
}

int ClientNetworkManager::getQueueSize() const {
    QMutexLocker locker(&queueMutex_);
    return dataQueue_.size();
}

bool ClientNetworkManager::sendForensicDataWithTaskId(const QString& taskId, const QString& moduleType,
                                                      const QString& fileName, const QByteArray& jsonData) {
    if (!isConnected()) {
        logError("서버에 연결되지 않음 - Task 데이터 전송 불가");
        return false;
    }

    if (taskId.isEmpty()) {
        logError("Task ID가 비어있음");
        return false;
    }

    try {
        // Task ID가 포함된 메타데이터 헤더 생성
        QJsonObject metadata;
        metadata["task_id"] = taskId;
        metadata["module_type"] = moduleType;
        metadata["file_name"] = fileName;
        metadata["collection_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        metadata["client_id"] = currentPCInfo_.pcId;
        metadata["data_size"] = jsonData.size();
        metadata["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

        QJsonDocument metadataDoc(metadata);
        QByteArray metadataBytes = metadataDoc.toJson(QJsonDocument::Compact);

        // 패킷 구성: [메타데이터 크기(4바이트)][메타데이터][원본 데이터]
        QByteArray packet;
        QDataStream stream(&packet, QIODevice::WriteOnly);
        stream.setByteOrder(QDataStream::LittleEndian);

        quint32 metadataSize = static_cast<quint32>(metadataBytes.size());
        stream << metadataSize;
        packet.append(metadataBytes);
        packet.append(jsonData);

        if (sendBinaryMessage(MessageType::DATA_PACKET, packet)) {
            logInfo(QString("Task 데이터 전송 성공: Task=%1, Module=%2, Size=%3")
                        .arg(taskId, moduleType).arg(jsonData.size()));
            emit dataTransmitted(moduleType, jsonData.size());
            return true;
        } else {
            logError(QString("Task 데이터 전송 실패: Task=%1").arg(taskId));
            return false;
        }

    } catch (const std::exception& e) {
        logError(QString("Task 데이터 전송 예외: %1").arg(e.what()));
        return false;
    }
}

// =================================================================
// 연결 설정
// =================================================================

void ClientNetworkManager::setAutoReconnect(bool enabled) {
    autoReconnectEnabled_ = enabled;
    logInfo(QString("자동 재연결: %1").arg(enabled ? "활성화" : "비활성화"));
}

void ClientNetworkManager::setHeartbeat(bool enabled) {
    heartbeatEnabled_ = enabled;
    if (enabled && isConnected()) {
        heartbeatTimer_->start(30000); // 30초 간격
    } else {
        heartbeatTimer_->stop();
    }
    logInfo(QString("하트비트: %1").arg(enabled ? "활성화" : "비활성화"));
}

// =================================================================
// 네트워크 이벤트 처리
// =================================================================

// 주기적 PC 정보 전송 (선택사항)
void ClientNetworkManager::onConnected() {
    logInfo("서버에 연결됨");
    setConnectionStatus(Connected);
    resetConnectionState();
    emit connected();

    // 하트비트 시작
    if (heartbeatEnabled_) {
        heartbeatTimer_->start(30000);
    }

    // 선택사항: 주기적 PC 정보 업데이트 타이머
    // QTimer* updateTimer = new QTimer(this);
    // connect(updateTimer, &QTimer::timeout, [this]() {
    //     resendPCInfo("periodic_update");
    // });
    // updateTimer->start(300000); // 5분마다

    // 자동으로 등록 프로세스 시작
    QTimer::singleShot(1000, this, &ClientNetworkManager::startRegistration);
}

void ClientNetworkManager::onDisconnected() {
    logInfo("서버 연결 끊김");
    setConnectionStatus(Disconnected);
    emit disconnected();

    heartbeatTimer_->stop();
    registrationComplete_ = false;
    ownerIdNeeded_ = false;

    // 자동 재연결 시도
    if (autoReconnectEnabled_) {
        startReconnectProcess();
    }
}

void ClientNetworkManager::onError(QAbstractSocket::SocketError error) {
    QString errorText = socket_->errorString();
    logError(QString("소켓 오류: %1 (%2)").arg(errorText).arg(static_cast<int>(error)));

    setConnectionStatus(Error);
    emit errorOccurred(errorText);

    // 자동 재연결 시도
    if (autoReconnectEnabled_) {
        startReconnectProcess();
    }
}

void ClientNetworkManager::onDataReceived() {
    QByteArray newData = socket_->readAll();
    receiveBuffer_.append(newData);

    logInfo(QString("데이터 수신: %1 bytes (버퍼: %2 bytes)").arg(newData.size()).arg(receiveBuffer_.size()));

    processIncomingData();
}

void ClientNetworkManager::onReconnectTimer() {
    reconnectAttempts_++;
    logInfo(QString("재연결 시도 %1/%2").arg(reconnectAttempts_).arg(MAX_RECONNECT_ATTEMPTS));

    if (reconnectAttempts_ > MAX_RECONNECT_ATTEMPTS) {
        logError("최대 재연결 시도 횟수 초과");
        stopReconnectProcess();
        return;
    }

    // 재연결 시도
    connectToServer(serverIP_, serverPort_);
}

void ClientNetworkManager::onHeartbeatTimer() {
    if (!isConnected()) {
        heartbeatTimer_->stop();
        return;
    }

    QByteArray payload = createHeartbeatMessage();
    if (sendBinaryMessage(MessageType::HEARTBEAT, payload)) {
        logInfo("하트비트 전송");
    } else {
        logWarning("하트비트 전송 실패");
    }
}

// =================================================================
// PC 정보 수집 (기존 ClientUtils에서 통합)
// =================================================================

ClientNetworkManager::PCInfo ClientNetworkManager::collectPCInfo() {
    logInfo("PC 정보 수집 시작");

    PCInfo info;
    info.pcId = generatePCId();
    info.pcName = getCurrentPCName();
    info.ip = getCurrentIPAddress();
    info.os = getCurrentOSVersion();

    logInfo(QString("PC 정보 수집 완료 - ID: %1, Name: %2, IP: %3, OS: %4")
                .arg(info.pcId, info.pcName, info.ip, info.os));

    return info;
}

QString ClientNetworkManager::generatePCId() {
    QStringList macs = getAllMACAddresses();
    QString primaryMac = getPrimaryMACAddress(macs);

    if (primaryMac.isEmpty()) {
        logWarning("MAC 주소를 찾을 수 없음 - 기본값 사용");
        return "MAC_UNKNOWN";
    }

    // MAC_XX-XX-XX-XX-XX-XX 형식으로 변환
    QString normalizedMac = primaryMac.toUpper().replace(":", "-");
    return QString("MAC_%1").arg(normalizedMac);
}

QString ClientNetworkManager::getCurrentPCName() {
#ifdef _WIN32
    char computerName[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(computerName);
    if (GetComputerNameA(computerName, &size)) {
        return QString::fromLocal8Bit(computerName);
    }
#endif
    return QHostInfo::localHostName();
}

QString ClientNetworkManager::getCurrentIPAddress() {
    // 사설 IP 우선 검색
    foreach (const QNetworkInterface &netInterface, QNetworkInterface::allInterfaces()) {
        if (netInterface.flags().testFlag(QNetworkInterface::IsUp) &&
            netInterface.flags().testFlag(QNetworkInterface::IsRunning) &&
            !netInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) {

            foreach (const QNetworkAddressEntry &entry, netInterface.addressEntries()) {
                if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                    QString ip = entry.ip().toString();
                    // 사설 IP 대역 우선
                    if (ip.startsWith("192.168.") || ip.startsWith("10.") ||
                        (ip.startsWith("172.") && ip.split('.')[1].toInt() >= 16 && ip.split('.')[1].toInt() <= 31)) {
                        return ip;
                    }
                }
            }
        }
    }

    return "127.0.0.1"; // 기본값
}

QString ClientNetworkManager::getCurrentOSVersion() {
    return QSysInfo::prettyProductName();
}

QStringList ClientNetworkManager::getAllMACAddresses() {
    QStringList macs;

#ifdef _WIN32
    ULONG bufferLength = 0;
    GetAdaptersInfo(nullptr, &bufferLength);

    if (bufferLength > 0) {
        PIP_ADAPTER_INFO pAdapterInfo = (PIP_ADAPTER_INFO)malloc(bufferLength);
        if (GetAdaptersInfo(pAdapterInfo, &bufferLength) == NO_ERROR) {
            PIP_ADAPTER_INFO pAdapter = pAdapterInfo;
            while (pAdapter) {
                if (pAdapter->AddressLength == 6) {
                    QString mac = QString("%1:%2:%3:%4:%5:%6")
                    .arg(pAdapter->Address[0], 2, 16, QChar('0'))
                        .arg(pAdapter->Address[1], 2, 16, QChar('0'))
                        .arg(pAdapter->Address[2], 2, 16, QChar('0'))
                        .arg(pAdapter->Address[3], 2, 16, QChar('0'))
                        .arg(pAdapter->Address[4], 2, 16, QChar('0'))
                        .arg(pAdapter->Address[5], 2, 16, QChar('0'))
                        .toUpper();

                    if (mac != "00:00:00:00:00:00") {
                        macs.append(mac);
                    }
                }
                pAdapter = pAdapter->Next;
            }
        }
        free(pAdapterInfo);
    }
#endif

    // Qt 백업 방식
    if (macs.isEmpty()) {
        foreach (const QNetworkInterface &netInterface, QNetworkInterface::allInterfaces()) {
            QString mac = netInterface.hardwareAddress().toUpper();
            if (!mac.isEmpty() && mac != "00:00:00:00:00:00") {
                macs.append(mac);
            }
        }
    }

    return macs;
}

QString ClientNetworkManager::getPrimaryMACAddress(const QStringList& macs) {
    return macs.isEmpty() ? QString() : macs.first();
}

// =================================================================
// 서버 IP 검색 (기존 ip_helper.h에서 통합)
// =================================================================

QString ClientNetworkManager::findServerIP() {
    QStringList candidates = {
        "13.124.25.47",   // 실제 서버 IP
        "127.0.0.1"       // 로컬 테스트용
    };

    logInfo("서버 IP 검색 중...");

    foreach (const QString& ip, candidates) {
        if (testConnection(ip, serverPort_)) {
            logInfo(QString("서버 발견: %1:%2").arg(ip).arg(serverPort_));
            return ip;
        }
    }

    logWarning("연결 가능한 서버를 찾을 수 없음");
    return QString();
}

bool ClientNetworkManager::testConnection(const QString& ip, uint16_t port) {
    QTcpSocket testSocket;
    testSocket.connectToHost(ip, port);
    return testSocket.waitForConnected(3000); // 3초 대기
}

// =================================================================
// 메시지 처리
// =================================================================

bool ClientNetworkManager::sendBinaryMessage(MessageType type, const QByteArray& payload) {
    if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) {
        logError("소켓이 연결되지 않음");
        return false;
    }

    try {
        // 서버 프로토콜: [4바이트 크기][1바이트 타입][페이로드]
        QByteArray packet;

        uint32_t totalSize = 1 + payload.size();
        uint32_t networkSize = qToLittleEndian(totalSize);
        packet.append(reinterpret_cast<const char*>(&networkSize), sizeof(networkSize));

        uint8_t msgType = static_cast<uint8_t>(type);
        packet.append(reinterpret_cast<const char*>(&msgType), sizeof(msgType));

        packet.append(payload);

        qint64 written = socket_->write(packet);
        socket_->flush();

        if (written == packet.size()) {
            logInfo(QString("메시지 전송: 타입=0x%1, 크기=%2 bytes").arg(static_cast<int>(type), 2, 16, QChar('0')).arg(written));
            return true;
        } else {
            logError(QString("메시지 전송 실패: %1/%2 bytes").arg(written).arg(packet.size()));
            return false;
        }

    } catch (const std::exception& e) {
        logError(QString("메시지 전송 예외: %1").arg(e.what()));
        return false;
    }
}

void ClientNetworkManager::processIncomingData() {
    while (true) {
        if (waitingForHeader_) {
            if (receiveBuffer_.size() < 5) break; // 4바이트 크기 + 1바이트 타입

            uint32_t messageSize;
            memcpy(&messageSize, receiveBuffer_.constData(), sizeof(messageSize));
            expectedMessageSize_ = qFromLittleEndian(messageSize);

            if (expectedMessageSize_ > 10 * 1024 * 1024) { // 10MB 제한
                logError(QString("메시지 크기가 너무 큼: %1 bytes").arg(expectedMessageSize_));
                receiveBuffer_.clear();
                waitingForHeader_ = true;
                break;
            }

            waitingForHeader_ = false;
        }

        if (!waitingForHeader_) {
            uint32_t totalSize = 4 + expectedMessageSize_;
            if (receiveBuffer_.size() < totalSize) break;

            uint8_t messageType = static_cast<uint8_t>(receiveBuffer_.at(4));
            QByteArray payload = receiveBuffer_.mid(5, expectedMessageSize_ - 1);

            logInfo(QString("메시지 수신: 타입=0x%1, 페이로드=%2 bytes")
                        .arg(messageType, 2, 16, QChar('0')).arg(payload.size()));

            handleMessage(static_cast<MessageType>(messageType), payload);

            receiveBuffer_.remove(0, totalSize);
            waitingForHeader_ = true;
        }
    }
}

void ClientNetworkManager::handleMessage(MessageType type, const QByteArray& payload) {
    switch (type) {
    case MessageType::PC_INFO:
        handlePCInfoResponse(payload);
        break;
    case MessageType::TASK_REQUEST:
        handleTaskRequest(payload);
        break;
    case MessageType::HEARTBEAT:
        handleHeartbeatResponse(payload);
        break;
    default:
        logWarning(QString("알 수 없는 메시지 타입: 0x%1").arg(static_cast<int>(type), 2, 16, QChar('0')));
        break;
    }
}

void ClientNetworkManager::handlePCInfoResponse(const QByteArray& payload) {
    try {
        QJsonDocument doc = QJsonDocument::fromJson(payload);
        QJsonObject response = doc.object();

        bool success = response.value("success").toBool(false);
        bool needsOwnerID = response.value("needs_owner_id").toBool(false);
        QString message = response.value("message").toString();

        logInfo(QString("PC 등록 응답: success=%1, needs_owner_id=%2, message=%3")
                    .arg(success).arg(needsOwnerID).arg(message));

        if (success && !needsOwnerID) {
            // 등록 완료
            registrationComplete_ = true;
            setConnectionStatus(Ready);
            emit registrationCompleted(currentPCInfo_.pcId);
            logInfo("PC 등록 완료");
        } else if (needsOwnerID) {
            // Owner_ID 필요
            ownerIdNeeded_ = true;
            setConnectionStatus(WaitingOwnerID);
            emit ownerIdRequired();
            logInfo("Owner_ID 입력 필요");
        } else {
            // 등록 실패
            setConnectionStatus(Error);
            emit registrationFailed(message);
            logError(QString("PC 등록 실패: %1").arg(message));
        }

    } catch (const std::exception& e) {
        logError(QString("PC 등록 응답 처리 오류: %1").arg(e.what()));
        emit registrationFailed("응답 처리 오류");
    }
}

void ClientNetworkManager::handleTaskRequest(const QByteArray& payload) {
    try {
        QJsonParseError error;
        QJsonDocument doc = QJsonDocument::fromJson(payload, &error);

        if (error.error != QJsonParseError::NoError) {
            logError(QString("Task JSON 파싱 오류: %1").arg(error.errorString()));
            return;
        }

        QJsonObject taskJson = doc.object();

        TaskRequest task;
        // 🎯 서버와 일치하는 필드명으로 수정
        task.taskId = taskJson.value("task_id").toString();     // ✅ taskId → task_id
        task.taskType = taskJson.value("task_type").toString(); // ✅ taskType → task_type
        task.params = taskJson.value("parameters").toObject(); // ✅ parameters 유지

        logInfo(QString("Task 요청 수신: ID=%1, Type=%2").arg(task.taskId, task.taskType));

        // 🔍 디버깅용 로그 추가
        qDebug() << QString("[ClientNetworkManager] 파싱된 Task 정보:");
        qDebug() << QString("  - Task ID: %1").arg(task.taskId);
        qDebug() << QString("  - Task Type: %1").arg(task.taskType);
        qDebug() << QString("  - Parameters: %1").arg(QJsonDocument(task.params).toJson(QJsonDocument::Compact));

        if (task.isValid()) {
            emit taskReceived(task);
            logInfo(QString("유효한 Task 요청 처리 시작: %1").arg(task.taskId));
        } else {
            logError(QString("잘못된 Task 요청: ID=%1, Type=%2").arg(task.taskId, task.taskType));
        }

    } catch (const std::exception& e) {
        logError(QString("Task 요청 처리 오류: %1").arg(e.what()));
    }
}

void ClientNetworkManager::handleHeartbeatResponse(const QByteArray& payload) {
    Q_UNUSED(payload);
    logInfo("하트비트 응답 수신");
}

// =================================================================
// 메시지 생성
// =================================================================

QByteArray ClientNetworkManager::createPCInfoMessage() {
    QJsonObject json;
    json["pc_id"] = currentPCInfo_.pcId;
    json["pc_name"] = currentPCInfo_.pcName;
    json["ip"] = currentPCInfo_.ip;
    json["os"] = currentPCInfo_.os;
    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(json);
    return doc.toJson(QJsonDocument::Compact);
}

// Owner ID 제출 시 전체 PC 정보 포함
QByteArray ClientNetworkManager::createOwnerIdMessage(const QString& ownerID) {
    // 최신 PC 정보 수집
    currentPCInfo_ = collectPCInfo();

    QJsonObject json;
    json["pc_id"] = currentPCInfo_.pcId;
    json["pc_name"] = currentPCInfo_.pcName;
    json["ip"] = currentPCInfo_.ip;
    json["os"] = currentPCInfo_.os;
    json["owner_id"] = ownerID;
    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(json);
    return doc.toJson(QJsonDocument::Compact);
}

// PC 정보 재전송 메서드 추가
bool ClientNetworkManager::resendPCInfo(const QString& reason) {
    if (!isConnected()) {
        logWarning("서버에 연결되지 않음 - PC 정보 전송 불가");
        return false;
    }

    // 최신 PC 정보 수집
    currentPCInfo_ = collectPCInfo();

    QJsonObject json;
    json["pc_id"] = currentPCInfo_.pcId;
    json["pc_name"] = currentPCInfo_.pcName;
    json["ip"] = currentPCInfo_.ip;
    json["os"] = currentPCInfo_.os;
    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 재전송 이유 추가 (선택사항)
    if (!reason.isEmpty()) {
        json["update_reason"] = reason;
    }

    QJsonDocument doc(json);
    QByteArray payload = doc.toJson(QJsonDocument::Compact);

    if (sendBinaryMessage(MessageType::PC_INFO, payload)) {
        logInfo(QString("PC 정보 재전송 완료 (이유: %1)").arg(reason.isEmpty() ? "정기 업데이트" : reason));
        return true;
    } else {
        logError("PC 정보 재전송 실패");
        return false;
    }
}


QByteArray ClientNetworkManager::createTaskResponseMessage(const QString& taskId, bool success) {
    // 최신 PC 정보 수집
    currentPCInfo_ = collectPCInfo();

    QJsonObject json;
    json["task_id"] = taskId;
    json["success"] = success;
    json["pc_id"] = currentPCInfo_.pcId;

    // PC 정보도 함께 포함
    json["pc_name"] = currentPCInfo_.pcName;
    json["ip"] = currentPCInfo_.ip;
    json["os"] = currentPCInfo_.os;

    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(json);
    return doc.toJson(QJsonDocument::Compact);
}

QByteArray ClientNetworkManager::createHeartbeatMessage() {
    QJsonObject json;
    json["type"] = "heartbeat";
    json["pc_id"] = currentPCInfo_.pcId;
    json["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(json);
    return doc.toJson(QJsonDocument::Compact);
}

// =================================================================
// 상태 관리
// =================================================================

void ClientNetworkManager::setConnectionStatus(ConnectionStatus newStatus) {
    if (status_ != newStatus) {
        status_ = newStatus;
        emit connectionStatusChanged(status_);
        logInfo(QString("연결 상태 변경: %1").arg(getStatusText()));
    }
}

void ClientNetworkManager::startReconnectProcess() {
    if (reconnectTimer_->isActive()) return;

    logInfo(QString("재연결 프로세스 시작 (시도: %1/%2)").arg(reconnectAttempts_).arg(MAX_RECONNECT_ATTEMPTS));
    reconnectTimer_->start(5000); // 5초 후 재시도
}

void ClientNetworkManager::stopReconnectProcess() {
    if (reconnectTimer_->isActive()) {
        reconnectTimer_->stop();
        logInfo("재연결 프로세스 중단");
    }
}

void ClientNetworkManager::resetConnectionState() {
    reconnectAttempts_ = 0;
    stopReconnectProcess();
}

// =================================================================
// 유틸리티 함수
// =================================================================

void ClientNetworkManager::logInfo(const QString& message) {
    qInfo().noquote() << "[ClientNetworkManager]" << message;
}

void ClientNetworkManager::logWarning(const QString& message) {
    qWarning().noquote() << "[ClientNetworkManager]" << message;
}

void ClientNetworkManager::logError(const QString& message) {
    qCritical().noquote() << "[ClientNetworkManager]" << message;
}

bool tls_request(const std::string& host, int port) {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    const SSL_METHOD* method = TLS_client_method();
    SSL_CTX* ctx = SSL_CTX_new(method);

    if (!ctx) {
        std::cerr << "SSL_CTX 생성 실패\n";
        return false;
    }

    // 소켓 생성
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "소켓 생성 실패\n";
        SSL_CTX_free(ctx);
        return false;
    }

    // 서버 주소 설정
    sockaddr_in serv_addr{};
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        std::cerr << "서버 연결 실패\n";
        fclose(sock);
        SSL_CTX_free(ctx);
        return false;
    }

    // SSL 객체 생성
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);

    if (SSL_connect(ssl) <= 0) {
        std::cerr << "TLS 핸드셰이크 실패\n";
        SSL_free(ssl);
        fclose(sock);
        SSL_CTX_free(ctx);
        return false;
    }

    std::cout << "TLS 연결 성공!\n";

    // 데이터 송신
    const char* msg = "Hello via TLS!";
    SSL_write(ssl, msg, strlen(msg));

    // 데이터 수신
    char buffer[4096];
    int bytes = SSL_read(ssl, buffer, sizeof(buffer)-1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        std::cout << "서버 응답: " << buffer << "\n";
    }

    // 정리
    SSL_shutdown(ssl);
    SSL_free(ssl);
    fclose(sock);
    SSL_CTX_free(ctx);
    EVP_cleanup();

    return true;
}
