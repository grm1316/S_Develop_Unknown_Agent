#include "NetworkManager.h"
#include "databaseschema.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTimer>
#include <QMetaObject>

const QStringList NetworkManager::REQUIRED_MODULE_TYPES = {
    "USB_DATA",
    "BROWSER_DATA",
    "PREFETCH_DATA",
    "LNK_DATA",
    "DELETED_FILES",
    "MESSENGER_DATA"
};

NetworkManager::NetworkManager(const QString& address, uint16_t port, QObject* parent)
    : QObject(parent),
    serverAddress_(address),
    port_(port),
    serverSocket_(INVALID_SOCKET),
    isRunning_(false),
    databaseManager_(nullptr),
    backendApiClient_(nullptr),
    backendApiConfigured_(false),
    backendApiAvailable_(false) {

    // 🆕 메타타입 등록 (기존 호환성)
    qRegisterMetaType<ForensicData>("ForensicData");
    qRegisterMetaType<PCRegistrationInfo>("PCRegistrationInfo");

    // 소켓 주소 초기화
    memset(&serverAddr_, 0, sizeof(serverAddr_));

    qDebug() << "[NetworkManager] 새로운 NetworkManager 초기화됨"
             << QString("- 주소: %1, 포트: %2").arg(address).arg(port);

    // 초기 상태 설정
    updateLastActivity();
}

NetworkManager::~NetworkManager() {
    qDebug() << "[NetworkManager] 소멸자 호출 - 정리 시작";
    Stop();

    // 백엔드 API 클라이언트 정리
    if (backendApiClient_ && backendApiClient_->parent() != this) {
        // 외부에서 주입된 경우 정리하지 않음
        backendApiClient_ = nullptr;
    }

    qDebug() << "[NetworkManager] 정리 완료";
}

// =================================================================
// 🔧 의존성 주입 (DatabaseManager + BackendApiClient)
// =================================================================

void NetworkManager::setDatabaseManager(DatabaseManager* dbManager) {
    if (databaseManager_) {
        // 기존 연결 해제
        disconnect(databaseManager_, nullptr, this, nullptr);
    }

    databaseManager_ = dbManager;

    if (databaseManager_) {
        // 새로운 연결 설정
        connect(databaseManager_, &DatabaseManager::databaseConnected,
                this, &NetworkManager::onDatabaseConnected);
        connect(databaseManager_, &DatabaseManager::databaseDisconnected,
                this, &NetworkManager::onDatabaseDisconnected);
        connect(databaseManager_, &DatabaseManager::clientInfoChanged,
                this, &NetworkManager::onClientInfoChanged);
        connect(databaseManager_, &DatabaseManager::taskCompletionNotification,
                this, &NetworkManager::onTaskCompletionNotification);
        connect(databaseManager_, &DatabaseManager::forensicDataStored,
                this, &NetworkManager::onForensicDataStored);

        qDebug() << "[NetworkManager] DatabaseManager 연동 완료";
    }
}

void NetworkManager::setBackendApiClient(BackendApiClient* apiClient) {
    if (backendApiClient_) {
        // 기존 연결 해제
        disconnect(backendApiClient_, nullptr, this, nullptr);
    }

    backendApiClient_ = apiClient;

    if (backendApiClient_) {
        setupBackendApiConnections();
        backendApiConfigured_ = true;

        qDebug() << "[NetworkManager] BackendApiClient 연동 완료";
        emit backendApiConfigured(true);
    }
}

void NetworkManager::setupBackendApiConnections() {
    if (!backendApiClient_) {
        qWarning() << "[NetworkManager] BackendApiClient가 없음 - 시그널 연결 생략";
        return;
    }

    qDebug() << "[NetworkManager] BackendApiClient 시그널 연결 시작";

    // 🆕 1. Owner_ID 검증 시그널 연결 (새로운 슬롯)
    connect(backendApiClient_, &BackendApiClient::ownerVerificationResult,
            this, &NetworkManager::onOwnerVerificationResult);

    // 2. PC 정보 변경 알림 시그널 연결 - 기존 유지
    connect(backendApiClient_, &BackendApiClient::clientUpdateNotified,
            this, &NetworkManager::onClientUpdateNotified,
            Qt::QueuedConnection);

    // 3. Task 완료 알림 시그널 연결 - 기존 유지
    connect(backendApiClient_, &BackendApiClient::taskCompleteNotified,
            this, &NetworkManager::onTaskCompleteNotified,
            Qt::QueuedConnection);

    // 4. 백엔드 연결 테스트 시그널 연결 - 기존 유지
    connect(backendApiClient_, &BackendApiClient::connectionTestResult,
            this, &NetworkManager::onBackendConnectionTested,
            Qt::QueuedConnection);

    qDebug() << "[NetworkManager] BackendApiClient 시그널 연결 완료";
    updateBackendApiStatus(true);
}

// =================================================================
// 서버 제어 (기존 인터페이스 유지)
// =================================================================

bool NetworkManager::Init() {
    qDebug() << "[NetworkManager] 서버 초기화 시작...";

    // WSA 초기화 (Windows 소켓)
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        qCritical() << "[NetworkManager] WSAStartup 실패:" << result;
        return false;
    }

    bool success = initServerSocket();
    if (success) {
        qDebug() << "[NetworkManager] 서버 초기화 완료";
    } else {
        qCritical() << "[NetworkManager] 서버 초기화 실패";
        WSACleanup();
    }

    return success;
}

bool NetworkManager::initServerSocket() {
    qDebug() << "[NetworkManager] 소켓 초기화 중...";

    // 서버 소켓 생성
    serverSocket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket_ == INVALID_SOCKET) {
        qCritical() << "[NetworkManager] 서버 소켓 생성 실패:" << WSAGetLastError();
        return false;
    }

    // SO_REUSEADDR 설정
    int reuseAddr = 1;
    if (setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR,
                   (char*)&reuseAddr, sizeof(reuseAddr)) == SOCKET_ERROR) {
        qWarning() << "[NetworkManager] SO_REUSEADDR 설정 실패";
    }

    // 주소 설정
    serverAddr_.sin_family = AF_INET;
    serverAddr_.sin_port = htons(port_);

    if (serverAddress_ == "0.0.0.0") {
        serverAddr_.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, serverAddress_.toLocal8Bit().constData(), &serverAddr_.sin_addr);
    }

    // 바인드
    if (::bind(serverSocket_, (struct sockaddr*)&serverAddr_, sizeof(serverAddr_)) == SOCKET_ERROR) {
        qCritical() << QString("[NetworkManager] 바인드 실패 - %1:%2, 오류: %3")
                           .arg(serverAddress_).arg(port_).arg(WSAGetLastError());
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
        return false;
    }

    qDebug() << QString("[NetworkManager] 소켓 바인드 완료 - %1:%2")
                    .arg(serverAddress_).arg(port_);
    return true;
}

bool NetworkManager::Start() {
    qDebug() << "[NetworkManager] 서버 시작 중...";

    if (isRunning_) {
        qWarning() << "[NetworkManager] 서버가 이미 실행 중입니다";
        return true;
    }

    // 리슨 시작
    if (listen(serverSocket_, 10) == SOCKET_ERROR) {
        qCritical() << "[NetworkManager] 리슨 실패:" << WSAGetLastError();
        return false;
    }

    // 서버 시작
    isRunning_ = true;
    acceptThread_ = std::thread(&NetworkManager::acceptClients, this);

    qDebug() << QString("[NetworkManager] 서버 시작 완료 - 포트 %1에서 대기 중").arg(port_);
    updateLastActivity();

    return true;
}

void NetworkManager::Stop() {
    if (!isRunning_.exchange(false)) {
        return;
    }

    qDebug() << "[NetworkManager] 서버 중지 시작...";

    // 서버 소켓 닫기
    if (serverSocket_ != INVALID_SOCKET) {
        closesocket(serverSocket_);
        serverSocket_ = INVALID_SOCKET;
    }

    // Accept 스레드 대기
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }

    // 클라이언트 스레드들 대기
    {
        QMutexLocker locker(&clientThreadsMutex_);
        for (auto& th : clientThreads_) {
            if (th.joinable()) {
                th.join();
            }
        }
        clientThreads_.clear();
    }

    // 연결된 클라이언트 정리
    {
        QMutexLocker locker(&clientsMutex_);
        connectedClients_.clear();
        socketToClientId_.clear();
    }

    WSACleanup();

    qDebug() << "[NetworkManager] 서버 중지 완료";
}

// =================================================================
// 클라이언트 연결 처리 (메인 스레드)
// =================================================================

void NetworkManager::acceptClients() {
    qDebug() << "[NetworkManager] 클라이언트 연결 대기 시작";

    int connectionCount = 0;

    while (isRunning_) {
        sockaddr_in clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(serverSocket_, (struct sockaddr*)&clientAddr, &clientAddrLen);

        if (clientSocket == INVALID_SOCKET) {
            if (isRunning_) {
                int error = WSAGetLastError();
                if (error != WSAEINTR) {  // 정상적인 종료가 아닌 경우만 로그
                    qWarning() << "[NetworkManager] Accept 실패:" << error;
                }
            }
            continue;
        }

        connectionCount++;
        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        QString ipAddress(clientIP);

        qDebug() << QString("[NetworkManager] 클라이언트 #%1 연결됨 - %2")
                        .arg(connectionCount).arg(ipAddress);

        // 클라이언트 정보 생성
        ClientInfo client;
        client.socket = clientSocket;
        client.macAddress = ipAddress;
        client.firstConnect = QDateTime::currentDateTime();  // ✅ connectTime → firstConnect
        client.lastSeen = QDateTime::currentDateTime();

        // 클라이언트 처리 스레드 시작
        {
            QMutexLocker locker(&clientThreadsMutex_);
            clientThreads_.emplace_back(&NetworkManager::handleClient, this, clientSocket);
        }

        updateLastActivity();
    }

    qDebug() << "[NetworkManager] 클라이언트 연결 대기 종료";
}

void NetworkManager::handleClient(SOCKET clientSocket) {
    QString currentClientId;
    int messageCount = 0;

    // 클라이언트 정보 조회 (connectedClients_에서 최신 정보 가져오기)
    ClientInfo client;
    {
        QMutexLocker locker(&clientsMutex_);
        QString id = socketToClientId_.value(clientSocket);
        for (const ClientInfo& c : connectedClients_) {
            if (c.socket == clientSocket) {
                client = c;
                break;
            }
        }
    }

    qDebug() << QString("[NetworkManager] 클라이언트 핸들러 시작: %1").arg(client.macAddress);

    try {
        while (isRunning_) {
            // 메시지 길이 읽기 (4바이트)
            uint32_t messageLength;
            int bytesRead = recv(clientSocket, (char*)&messageLength, sizeof(messageLength), 0);

            if (bytesRead <= 0) {
                qDebug() << QString("[NetworkManager] 클라이언트 %1 연결 해제 (길이 읽기 실패)")
                                .arg(client.macAddress);
                break;
            }

            messageLength = qFromLittleEndian(messageLength);

            if (messageLength > 100 * 1024 * 1024) {  // 100MB 제한
                qWarning() << QString("[NetworkManager] 너무 큰 메시지: %1 bytes").arg(messageLength);
                break;
            }

            // 메시지 타입 읽기 (1바이트)
            uint8_t messageType;
            bytesRead = recv(clientSocket, (char*)&messageType, sizeof(messageType), 0);
            if (bytesRead <= 0) break;

            // 페이로드 길이 계산
            uint32_t payloadLength = messageLength - sizeof(messageType);

            // 페이로드 읽기
            QByteArray payload;
            if (payloadLength > 0) {
                payload.resize(payloadLength);
                uint32_t totalReceived = 0;

                while (totalReceived < payloadLength && isRunning_) {
                    int received = recv(clientSocket, payload.data() + totalReceived,
                                        payloadLength - totalReceived, 0);
                    if (received <= 0) {
                        qWarning() << "[NetworkManager] 페이로드 읽기 실패";
                        break;
                    }
                    totalReceived += received;
                }

                if (totalReceived != payloadLength) {
                    qWarning() << "[NetworkManager] 불완전한 페이로드";
                    break;
                }
            }

            messageCount++;
            // client.lastSeen = QDateTime::currentDateTime(); // client 객체는 이제 로컬 복사본이므로 직접 업데이트하지 않음
            updateLastActivity();

            // 메시지 타입별 처리
            BinaryMessageType msgType = static_cast<BinaryMessageType>(messageType);

            switch (msgType) {
            case BinaryMessageType::PC_INFO: {
                qDebug() << QString("[NetworkManager] PC_INFO 메시지 수신 - %1").arg(client.macAddress);
                QString pcIdFromMessage = handlePCInfoMessage(clientSocket, payload);
                if (!pcIdFromMessage.isEmpty()) {
                    // handlePCInfoMessage에서 clientInfo가 업데이트되었으므로, 여기서 client 객체를 다시 로드
                    QMutexLocker locker(&clientsMutex_);
                    for (const ClientInfo& c : connectedClients_) {
                        if (c.socket == clientSocket) {
                            client = c;
                            break;
                        }
                    }
                    currentClientId = client.pcId; // 현재 처리 중인 클라이언트 ID 업데이트
                }
                break;
            }

            // NetworkManager.cpp의 handleClient() 함수에서 DATA_PACKET 케이스를 다음과 같이 수정:

            case BinaryMessageType::DATA_PACKET: {
                // 🔍 디버그 로그 추가
                qDebug() << QString("[NetworkManager] DATA_PACKET 수신 시도 - Socket: %1, Client PC ID: '%2'")
                                .arg(clientSocket).arg(client.pcId);

                if (client.pcId.isEmpty()) {
                    qWarning() << QString("[NetworkManager] PC 등록 없이 데이터 수신 - Socket: %1, MAC: %2")
                                      .arg(clientSocket).arg(client.macAddress);

                    // 🔧 연결된 클라이언트 목록에서 PC ID 찾기 시도
                    QString foundPcId;
                    {
                        QMutexLocker locker(&clientsMutex_);
                        for (const ClientInfo& c : connectedClients_) {
                            if (c.socket == clientSocket && !c.pcId.isEmpty()) {
                                foundPcId = c.pcId;
                                client = c; // 클라이언트 정보 업데이트
                                break;
                            }
                        }
                    }

                    if (foundPcId.isEmpty()) {
                        qWarning() << QString("[NetworkManager] 클라이언트 PC ID를 찾을 수 없음 - Socket: %1, 데이터 무시")
                                          .arg(clientSocket);
                        break;
                    } else {
                        qDebug() << QString("[NetworkManager] PC ID 복구 성공: %1").arg(foundPcId);
                    }
                }

                qDebug() << QString("[NetworkManager] 포렌식 데이터 수신 - PC: %1, 크기: %2 bytes")
                                .arg(client.pcId).arg(payload.size());

                try {
                    processForensicData(client, payload);
                    qDebug() << QString("[NetworkManager] 포렌식 데이터 처리 완료 - PC: %1")
                                    .arg(client.pcId);
                } catch (const std::exception& e) {
                    qCritical() << QString("[NetworkManager] 포렌식 데이터 처리 중 예외 발생 - PC: %1, 오류: %2")
                                       .arg(client.pcId).arg(e.what());
                } catch (...) {
                    qCritical() << QString("[NetworkManager] 포렌식 데이터 처리 중 알 수 없는 예외 발생 - PC: %1")
                                       .arg(client.pcId);
                }

                break;
            }

            case BinaryMessageType::TASK_RESPONSE: {
                if (client.pcId.isEmpty()) { // client.pcId 사용
                    qWarning() << "[NetworkManager] PC 등록 없이 Task 응답 수신";
                    break;
                }
                qDebug() << QString("[NetworkManager] Task 응답 수신 - %1").arg(client.pcId); // client.pcId 사용

                // Task 완료 처리
                QString taskId = getClientTaskId(client.pcId); // client.pcId 사용
                if (!taskId.isEmpty()) {
                    emit taskCompleted(client.pcId, taskId); // client.pcId 사용
                    clearClientTaskId(client.pcId); // client.pcId 사용

                    // 🎯 Task 완료 알림 시작
                    QJsonDocument doc = QJsonDocument::fromJson(payload);
                    if (doc.isObject()) {
                        QJsonObject responseObj = doc.object();
                        QString moduleType = responseObj.value("moduleType").toString();
                        if (moduleType.isEmpty()) {
                            moduleType = detectModuleType(responseObj);
                        }

                        startTaskCompletion(taskId, client.pcId, moduleType, responseObj); // client.pcId 사용
                    }
                }
                break;
            }

            case BinaryMessageType::HEARTBEAT: {
                // Heartbeat 응답
                sendBinaryMessageToSocket(clientSocket, BinaryMessageType::HEARTBEAT, QByteArray());
                break;
            }

            default:
                qWarning() << QString("[NetworkManager] 알 수 없는 메시지 타입: %1").arg(messageType);
                break;
            }
        }

    } catch (const std::exception& e) {
        qCritical() << QString("[NetworkManager] 클라이언트 처리 중 예외: %1").arg(e.what());
    }

    // 클라이언트 정리
    cleanupClient(clientSocket);

    if (!currentClientId.isEmpty()) {
        emit clientDisconnected(currentClientId);
    }

    qDebug() << QString("[NetworkManager] 클라이언트 핸들러 종료: %1 (메시지 %2개 처리)")
                    .arg(client.macAddress).arg(messageCount);
}

// =================================================================
// 🎯 1. PC 등록 프로세스 (핵심 기능 #1)
// =================================================================

NetworkManager::PCRegistrationStatus NetworkManager::startPCRegistration(
    const QString& clientId, const PCRegistrationInfo& pcInfo) {

    QMutexLocker locker(&registrationMutex_);

    qDebug() << QString("[NetworkManager] PC 등록 프로세스 시작: %1").arg(clientId);

    PCRegistrationStatus status;
    status.pcId = clientId;
    status.currentStatus = "processing";

    if (!databaseManager_) {
        status.currentStatus = "failed";
        status.errorMessage = "DatabaseManager가 설정되지 않음";
        qCritical() << "[NetworkManager] DatabaseManager가 없어 PC 등록 실패";
        return status;
    }

    // 🔍 기존 PC 확인
    DatabaseManager::ClientInfo existingClient = databaseManager_->getClientInfo(clientId);

    if (!existingClient.pcId.isEmpty()) {
        // 기존 PC - 정보 업데이트만
        status.isExistingPC = true;
        status.ownerIdVerified = true;  // 기존 PC는 이미 검증됨
        status.currentStatus = "updating";

        qDebug() << QString("[NetworkManager] 기존 PC 발견: %1").arg(clientId);

        // 🔄 PC 정보 변경 감지
        PCChangeDetectionResult changeResult = detectPCChanges(clientId, pcInfo);
        if (changeResult.hasChanges) {
            processPCChanges(changeResult);
        }

        // 마지막 연결 시간 업데이트
        databaseManager_->updateClientLastConnect(clientId);

        status.currentStatus = "completed";

    } else {
        // 🆕 신규 PC - Owner_ID 검증 필요
        status.isExistingPC = false;
        status.ownerIdVerified = false;
        status.currentStatus = "verifying";

        qDebug() << QString("[NetworkManager] 신규 PC 등록 시작: %1").arg(clientId);

        // DatabaseManager에 신규 PC 정보 저장
        DatabaseManager::ClientInfo newClient;
        newClient.pcId = clientId;
        newClient.pcName = pcInfo.pcName;
        newClient.ip = pcInfo.ip;
        newClient.os = pcInfo.os;
        newClient.firstConnect = QDateTime::currentDateTime();
        newClient.lastConnect = QDateTime::currentDateTime();

        bool stored = databaseManager_->registerOrUpdateClient(newClient);
        if (!stored) {
            status.currentStatus = "failed";
            status.errorMessage = "데이터베이스 저장 실패";
            qCritical() << "[NetworkManager] 신규 PC 저장 실패:" << clientId;
            return status;
        }

        // 🔐 Owner_ID 검증 요청
        if (backendApiClient_ && backendApiConfigured_) {
            requestOwnerIdVerification(clientId);
            status.currentStatus = "pending_verification";
        } else {
            // 백엔드가 없으면 기본 Owner로 처리
            qWarning() << "[NetworkManager] 백엔드 API가 없어 기본 Owner로 등록:" << clientId;
            finalizePCRegistration(clientId, true, "default");
            status.ownerIdVerified = true;
            status.currentStatus = "completed";
        }
    }

    // 상태 저장
    pcRegistrationStatuses_[clientId] = status;

    emit pcRegistrationStarted(clientId, status);

    return status;
}

void NetworkManager::requestOwnerIdVerification(const QString& pcId) {
    if (!backendApiClient_) {
        qWarning() << "[NetworkManager] BackendApiClient가 없어 Owner_ID 검증 불가:" << pcId;
        return;
    }

    // Owner_ID 검증 요청 생성
    BackendApi::VerifyOwnerRequest request;
    request.pcId = pcId;
    request.ownerId = ""; // 클라이언트가 입력할 예정
    request.requestTime = QDateTime::currentDateTime();

    qDebug() << QString("[NetworkManager] Owner_ID 검증 요청 전송: %1").arg(pcId);

    // 🔧 QMetaObject::invokeMethod로 변경 (Threading 안전)
    QMetaObject::invokeMethod(backendApiClient_, [this, request]() {
        qDebug() << "[NetworkManager] 🔧 QMetaObject를 통한 백엔드 호출 실행";
        if (backendApiClient_) {
            backendApiClient_->verifyOwner(request);
        }
    }, Qt::QueuedConnection);

    emit ownerIdVerificationNeeded(pcId, request);
    emit backendRequestSent("verify-owner", pcId);
}

void NetworkManager::finalizePCRegistration(const QString& pcId, bool success, const QString& ownerId) {
    QMutexLocker locker(&registrationMutex_);

    auto it = pcRegistrationStatuses_.find(pcId);
    if (it == pcRegistrationStatuses_.end()) {
        qWarning() << "[NetworkManager] 등록 상태를 찾을 수 없음:" << pcId;
        return;
    }

    PCRegistrationStatus& status = it.value();

    if (success) {
        status.ownerIdVerified = true;
        status.currentStatus = "completed";

        // Owner_ID 정보를 데이터베이스에 업데이트 (필요시)
        if (!ownerId.isEmpty() && ownerId != "default") {
            // owner_id 필드가 있다면 업데이트
            qDebug() << QString("[NetworkManager] Owner_ID 업데이트: %1 -> %2").arg(pcId).arg(ownerId);
        }

        qDebug() << QString("[NetworkManager] PC 등록 완료: %1").arg(pcId);

    } else {
        status.currentStatus = "failed";
        status.errorMessage = "Owner_ID 검증 실패";
        qCritical() << QString("[NetworkManager] PC 등록 실패: %1").arg(pcId);
    }

    emit pcRegistrationCompleted(pcId, success);
    emit ownerIdVerificationResult(pcId, success, ownerId);
}

bool NetworkManager::isPCRegistrationComplete(const QString& pcId) {
    QMutexLocker locker(&registrationMutex_);

    auto it = pcRegistrationStatuses_.find(pcId);
    if (it == pcRegistrationStatuses_.end()) {
        return false;
    }

    return it.value().currentStatus == "completed";
}

NetworkManager::PCRegistrationStatus NetworkManager::getPCRegistrationStatus(const QString& pcId) {
    QMutexLocker locker(&registrationMutex_);

    auto it = pcRegistrationStatuses_.find(pcId);
    if (it != pcRegistrationStatuses_.end()) {
        return it.value();
    }

    // 기본 상태 반환
    PCRegistrationStatus status;
    status.pcId = pcId;
    status.currentStatus = "unknown";
    return status;
}

void NetworkManager::completePCRegistration(const QString& pcId) {
    finalizePCRegistration(pcId, true, "default");
}

// =================================================================
// 🔄 2. PC 정보 변경 감지 (핵심 기능 #2)
// =================================================================

NetworkManager::PCChangeDetectionResult NetworkManager::detectPCChanges(
    const QString& pcId, const PCRegistrationInfo& currentInfo) {

    QMutexLocker locker(&changeDetectionMutex_);

    PCChangeDetectionResult result;
    result.pcId = pcId;
    result.hasChanges = false;

    if (!databaseManager_) {
        qWarning() << "[NetworkManager] DatabaseManager가 없어 변경 감지 불가";
        return result;
    }

    qDebug() << QString("[NetworkManager] PC 정보 변경 감지 시작: %1").arg(pcId);

    // 기존 PC 정보 조회
    DatabaseManager::ClientInfo existingInfo = databaseManager_->getClientInfo(pcId);
    if (existingInfo.pcId.isEmpty()) {
        qDebug() << QString("[NetworkManager] 기존 PC 정보 없음: %1").arg(pcId);
        return result;
    }

    // 🔍 변경 사항 감지 (DatabaseManager에 위임)
    DatabaseManager::ClientInfo newClientInfo;
    newClientInfo.pcId = pcId;
    newClientInfo.pcName = currentInfo.pcName;
    newClientInfo.ip = currentInfo.ip;
    newClientInfo.os = currentInfo.os;

    DatabaseManager::ClientChangeInfo changeInfo = databaseManager_->detectClientChanges(pcId, newClientInfo);

    if (changeInfo.hasChanges()) {
        result.hasChanges = true;
        result.changedFields = changeInfo.changedFields;
        result.changeDetails = changeInfo;

        // 백엔드 알림 요청 준비
        result.updateRequest = databaseManager_->prepareClientUpdateRequest(result.changeDetails);

        qDebug() << QString("[NetworkManager] PC 정보 변경 감지됨: %1, 변경 필드: %2")
                        .arg(pcId).arg(result.changedFields.join(", "));

        emit pcChangesDetected(pcId, result);
    }

    // 결과 저장
    pcChangeResults_[pcId] = result;

    return result;
}

void NetworkManager::processPCChanges(const PCChangeDetectionResult& changeResult) {
    if (!changeResult.hasChanges) {
        return;
    }

    qDebug() << QString("[NetworkManager] PC 변경 사항 처리 시작: %1").arg(changeResult.pcId);

    bool success = false;

    if (databaseManager_) {
        // 🔍 현재 DB에서 완전한 정보를 가져옴
        DatabaseManager::ClientInfo currentClientInfo = databaseManager_->getClientInfo(changeResult.pcId);

        if (!currentClientInfo.isValid()) {
            qWarning() << QString("[NetworkManager] DB에서 PC 정보를 찾을 수 없음: %1")
                              .arg(changeResult.pcId);
            return;
        }

        // 🔄 업데이트할 정보 준비 (전체 정보 + 변경사항 적용)
        DatabaseManager::ClientInfo updatedClientInfo = currentClientInfo; // 기존 정보 복사

        // 변경된 필드만 새 값으로 업데이트
        if (changeResult.changedFields.contains("pc_name") && !changeResult.changeDetails.newPcName.isEmpty()) {
            updatedClientInfo.pcName = changeResult.changeDetails.newPcName;
        }
        if (changeResult.changedFields.contains("ip") && !changeResult.changeDetails.newIp.isEmpty()) {
            updatedClientInfo.ip = changeResult.changeDetails.newIp;
        }
        if (changeResult.changedFields.contains("os") && !changeResult.changeDetails.newOs.isEmpty()) {
            updatedClientInfo.os = changeResult.changeDetails.newOs;
        }

        // 마지막 연결 시간 업데이트
        updatedClientInfo.lastConnect = QDateTime::currentDateTime();

        qDebug() << QString("[NetworkManager] DB 업데이트 실행: %1")
                        .arg(changeResult.pcId);
        qDebug() << QString("  업데이트할 정보: name=%1, ip=%2, os=%3")
                        .arg(updatedClientInfo.pcName)
                        .arg(updatedClientInfo.ip)
                        .arg(updatedClientInfo.os);

        // 📝 데이터베이스에 변경 사항 적용 (UPSERT 사용)
        success = databaseManager_->registerOrUpdateClient(updatedClientInfo);

        if (success) {
            qDebug() << QString("[NetworkManager] 데이터베이스 변경 적용 완료: %1")
                            .arg(changeResult.pcId);

            // 🔔 백엔드 알림 (prepareClientUpdateRequest가 이제 제대로 동작함)
            if (backendApiClient_ && backendApiConfigured_) {
                notifyBackendOfChanges(changeResult);
            }
        } else {
            qWarning() << QString("[NetworkManager] 데이터베이스 변경 적용 실패: %1")
                              .arg(changeResult.pcId);
        }
    }

    emit pcChangesProcessed(changeResult.pcId, success);
}

void NetworkManager::notifyBackendOfChanges(const PCChangeDetectionResult& changeResult) {
    if (!backendApiClient_) {
        qWarning() << "[NetworkManager] BackendApiClient가 없어 변경 알림 불가";
        return;
    }

    qDebug() << QString("[NetworkManager] 백엔드에 PC 변경 알림 전송: %1").arg(changeResult.pcId);

    // 백엔드 알림 전송
    backendApiClient_->notifyClientUpdate(changeResult.updateRequest);

    emit backendRequestSent("client-update", changeResult.pcId);
}

// =================================================================
// 📋 3. Task 처리 및 완료 알림 (핵심 기능 #3)
// =================================================================

NetworkManager::TaskCompletionStatus NetworkManager::startTaskCompletion(
    const QString& taskId, const QString& pcId, const QString& moduleType, const QJsonObject& forensicData) {

    QMutexLocker locker(&taskCompletionMutex_);

    qDebug() << QString("[NetworkManager] Task 완료 처리 시작: %1 (PC: %2, 모듈: %3)")
                    .arg(taskId).arg(pcId).arg(moduleType);

    TaskCompletionStatus status;
    status.taskId = taskId;
    status.pcId = pcId;
    status.moduleType = moduleType;
    status.currentStatus = "storing";

    if (!databaseManager_) {
        status.currentStatus = "failed";
        status.errorMessage = "DatabaseManager가 설정되지 않음";
        qCritical() << "[NetworkManager] DatabaseManager가 없어 Task 완료 처리 실패";
        return status;
    }

    // 상태 저장
    taskCompletionStatuses_[taskId] = status;

    emit taskCompletionStarted(taskId, status);

    // 🗄️ 포렌식 데이터 저장 시작
    storeForensicDataInDB(taskId, pcId, moduleType, forensicData);

    return status;
}

void NetworkManager::storeForensicDataInDB(const QString& taskId, const QString& pcId,
                                           const QString& moduleType, const QJsonObject& forensicData) {

    if (!databaseManager_) {
        finalizeTaskCompletion(taskId, false);
        return;
    }

    qDebug() << QString("[NetworkManager] 포렌식 데이터 DB 저장 시작: Task=%1, Module=%2").arg(taskId, moduleType);

    // ForensicInfo 구조체 생성
    DatabaseManager::ForensicInfo forensicInfo;
    forensicInfo.taskId = taskId;
    forensicInfo.pcId = pcId;
    forensicInfo.moduleType = moduleType;
    forensicInfo.collectionTime = QDateTime::currentDateTime();
    forensicInfo.jsonData = forensicData;

    // JSON 데이터 크기 계산
    QJsonDocument doc(forensicData);
    forensicInfo.fileSize = doc.toJson(QJsonDocument::Compact).size();

    // 데이터베이스에 저장
    bool stored = databaseManager_->storeForensicData(forensicInfo);

    if (stored) {
        qDebug() << QString("[NetworkManager] 포렌식 데이터 저장 완료: Task=%1, Module=%2").arg(taskId, moduleType);

        // recent_scan 시간 업데이트 (Task 완료 시간 기준)
        databaseManager_->updateClientRecentScan(pcId, QDateTime::currentDateTime());

        // Task 완료 상태 업데이트
        QMutexLocker locker(&taskCompletionMutex_);
        auto it = taskCompletionStatuses_.find(taskId);
        if (it != taskCompletionStatuses_.end()) {
            it.value().forensicDataStored = true;
            it.value().currentStatus = "stored";
        }

        emit forensicDataStoredInDB(taskId, 0); // forensicId는 0으로 임시 설정

        // 🎯 목표 1: 6개 모듈 완료 여부 확인 후 백엔드 알림 (디버깅 강화)
        qDebug() << QString("[DEBUG-GOAL1] Task %1: DB 저장 후 완료 상태 확인 시작").arg(taskId);

        QList<DatabaseManager::ForensicInfo> forensicList = databaseManager_->getForensicDataByTaskId(taskId);

        qDebug() << QString("[DEBUG-GOAL1] Task %1: DB에서 조회된 모듈 수 = %2").arg(taskId).arg(forensicList.size());

        // 각 모듈 타입 출력
        QStringList storedModules;
        for (const auto& forensicInfo : forensicList) {
            storedModules.append(forensicInfo.moduleType);
        }
        qDebug() << QString("[DEBUG-GOAL1] Task %1: 저장된 모듈들 = [%2]").arg(taskId, storedModules.join(", "));

        if (forensicList.size() >= 6) {
            qDebug() << QString("[DEBUG-GOAL1] Task %1: 6개 완료 → 백엔드 알림 호출").arg(taskId);
            notifyBackendOfTaskCompletion(taskId);
        } else {
            qDebug() << QString("[DEBUG-GOAL1] Task %1: %2/6 미완료 → 백엔드 알림 건너뜀").arg(taskId).arg(forensicList.size());
        }

    } else {
        qCritical() << QString("[NetworkManager] 포렌식 데이터 저장 실패: Task=%1, Module=%2").arg(taskId, moduleType);
        finalizeTaskCompletion(taskId, false);
    }
}

void NetworkManager::notifyBackendOfTaskCompletion(const QString& taskId) {
    if (!backendApiClient_ || !backendApiConfigured_) {
        qWarning() << "[NetworkManager] 백엔드 API가 없어 완료 알림 건너뜀:" << taskId;
        finalizeTaskCompletion(taskId, true);  // 백엔드 없어도 성공으로 처리
        return;
    }

    QMutexLocker locker(&taskCompletionMutex_);
    auto it = taskCompletionStatuses_.find(taskId);
    if (it == taskCompletionStatuses_.end()) {
        qWarning() << "[NetworkManager] Task 완료 상태를 찾을 수 없음:" << taskId;
        return;
    }

    const TaskCompletionStatus& status = it.value();

    if (!databaseManager_) {
        finalizeTaskCompletion(taskId, false);
        return;
    }

    // 포렌식 정보 조회
    QList<DatabaseManager::ForensicInfo> forensicList = databaseManager_->getForensicDataByTaskId(taskId);
    if (forensicList.isEmpty()) {
        qWarning() << QString("[NetworkManager] Task에 대한 포렌식 데이터를 찾을 수 없음: %1").arg(taskId);
        finalizeTaskCompletion(taskId, false);
        return;
    }

    const DatabaseManager::ForensicInfo& forensicInfo = forensicList.first();

    // 백엔드 알림 요청 생성
    BackendApi::TaskCompleteRequest request = databaseManager_->prepareTaskCompleteRequest(
        status.pcId, taskId, status.moduleType, forensicInfo);

    qDebug() << QString("[NetworkManager] 백엔드에 Task 완료 알림 전송: %1").arg(taskId);

    // 백엔드 알림 전송
    backendApiClient_->notifyTaskComplete(request);

    emit backendRequestSent("task-complete", taskId);
}

void NetworkManager::finalizeTaskCompletion(const QString& taskId, bool success) {
    QMutexLocker locker(&taskCompletionMutex_);

    auto it = taskCompletionStatuses_.find(taskId);
    if (it == taskCompletionStatuses_.end()) {
        qWarning() << "[NetworkManager] Task 완료 상태를 찾을 수 없음:" << taskId;
        return;
    }

    TaskCompletionStatus& status = it.value();

    if (success) {
        status.backendNotified = true;
        status.currentStatus = "completed";
        qDebug() << QString("[NetworkManager] Task 완료 처리 성공: %1").arg(taskId);
    } else {
        status.currentStatus = "failed";
        qCritical() << QString("[NetworkManager] Task 완료 처리 실패: %1").arg(taskId);
    }

    emit taskCompletionFinished(taskId, success);
    emit taskCompletionNotifiedToBackend(taskId, success);
}

bool NetworkManager::isTaskCompletionInProgress(const QString& taskId) {
    QMutexLocker locker(&taskCompletionMutex_);

    auto it = taskCompletionStatuses_.find(taskId);
    if (it == taskCompletionStatuses_.end()) {
        return false;
    }

    const QString& status = it.value().currentStatus;
    return status == "storing" || status == "notifying";
}

NetworkManager::TaskCompletionStatus NetworkManager::getTaskCompletionStatus(const QString& taskId) {
    QMutexLocker locker(&taskCompletionMutex_);

    auto it = taskCompletionStatuses_.find(taskId);
    if (it != taskCompletionStatuses_.end()) {
        return it.value();
    }

    // 기본 상태 반환
    TaskCompletionStatus status;
    status.taskId = taskId;
    status.currentStatus = "unknown";
    return status;
}

// =================================================================
// 🔗 4. 백엔드 API 연동 (핵심 기능 #4)
// =================================================================

bool NetworkManager::isBackendApiConfigured() const {
    QMutexLocker locker(&backendApiMutex_);
    return backendApiConfigured_ && backendApiClient_ != nullptr;
}

bool NetworkManager::isBackendAvailable() const {
    if (backendApiClient_) {
        return backendApiClient_->isBackendAvailable();
    }
    return false;
}

void NetworkManager::configureBackendApi(const BackendApi::BackendConfig& config) {
    QMutexLocker locker(&backendApiMutex_);

    if (backendApiClient_) {
        backendApiClient_->setConfig(config);
        backendApiConfigured_ = backendApiClient_->isConfigValid();

        qDebug() << QString("[NetworkManager] 백엔드 API 설정 업데이트: %1")
                        .arg(backendApiConfigured_ ? "성공" : "실패");

        emit backendApiConfigured(backendApiConfigured_);
    }
}

void NetworkManager::testBackendConnection() {
    if (!backendApiClient_) {
        emit backendConnectionTested(false);
        return;
    }

    qDebug() << "[NetworkManager] 백엔드 연결 테스트 시작";
    backendApiClient_->testConnection();
}

void NetworkManager::updateBackendApiStatus(bool available) {
    QMutexLocker locker(&backendApiMutex_);

    if (backendApiAvailable_ != available) {
        backendApiAvailable_ = available;
        qDebug() << QString("[NetworkManager] 백엔드 API 상태 변경: %1")
                        .arg(available ? "사용 가능" : "사용 불가");
    }
}

// =================================================================
// 📊 상태 조회 및 진단
// =================================================================

NetworkManager::NetworkManagerStatus NetworkManager::getStatus() const {
    QMutexLocker statusLocker(&statusMutex_);
    QMutexLocker clientsLocker(&clientsMutex_);
    QMutexLocker registrationLocker(&registrationMutex_);
    QMutexLocker taskLocker(&taskCompletionMutex_);
    QMutexLocker backendLocker(&backendApiMutex_);

    NetworkManagerStatus status;
    status.isRunning = isRunning_;
    status.isDatabaseConnected = (databaseManager_ != nullptr && databaseManager_->isConnected());
    status.isBackendApiConfigured = backendApiConfigured_;
    status.connectedClientCount = connectedClients_.size();
    status.pendingRegistrations = pcRegistrationStatuses_.size();
    status.activeTaskCompletions = taskCompletionStatuses_.size();
    status.lastActivity = lastActivityTime_;

    return status;
}

void NetworkManager::printDiagnostics() const {
    NetworkManagerStatus status = getStatus();

    qDebug() << "=== NetworkManager 진단 정보 ===";
    qDebug() << QString("실행 중: %1").arg(status.isRunning ? "예" : "아니오");
    qDebug() << QString("데이터베이스 연결: %1").arg(status.isDatabaseConnected ? "연결됨" : "연결 안됨");
    qDebug() << QString("백엔드 API 설정: %1").arg(status.isBackendApiConfigured ? "설정됨" : "설정 안됨");
    qDebug() << QString("연결된 클라이언트: %1개").arg(status.connectedClientCount);
    qDebug() << QString("대기 중인 등록: %1개").arg(status.pendingRegistrations);
    qDebug() << QString("진행 중인 Task 완료: %1개").arg(status.activeTaskCompletions);
    qDebug() << QString("마지막 활동: %1").arg(status.lastActivity.toString());
    qDebug() << "================================";
}

// =================================================================
// 🔄 기존 호환 인터페이스 (ForensicServer, HttpApiHandler용)
// =================================================================
// =================================================================
// 🔧 수정 대상: sendTaskToClient 함수 (NetworkManager.cpp에서)
// =================================================================

bool NetworkManager::sendTaskToClient(const QString& clientId, const TaskRequest& task) {
    QMutexLocker locker(&clientsMutex_);

    qDebug() << QString("[NetworkManager] Task 전송 요청: ClientID=%1, TaskID=%2")
                    .arg(clientId, task.taskId);
    qDebug() << QString("[NetworkManager] 현재 연결된 클라이언트 수: %1")
                    .arg(connectedClients_.size());

    for (int i = 0; i < connectedClients_.size(); ++i) {
        const ClientInfo& client = connectedClients_[i];
        qDebug() << QString("[NetworkManager] 클라이언트[%1]: ID=%2, IP=%3, 소켓=%4")
                        .arg(i)
                        .arg(client.pcId)
                        .arg(client.macAddress)
                        .arg(QString::number(static_cast<qulonglong>(client.socket)));
    }

    SOCKET targetSocket = INVALID_SOCKET;
    QString foundClientInfo;

    for (const ClientInfo& client : connectedClients_) {
        if (client.pcId == clientId) {
            targetSocket = client.socket;
            foundClientInfo = QString("IP=%1, 소켓=%2")
                                  .arg(client.macAddress)
                                  .arg(QString::number(static_cast<qulonglong>(client.socket)));
            break;
        }
    }

    if (targetSocket == INVALID_SOCKET) {
        qWarning() << QString("[NetworkManager] 클라이언트를 찾을 수 없음: %1").arg(clientId);
        qWarning() << "[NetworkManager] 사용 가능한 클라이언트 ID 목록:";
        for (const ClientInfo& client : connectedClients_) {
            qWarning() << QString("  - %1 (IP: %2)").arg(client.pcId, client.macAddress);
        }
        return false;
    }

    qDebug() << QString("[NetworkManager] 클라이언트 발견: %1 (%2)")
                    .arg(clientId, foundClientInfo);

    // 🔍 디버깅 로그 추가 - 단계별 진행 상황 확인
    qDebug() << "[NetworkManager] Step 1: 클라이언트 발견 완료";

    try {
        // Task ID 설정
        qDebug() << "[NetworkManager] Step 2: setClientTaskId 호출 시작";
        setClientTaskId(clientId, task.taskId);
        qDebug() << "[NetworkManager] Step 2: setClientTaskId 호출 완료";

        // JSON 생성 시작
        qDebug() << "[NetworkManager] Step 3: Task JSON 생성 시작";
        QJsonObject taskJson;
        taskJson["task_id"] = task.taskId;
        taskJson["task_type"] = task.tasktype;
        taskJson["parameters"] = task.parameters;
        taskJson["timestamp"] = task.requestTime.toString(Qt::ISODate);
        qDebug() << "[NetworkManager] Step 3: Task JSON 객체 생성 완료";

        // JSON 문서 변환
        qDebug() << "[NetworkManager] Step 4: JSON 문서 변환 시작";
        QJsonDocument doc(taskJson);
        QByteArray taskData = doc.toJson(QJsonDocument::Compact);
        qDebug() << "[NetworkManager] Step 4: JSON 문서 변환 완료";

        qDebug() << QString("[NetworkManager] Task JSON 생성 완료: %1 bytes")
                        .arg(taskData.size());
        qDebug() << QString("[NetworkManager] Task 내용: %1")
                        .arg(QString::fromUtf8(taskData.left(200)));

        // 소켓 전송 시작
        qDebug() << "[NetworkManager] Step 5: sendBinaryMessageToSocket 호출 시작";
        qDebug() << QString("[NetworkManager] 대상 소켓: %1, 메시지 타입: TASK_REQUEST, 데이터 크기: %2")
                        .arg(QString::number(static_cast<qulonglong>(targetSocket)))
                        .arg(taskData.size());

        bool success = sendBinaryMessageToSocket(targetSocket, BinaryMessageType::TASK_REQUEST, taskData);

        qDebug() << "[NetworkManager] Step 5: sendBinaryMessageToSocket 호출 완료";
        qDebug() << QString("[NetworkManager] 전송 결과: %1").arg(success ? "성공" : "실패");

        if (success) {
            qDebug() << QString("[NetworkManager] Task %1 전송 성공 to %2")
                            .arg(task.taskId, clientId);
            updateLastActivity();
            return true;
        } else {
            qWarning() << QString("[NetworkManager] Task %1 전송 실패 to %2")
                              .arg(task.taskId, clientId);
            clearClientTaskId(clientId);
            return false;
        }

    } catch (const std::exception& e) {
        qCritical() << QString("[NetworkManager] sendTaskToClient 예외 발생: %1").arg(e.what());
        clearClientTaskId(clientId);
        return false;
    } catch (...) {
        qCritical() << "[NetworkManager] sendTaskToClient 알 수 없는 예외 발생";
        clearClientTaskId(clientId);
        return false;
    }

    setClientTaskId(clientId, task.taskId);

    QJsonObject taskJson;
    taskJson["task_id"] = task.taskId;
    taskJson["task_type"] = task.tasktype;
    taskJson["parameters"] = task.parameters;
    taskJson["timestamp"] = task.requestTime.toString(Qt::ISODate);

    QJsonDocument doc(taskJson);
    QByteArray taskData = doc.toJson(QJsonDocument::Compact);

    qDebug() << QString("[NetworkManager] Task JSON 생성 완료: %1 bytes")
                    .arg(taskData.size());
    qDebug() << QString("[NetworkManager] Task 내용: %1")
                    .arg(QString::fromUtf8(taskData.left(200)));

    bool success = sendBinaryMessageToSocket(targetSocket, BinaryMessageType::TASK_REQUEST, taskData);

    if (success) {
        qDebug() << QString("[NetworkManager] Task %1 전송 성공 to %2")
                        .arg(task.taskId, clientId);
        updateLastActivity();
    } else {
        qWarning() << QString("[NetworkManager] Task %1 전송 실패 to %2")
                          .arg(task.taskId, clientId);
        qWarning() << QString("[NetworkManager] 소켓 상태 확인 필요: %1")
                          .arg(QString::number(static_cast<qulonglong>(targetSocket)));
        clearClientTaskId(clientId);
    }

    return success;
}

QString NetworkManager::getClientIdByPcId(const QString& pcId) {
    // pcId와 clientId가 동일함 (MAC 기반)
    return pcId;
}

bool NetworkManager::isClientConnected(const QString& pcId) {
    QMutexLocker locker(&clientsMutex_);

    for (const ClientInfo& client : connectedClients_) {
        if (client.pcId == pcId) {
            return true;
        }
    }

    return false;
}

void NetworkManager::setClientTaskId(const QString& clientId, const QString& taskId) {
    // 🔧 데드락 수정: 중복된 clientsMutex_ 사용 제거
    QMutexLocker locker(&taskIdsMutex_);

    qDebug() << QString("[NetworkManager] Task ID 매핑 저장 시작: %1 → %2").arg(clientId, taskId);

    // 단순히 clientId로만 저장 (pcId와 clientId가 동일하므로 중복 저장 불필요)
    clientTaskIds_[clientId] = taskId;

    qDebug() << QString("[NetworkManager] Task ID 매핑 저장 완료: %1 → %2").arg(clientId, taskId);
}

QString NetworkManager::getClientTaskId(const QString& clientId) const {
    QMutexLocker locker(&taskIdsMutex_);
    return clientTaskIds_.value(clientId, QString());
}

void NetworkManager::clearClientTaskId(const QString& clientId) {
    QMutexLocker locker(&taskIdsMutex_);
    clientTaskIds_.remove(clientId);
}

// =================================================================
// 🔧 내부 처리 메서드들
// =================================================================

QString NetworkManager::handlePCInfoMessage(SOCKET clientSocket, const QByteArray& messageData) {
    qDebug() << "[NetworkManager] PC_INFO 메시지 처리";

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(messageData, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "[NetworkManager] PC_INFO JSON 파싱 실패:" << parseError.errorString();
        sendPCInfoResponse(clientSocket, false, false, "Invalid JSON format");
        return "";
    }

    QJsonObject jsonData = doc.object();
    qDebug() << QString("[NetworkManager] 수신된 PC 정보 JSON: %1").arg(QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));

    PCRegistrationInfo pcInfo = parsePCInfoJson(jsonData);
    QString pcId = pcInfo.pcId;

    if (pcId.isEmpty()) {
        qWarning() << "[NetworkManager] PC ID가 비어있음";
        sendPCInfoResponse(clientSocket, false, false, "PC ID is required");
        return "";
    }

    // 2단계 처리: owner_id가 포함된 경우
    if (jsonData.contains("owner_id")) {
        qDebug() << QString("[NetworkManager] 2단계 등록 처리 시작: %1").arg(pcId);
        QString ownerId = jsonData.value("owner_id").toString();

        if (ownerId.isEmpty()) {
            sendPCInfoResponse(clientSocket, false, true, "Owner ID cannot be empty.");
            return "";
        }

        // 🔧 디버그 로그 추가
        qDebug() << QString("[DEBUG] backendApiClient_ 상태: %1").arg(backendApiClient_ ? "있음" : "없음");
        qDebug() << QString("[DEBUG] backendApiConfigured_ 상태: %1").arg(backendApiConfigured_ ? "설정됨" : "설정 안됨");

        if (backendApiClient_) {
            qDebug() << "[DEBUG] 백엔드 요청 생성 중...";

            BackendApi::VerifyOwnerRequest verifyRequest;
            verifyRequest.pcId = pcId;
            verifyRequest.pcName = pcInfo.pcName;
            verifyRequest.ip = pcInfo.ip;
            verifyRequest.os = pcInfo.os;
            verifyRequest.macAddress = pcInfo.primaryMac;
            verifyRequest.ownerId = ownerId;
            verifyRequest.requestTime = QDateTime::currentDateTime();

            qDebug() << "[DEBUG] 백엔드 요청 데이터 준비 완료, QMetaObject::invokeMethod 실행 중...";

            // 🔧 QMetaObject::invokeMethod로 변경 (Threading 안전)
            QMetaObject::invokeMethod(backendApiClient_, [this, verifyRequest]() {
                qDebug() << "[NetworkManager] 🔧 2단계: QMetaObject를 통한 백엔드 호출 실행";
                if (backendApiClient_) {
                    qDebug() << "[DEBUG] backendApiClient_->verifyOwner() 호출 중...";
                    backendApiClient_->verifyOwner(verifyRequest);
                } else {
                    qWarning() << "[DEBUG] QMetaObject 내부에서 backendApiClient_가 null!";
                }
            }, Qt::QueuedConnection);

            qDebug() << "[DEBUG] QMetaObject::invokeMethod 호출 완료";
        } else {
            qWarning() << "[DEBUG] backendApiClient_가 null이므로 백엔드 요청을 보낼 수 없음";
        }

        return ""; // 비동기 응답을 기다리므로 여기서 ID를 반환하지 않음
    }

    // 1단계 처리: owner_id가 없는 최초 연결
    bool isExistingPC = false;
    if (databaseManager_) {
        isExistingPC = !databaseManager_->getClientInfo(pcId).pcId.isEmpty();
    }

    if (isExistingPC) {
        // ===== 기존 PC 처리 (🔄 변경 감지 로직 추가) =====
        qDebug() << QString("[NetworkManager] 기존 PC 연결: %1").arg(pcId);

        if (databaseManager_) {
            // 🔍 PC 정보 변경 감지
            PCChangeDetectionResult changeResult = detectPCChanges(pcId, pcInfo);

            if (changeResult.hasChanges) {
                qDebug() << QString("[NetworkManager] PC 정보 변경 감지됨: %1, 변경 필드: %2")
                                .arg(pcId).arg(changeResult.changedFields.join(", "));

                // 📝 변경사항 처리 (데이터베이스 업데이트 + 백엔드 알림)
                processPCChanges(changeResult);

                sendPCInfoResponse(clientSocket, true, false, "PC updated successfully");
            } else {
                qDebug() << QString("[NetworkManager] PC 정보 변경 없음: %1").arg(pcId);

                // 마지막 연결 시간만 업데이트
                databaseManager_->updateClientLastConnect(pcId);

                sendPCInfoResponse(clientSocket, true, false, "PC registered successfully");
            }
        } else {
            qWarning() << "[NetworkManager] DatabaseManager가 없어 변경 감지 불가";
            sendPCInfoResponse(clientSocket, true, false, "PC registered successfully");
        }

        // 연결된 클라이언트 목록 업데이트
        updateConnectedClientsList(clientSocket, pcId, pcInfo);
        emit clientConnected(pcId);

        return pcId;

    } else {
        // ===== 신규 PC 처리 (1단계) =====
        qDebug() << QString("[NetworkManager] 신규 PC 감지 (1단계): %1").arg(pcId);

        {
            QMutexLocker locker(&pendingMutex_);
            pendingRegistrations_[pcId] = clientSocket;
        }

        sendPCInfoResponse(clientSocket, false, true, "Owner ID verification required");

        return "";
    }
}

// =================================================================
// 🆕 PC_INFO 응답 전송 메서드 (새로 추가)
// =================================================================

bool NetworkManager::sendPCInfoResponse(SOCKET clientSocket, bool success, bool needsOwnerID, const QString& message) {
    QJsonObject response;
    response["success"] = success;
    response["needs_owner_id"] = needsOwnerID;  // 🆕 클라이언트 요구 필드
    response["message"] = message;
    response["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(response);
    QByteArray responseData = doc.toJson(QJsonDocument::Compact);

    qDebug() << QString("[NetworkManager] PC_INFO 응답 전송: success=%1, needs_owner_id=%2, message=%3")
                    .arg(success).arg(needsOwnerID).arg(message);

    return sendBinaryMessageToSocket(clientSocket, BinaryMessageType::PC_INFO, responseData);
}

// =================================================================
// 🆕 백엔드 Owner_ID 검증 결과 처리 슬롯 (새로 추가)
// =================================================================

void NetworkManager::onOwnerVerificationResult(const BackendApi::VerifyOwnerRequest& request,
                                               const BackendApi::VerifyOwnerResponse& response,
                                               bool success) {
    QString pcId = request.pcId;
    qDebug() << QString("[NetworkManager] Owner 검증 결과 수신: PC=%1, Success=%2")
                    .arg(pcId).arg(success);

    // 대기 중인 소켓 찾기
    SOCKET clientSocket = INVALID_SOCKET;
    {
        QMutexLocker locker(&pendingMutex_);
        if (pendingRegistrations_.contains(pcId)) {
            clientSocket = pendingRegistrations_.take(pcId);
        }
    }

    if (clientSocket == INVALID_SOCKET) {
        qWarning() << QString("[NetworkManager] 대기 중인 소켓을 찾을 수 없음: %1").arg(pcId);
        return;
    }

    if (success) {
        // 검증 성공 - DB에 PC 등록
        if (databaseManager_) {
            DatabaseManager::ClientInfo newClient;
            newClient.pcId = pcId;
            newClient.pcName = request.pcName;
            newClient.ip = request.ip;
            newClient.os = request.os;
            newClient.firstConnect = QDateTime::currentDateTime();
            newClient.lastConnect = QDateTime::currentDateTime();
            // owner_id는 DB에 저장하지 않음

            bool dbSuccess = databaseManager_->registerOrUpdateClient(newClient);

            if (dbSuccess) {
                qDebug() << QString("[NetworkManager] PC 등록 완료: %1").arg(pcId);

                // PCRegistrationInfo 재구성
                PCRegistrationInfo pcInfo;
                pcInfo.pcId = pcId;
                pcInfo.pcName = request.pcName;
                pcInfo.ip = request.ip;
                pcInfo.os = request.os;
                pcInfo.primaryMac = request.macAddress;
                pcInfo.timestamp = QDateTime::currentDateTime();

                updateConnectedClientsList(clientSocket, pcId, pcInfo);

                qDebug() << QString(">>>> [DEBUG] Attempting to send final success response to socket %1").arg(clientSocket);
                bool finalSendSuccess = sendPCInfoResponse(clientSocket, true, false, "PC registration completed");
                qDebug() << QString(">>>> [DEBUG] Final response send result: %1").arg(finalSendSuccess);

                emit clientConnected(pcId);
                // ownerId는 response에 없으므로 request에서 가져옴
                finalizePCRegistration(pcId, true, request.ownerId);
            } else {
                sendPCInfoResponse(clientSocket, false, false, "Database registration failed");
                cleanupClient(clientSocket);
            }
        }
    } else {
        // 검증 실패
        QString errorMsg = response.reason.isEmpty() ? "Owner verification failed" : response.reason;
        sendPCInfoResponse(clientSocket, false, false, errorMsg);
        cleanupClient(clientSocket);

        finalizePCRegistration(pcId, false, "");
    }
}

// =================================================================
// 🔧 processForensicData 함수 수정 (복합 패킷 파싱 지원)
// =================================================================

void NetworkManager::processForensicData(const ClientInfo& client, const QByteArray& data) {
    try {
        qDebug() << QString("[NetworkManager] 포렌식 데이터 처리 시작: PC=%1, 크기=%2")
                        .arg(client.pcId).arg(data.size());

        // 🎯 새로운 복합 패킷 파싱 로직
        QString currentTaskId;
        QString moduleType;
        QJsonObject forensicJsonData;

        // 1단계: 데이터 형식 감지 및 파싱
        if (data.size() >= 4) {
            // 첫 4바이트가 메타데이터 크기인지 확인 (복합 패킷 형식)
            QDataStream stream(data);
            stream.setByteOrder(QDataStream::LittleEndian);

            quint32 potentialMetadataSize;
            stream >> potentialMetadataSize;

            // 복합 패킷 형식인지 검증 (메타데이터 크기가 합리적인 범위인지)
            if (potentialMetadataSize > 0 &&
                potentialMetadataSize < 10000 &&
                (4 + potentialMetadataSize) < data.size()) {

                qDebug() << QString("[NetworkManager] 복합 패킷 감지 - 메타데이터 크기: %1").arg(potentialMetadataSize);

                // 복합 패킷 파싱
                QByteArray metadataBytes = data.mid(4, potentialMetadataSize);
                QByteArray actualDataBytes = data.mid(4 + potentialMetadataSize);

                // 메타데이터 파싱
                QJsonDocument metadataDoc = QJsonDocument::fromJson(metadataBytes);
                if (metadataDoc.isObject()) {
                    QJsonObject metadata = metadataDoc.object();
                    currentTaskId = metadata.value("task_id").toString();
                    moduleType = metadata.value("module_type").toString();

                    qDebug() << QString("[NetworkManager] 메타데이터 파싱 성공:");
                    qDebug() << "  - Task ID:" << currentTaskId;
                    qDebug() << "  - Module Type:" << moduleType;
                    qDebug() << "  - 실제 데이터 크기:" << actualDataBytes.size();
                }

                // 실제 포렌식 데이터 파싱
                QJsonDocument forensicDoc = QJsonDocument::fromJson(actualDataBytes);
                if (!forensicDoc.isObject()) {
                    qWarning() << "[NetworkManager] 실제 포렌식 데이터가 JSON 객체가 아님";
                    return;
                }
                forensicJsonData = forensicDoc.object();

            } else {
                // 단순 JSON 형식 (기존 방식)
                qDebug() << "[NetworkManager] 단순 JSON 형식 감지";

                QJsonDocument doc = QJsonDocument::fromJson(data);
                if (!doc.isObject()) {
                    qWarning() << "[NetworkManager] 포렌식 데이터가 JSON 객체가 아님";
                    return;
                }

                forensicJsonData = doc.object();

                // 기존 Task ID 조회 방식
                currentTaskId = getClientTaskId(client.pcId);
                if (currentTaskId.isEmpty() && forensicJsonData.contains("task_id")) {
                    currentTaskId = forensicJsonData["task_id"].toString();
                }
            }
        } else {
            qWarning() << "[NetworkManager] 데이터 크기가 너무 작음: " << data.size();
            return;
        }

        // 2단계: Task ID 검증 및 기본값 설정
        if (currentTaskId.isEmpty()) {
            qWarning() << QString("[NetworkManager] Task ID를 찾을 수 없음 - PC: %1").arg(client.pcId);

            // 🚨 긴급 수정: Task ID 없어도 저장하도록 기본값 설정
            currentTaskId = "unknown_task_" + QString::number(QDateTime::currentMSecsSinceEpoch());
            qWarning() << QString("[NetworkManager] 기본 Task ID 사용: %1").arg(currentTaskId);
        }

        // 3단계: Module Type 감지
        if (moduleType.isEmpty()) {
            moduleType = detectModuleType(forensicJsonData);
            if (moduleType.isEmpty() || moduleType == "unknown") {
                // JSON에서 직접 추출 시도
                if (forensicJsonData.contains("module_type")) {
                    moduleType = forensicJsonData["module_type"].toString();
                } else if (forensicJsonData.contains("moduleType")) {
                    moduleType = forensicJsonData["moduleType"].toString();
                } else {
                    moduleType = "UNKNOWN_DATA";
                }
            }
        }

        qDebug() << QString("[NetworkManager] 포렌식 데이터 정보:");
        qDebug() << "  - PC ID:" << client.pcId;
        qDebug() << "  - Task ID:" << currentTaskId;
        qDebug() << "  - Module Type:" << moduleType;
        qDebug() << "  - Data Size:" << data.size();

        // 4단계: ForensicInfo 구조체 생성
        DatabaseManager::ForensicInfo forensicInfo;
        forensicInfo.pcId = client.pcId;
        forensicInfo.taskId = currentTaskId;
        forensicInfo.moduleType = moduleType;
        forensicInfo.collectionTime = QDateTime::currentDateTime();
        forensicInfo.jsonData = forensicJsonData;
        forensicInfo.fileSize = data.size();

        // 5단계: 데이터베이스 저장 (기존 인터페이스 사용)
        if (databaseManager_) {
            int forensicId = databaseManager_->storeForensicData(forensicInfo);
            if (forensicId > 0) {
                qDebug() << QString("[NetworkManager] 포렌식 데이터 저장 성공: DB ID=%1, Task ID=%2")
                                .arg(forensicId).arg(currentTaskId);

                emit forensicDataStoredInDB(currentTaskId, forensicId);
                databaseManager_->updateClientRecentScan(client.pcId, QDateTime::currentDateTime());

                // Task 완료 알림 (백엔드)
                if (backendApiClient_ && !currentTaskId.isEmpty() &&
                    !currentTaskId.startsWith("unknown_task_")) {

                    qDebug() << QString("[NetworkManager] Task 완료 확인 중: %1").arg(currentTaskId);

                    // Task 완료 알림 요청
                    BackendApi::TaskCompleteRequest completeRequest;
                    completeRequest.taskId = currentTaskId;
                    completeRequest.pcId = client.pcId;
                    completeRequest.isSuccess = true;
                    completeRequest.moduleType = moduleType;
                    completeRequest.ownerId = "default";  // request용
                    completeRequest.taskStartTime = QDateTime::currentDateTime();
                    completeRequest.taskEndTime = QDateTime::currentDateTime();
                    completeRequest.errorMessage = "";

                    // 결과 정보
                    QJsonObject summaryInfo;
                    summaryInfo["data_size"] = static_cast<qint64>(data.size());
                    summaryInfo["file_count"] = 1;
                    summaryInfo["module_type"] = moduleType;
                    summaryInfo["collection_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);

                    completeRequest.summary = summaryInfo;

                    backendApiClient_->notifyTaskComplete(completeRequest);
                    emit backendRequestSent("task-complete", currentTaskId);

                    qDebug() << QString("[NetworkManager] Task 완료 알림 전송: %1").arg(currentTaskId);
                }

                qInfo() << QString("[NetworkManager] ✅ 데이터 저장 완료: PC=%1, Task=%2, Module=%3, Size=%4")
                               .arg(client.pcId, currentTaskId, moduleType).arg(data.size());
            } else {
                qCritical() << QString("[NetworkManager] 포렌식 데이터 저장 실패: Task ID=%1").arg(currentTaskId);
            }
        } else {
            qCritical() << "[NetworkManager] DatabaseManager가 없어 데이터 저장 불가";
        }

    } catch (const std::exception& e) {
        qCritical() << QString("[NetworkManager] 포렌식 데이터 처리 중 예외: %1").arg(e.what());
    }
}

// =================================================================
// 🔧 수정 대상: updateConnectedClientsList 함수 (NetworkManager.cpp에서)
// =================================================================

// =================================================================
// 🔧 updateConnectedClientsList 함수 수정 (ClientInfo 구조체 올바른 생성)
// =================================================================

void NetworkManager::updateConnectedClientsList(SOCKET clientSocket, const QString& pcId,
                                                const PCRegistrationInfo& pcInfo) {
    QMutexLocker locker(&clientsMutex_);

    // 🎯 기존 NetworkManager::ClientInfo 구조체 생성 (기존 구조 사용)
    ClientInfo clientInfo;

    // 기본 PC 정보 (데이터베이스와 일치)
    clientInfo.pcId = pcInfo.pcId;           // ✅ MAC_00-15-5D-00-02-01
    clientInfo.pcName = pcInfo.pcName;       // ✅ Windows PC 이름
    clientInfo.ip = pcInfo.ip;               // ✅ IP 주소
    clientInfo.os = pcInfo.os;               // ✅ OS 정보
    clientInfo.firstConnect = pcInfo.timestamp;
    clientInfo.lastConnect = pcInfo.timestamp;
    clientInfo.recentScan = QDateTime(); // NULL 상태 (아직 스캔 없음)

    // 네트워크 연결 정보
    clientInfo.socket = clientSocket;        // Windows 소켓
    clientInfo.macAddress = pcInfo.primaryMac; // MAC 주소 (원본 형식)
    clientInfo.lastSeen = QDateTime::currentDateTime();

    qDebug() << "[NetworkManager] ClientInfo 구조체 생성 완료:";
    qDebug() << "  - PC ID:" << clientInfo.pcId;
    qDebug() << "  - PC Name:" << clientInfo.pcName;
    qDebug() << "  - IP:" << clientInfo.ip;
    qDebug() << "  - OS:" << clientInfo.os;
    qDebug() << "  - Socket:" << clientSocket;
    qDebug() << "  - MAC:" << clientInfo.macAddress;

    // 🎯 기존 클라이언트가 있으면 제거 (재연결 처리)
    for (auto it = connectedClients_.begin(); it != connectedClients_.end(); ++it) {
        if (it->pcId == pcId) {
            qDebug() << QString("[NetworkManager] 기존 클라이언트 제거: %1").arg(pcId);
            connectedClients_.erase(it);
            break;
        }
    }

    // 🎯 새 클라이언트 추가
    connectedClients_.append(clientInfo);

    // 소켓-클라이언트 매핑 추가
    socketToClientId_[clientSocket] = pcId;

    qDebug() << QString("[NetworkManager] 연결된 클라이언트 목록에 추가: %1 (총 %2개)")
                    .arg(pcId).arg(connectedClients_.size());

    // 연결 시그널 발생
    emit clientConnected(pcId);
}

void NetworkManager::cleanupClient(SOCKET clientSocket) {
    QMutexLocker locker(&clientsMutex_);

    QString clientId = socketToClientId_.value(clientSocket, QString());

    // 연결된 클라이언트 목록에서 제거
    for (int i = connectedClients_.size() - 1; i >= 0; --i) {
        if (connectedClients_[i].socket == clientSocket) {
            connectedClients_.removeAt(i);
            break;
        }
    }

    // 매핑 제거
    socketToClientId_.remove(clientSocket);

    // 소켓 닫기
    if (clientSocket != INVALID_SOCKET) {
        closesocket(clientSocket);
    }

    // 🆕 Task 실패 처리: Task ID 정리 전에 미완료 Task 실패 알림
    if (!clientId.isEmpty()) {
        QString currentTaskId = getClientTaskId(clientId);

        // Task ID가 있고 유효한 Task인 경우
        if (!currentTaskId.isEmpty() && !currentTaskId.startsWith("unknown_task_")) {
            if (databaseManager_) {
                int moduleCount = databaseManager_->getModuleCountForTask(currentTaskId);

                // 6개 미만이면 실패 처리
                if (moduleCount < 6) {
                    QStringList missingModules = databaseManager_->getMissingModulesForTask(currentTaskId);
                    QString errorMessage = QString("Client disconnected. Missing modules: %1")
                                               .arg(missingModules.join(", "));

                    qDebug() << QString("[NetworkManager] Client %1 disconnected with incomplete Task %2 (%3/6 modules)")
                                    .arg(clientId, currentTaskId).arg(moduleCount);

                    // 🔧 스레드 안전 처리: QMetaObject::invokeMethod로 메인 스레드에서 실행
                    QMetaObject::invokeMethod(this, [this, currentTaskId, missingModules]() {
                        notifyTaskFailureForMissingModules(currentTaskId, missingModules);
                    }, Qt::QueuedConnection);
                }
            }
        }

        // 기존 Task ID 정리
        clearClientTaskId(clientId);
    }

    qDebug() << QString("[NetworkManager] 클라이언트 정리 완료: %1").arg(clientId);
}

bool NetworkManager::sendBinaryMessageToSocket(SOCKET socket, BinaryMessageType messageType,
                                               const QByteArray& payload) {
    try {
        QByteArray packet;

        // 메시지 길이 (1바이트 타입 + 페이로드)
        uint32_t totalSize = 1 + payload.size();
        uint32_t networkSize = qToLittleEndian(totalSize);

        packet.append(reinterpret_cast<const char*>(&networkSize), sizeof(networkSize));
        packet.append(static_cast<char>(messageType));
        packet.append(payload);

        qDebug() << QString(">> [DEBUG_SEND] Sending packet of size: %1 bytes").arg(packet.size());
        // 전송
        int totalSent = 0;
        int packetSize = packet.size();

        while (totalSent < packetSize) {
            int sent = send(socket, packet.data() + totalSent, packetSize - totalSent, 0);
            if (sent == SOCKET_ERROR) {
                qWarning() << "[NetworkManager] 메시지 전송 실패:" << WSAGetLastError();
                qWarning() << QString(">> [DEBUG_SEND] Send error: %1, WSAGetLastError: %2").arg(sent).arg(WSAGetLastError());
                return false;
            }
            totalSent += sent;
            qDebug() << QString(">> [DEBUG_SEND] Sent %1 bytes, totalSent: %2/%3").arg(sent).arg(totalSent).arg(packetSize);
        }

        qDebug() << QString(">> [DEBUG_SEND] Packet fully sent. Total: %1 bytes").arg(totalSent);
        return true;

    } catch (const std::exception& e) {
        qCritical() << QString("[NetworkManager] 메시지 전송 중 예외: %1").arg(e.what());
        return false;
    }
}

// =================================================================
// 🔧 유틸리티 메서드들
// =================================================================

QString NetworkManager::detectModuleType(const QJsonObject& jsonObj) {
    // JSON 객체에서 모듈 타입 추정
    if (jsonObj.contains("moduleType")) {
        return jsonObj["moduleType"].toString();
    }

    // 키 기반 추정
    if (jsonObj.contains("processes")) return "process";
    if (jsonObj.contains("registry")) return "registry";
    if (jsonObj.contains("files")) return "file";
    if (jsonObj.contains("network")) return "network";
    if (jsonObj.contains("usb")) return "usb";
    if (jsonObj.contains("memory")) return "memory";
    if (jsonObj.contains("event_logs")) return "eventlog";
    if (jsonObj.contains("browser")) return "browser";

    return "unknown";
}

// =================================================================
// 🔧 parsePCInfoJson 함수 수정 (핵심 문제 해결!)
// =================================================================

NetworkManager::PCRegistrationInfo NetworkManager::parsePCInfoJson(const QJsonObject& jsonData) {
    qDebug() << "[DEBUG] Received JSON for PC Info parsing:" << QJsonDocument(jsonData).toJson(QJsonDocument::Compact);

    PCRegistrationInfo info;

    // 🎯 클라이언트가 보내는 정확한 JSON 구조에 맞춰 파싱
    // 클라이언트 JSON: {"pc_id": "MAC_00-15-5D-00-02-01", "pc_name": "DESKTOP-ABC", "ip": "192.168.1.100", "os": "Windows 10"}

    // 직접 필드 매핑 (기존 잘못된 파싱 수정)
    info.pcId = jsonData.value("pc_id").toString();           // ✅ 정확한 필드명
    info.pcName = jsonData.value("pc_name").toString();       // ✅ 정확한 필드명
    info.ip = jsonData.value("ip").toString();                // ✅ 정확한 필드명
    info.os = jsonData.value("os").toString();                // ✅ 정확한 필드명

    // MAC 주소 추출 (pc_id에서)
    if (info.pcId.startsWith("MAC_")) {
        info.primaryMac = info.pcId.mid(4); // "MAC_" 제거
        info.primaryMac.replace("-", ":");  // 형식 변환: AA-BB-CC → AA:BB:CC
    }

    // 타임스탬프 설정
    info.timestamp = QDateTime::currentDateTime();

    // 🔍 디버그 로그 (수정된 파싱 결과 확인)
    qDebug() << "[DEBUG] Parsed PC Info:";
    qDebug() << "  - PC ID:" << info.pcId;
    qDebug() << "  - PC Name:" << info.pcName;
    qDebug() << "  - IP:" << info.ip;
    qDebug() << "  - OS:" << info.os;
    qDebug() << "  - Primary MAC:" << info.primaryMac;

    // 유효성 검증
    if (info.pcId.isEmpty() || info.pcName.isEmpty()) {
        qWarning() << "[NetworkManager] 필수 PC 정보가 누락됨";
        qWarning() << "  - PC ID empty:" << info.pcId.isEmpty();
        qWarning() << "  - PC Name empty:" << info.pcName.isEmpty();
    }

    return info;
}

QString NetworkManager::generateClientIdFromMAC(const QString& macAddress) {
    return DatabaseSchema::generatePcIdFromMac(macAddress);
}

void NetworkManager::updateLastActivity() {
    QMutexLocker locker(&statusMutex_);
    lastActivityTime_ = QDateTime::currentDateTime();
}

void NetworkManager::logActivity(const QString& activity) {
    qDebug() << QString("[NetworkManager] 활동: %1").arg(activity);
    updateLastActivity();
}

// =================================================================
// DatabaseManager 연동 슬롯들
// =================================================================

void NetworkManager::onDatabaseConnected() {
    qDebug() << "[NetworkManager] 데이터베이스 연결됨";
}

void NetworkManager::onDatabaseDisconnected() {
    qWarning() << "[NetworkManager] 데이터베이스 연결 해제됨";
}

void NetworkManager::onClientInfoChanged(const BackendApi::ClientUpdateRequest& updateRequest) {
    qDebug() << QString("[NetworkManager] 클라이언트 정보 변경 감지: %1").arg(updateRequest.pcId);

    if (backendApiClient_ && backendApiConfigured_) {
        backendApiClient_->notifyClientUpdate(updateRequest);
        emit backendRequestSent("client-update", updateRequest.pcId);
    }
}

void NetworkManager::onTaskCompletionNotification(const BackendApi::TaskCompleteRequest& completeRequest) {
    qDebug() << QString("[NetworkManager] Task 완료 알림 요청: %1").arg(completeRequest.taskId);

    if (backendApiClient_ && backendApiConfigured_) {
        backendApiClient_->notifyTaskComplete(completeRequest);
        emit backendRequestSent("task-complete", completeRequest.taskId);
    }
}

void NetworkManager::onForensicDataStored(int forensicId, const QString& taskId) {
    qDebug() << QString("[NetworkManager] 포렌식 데이터 저장 완료: Task %1, ID %2")
                    .arg(taskId).arg(forensicId);

    emit forensicDataStoredInDB(taskId, forensicId);

    // Task 완료 처리 계속
    if (!taskId.isEmpty()) {
        notifyBackendOfTaskCompletion(taskId);
    }
}

// =================================================================
// 🔗 BackendApiClient 연동 슬롯들
// =================================================================
void NetworkManager::onClientUpdateNotified(const BackendApi::ClientUpdateRequest& request,
                                            const BackendApi::ClientUpdateResponse& response, bool success) {
    Q_UNUSED(response)

    qDebug() << QString("[NetworkManager] 클라이언트 업데이트 알림 완료: %1, 성공: %2")
                    .arg(request.pcId).arg(success);

    emit backendResponseReceived("client-update", request.pcId, success);
}

void NetworkManager::onTaskCompleteNotified(const BackendApi::TaskCompleteRequest& request,
                                            const BackendApi::TaskCompleteResponse& response, bool success) {
    Q_UNUSED(response)

    qDebug() << QString("[NetworkManager] Task 완료 알림 완료: %1, 성공: %2")
                    .arg(request.taskId).arg(success);

    finalizeTaskCompletion(request.taskId, success);

    emit backendResponseReceived("task-complete", request.taskId, success);
}

void NetworkManager::onBackendConnectionTested(bool available) {
    qDebug() << QString("[NetworkManager] 백엔드 연결 테스트 결과: %1")
                    .arg(available ? "사용 가능" : "사용 불가");

    updateBackendApiStatus(available);
    emit backendConnectionTested(available);
}

void NetworkManager::onPCRegistrationStarted(const QString& pcId, const PCRegistrationStatus& status) {
    Q_UNUSED(pcId)
    Q_UNUSED(status)

    qDebug() << "[NetworkManager] onPCRegistrationStarted 호출됨 - PC ID:" << pcId;
    // TODO: PC 등록 시작 처리 로직 추가 예정
}

void NetworkManager::onPCRegistrationCompleted(const QString& pcId, bool success) {
    Q_UNUSED(pcId)
    Q_UNUSED(success)

    qDebug() << "[NetworkManager] onPCRegistrationCompleted 호출됨 - PC ID:" << pcId << "성공:" << success;
    // TODO: PC 등록 완료 처리 로직 추가 예정
}

QString NetworkManager::extractMacFromPcId(const QString& pcId) {
    // PC ID 형식: "MAC_00-15-5D-00-02-01" → "00:15:5D:00:02:01"
    if (!pcId.startsWith("MAC_")) {
        return QString();
    }

    QString macPart = pcId.mid(4); // "MAC_" 제거
    return macPart.replace("-", ":"); // "-"를 ":"로 변경
}

bool NetworkManager::isAllModulesCompleted(const QString& taskId) {
    if (!databaseManager_) {
        qWarning() << "[NetworkManager] DatabaseManager가 없어 Task 완료 여부 확인 불가";
        return false;
    }

    // DB에서 해당 Task ID로 저장된 포렌식 데이터 조회
    QList<DatabaseManager::ForensicInfo> forensicList = databaseManager_->getForensicDataByTaskId(taskId);

    if (forensicList.size() < TOTAL_REQUIRED_MODULES) {
        qDebug() << QString("[NetworkManager] Task %1: 저장된 모듈 수 %2/6 (미완료)")
                        .arg(taskId).arg(forensicList.size());
        return false;
    }

    // 각 모듈 타입이 모두 존재하는지 확인
    QSet<QString> storedModules;
    for (const auto& forensicInfo : forensicList) {
        storedModules.insert(forensicInfo.moduleType);
    }

    for (const QString& requiredModule : REQUIRED_MODULE_TYPES) {
        if (!storedModules.contains(requiredModule)) {
            qDebug() << QString("[NetworkManager] Task %1: 누락된 모듈 %2").arg(taskId, requiredModule);
            return false;
        }
    }

    qDebug() << QString("[NetworkManager] Task %1: 모든 모듈(6개) 완료 확인").arg(taskId);
    return true;
}

QStringList NetworkManager::getMissingModuleTypes(const QString& taskId) {
    QStringList missingModules;

    if (!databaseManager_) {
        return REQUIRED_MODULE_TYPES; // 전체 모듈을 누락으로 간주
    }

    // DB에서 저장된 모듈 타입들 조회
    QList<DatabaseManager::ForensicInfo> forensicList = databaseManager_->getForensicDataByTaskId(taskId);

    QSet<QString> storedModules;
    for (const auto& forensicInfo : forensicList) {
        storedModules.insert(forensicInfo.moduleType);
    }

    // 누락된 모듈 찾기
    for (const QString& requiredModule : REQUIRED_MODULE_TYPES) {
        if (!storedModules.contains(requiredModule)) {
            missingModules.append(requiredModule);
        }
    }

    return missingModules;
}

void NetworkManager::checkAndProcessTaskCompletion(const QString& taskId) {
    qDebug() << QString("[NetworkManager] Task 완료 상태 확인 시작: %1").arg(taskId);

    if (isAllModulesCompleted(taskId)) {
        // 🎯 6개 모듈 모두 완료 → 성공 알림
        qDebug() << QString("[NetworkManager] Task %1: 모든 모듈 완료 → 백엔드 성공 알림").arg(taskId);

        // Task 완료 상태 업데이트
        QMutexLocker locker(&taskCompletionMutex_);
        auto it = taskCompletionStatuses_.find(taskId);
        if (it != taskCompletionStatuses_.end()) {
            it.value().currentStatus = "notifying";
        }

        // 백엔드 완료 알림 (기존 함수 재사용)
        notifyBackendOfTaskCompletion(taskId);

    } else {
        // 🎯 6개 미만 → 일정 시간 후 실패 처리 (타이머 이용)
        qDebug() << QString("[NetworkManager] Task %1: 모듈 미완료 → 대기 중").arg(taskId);

        // 10초 후 다시 확인하여 여전히 미완료면 실패 처리
        QTimer::singleShot(10000, this, [this, taskId]() {
            if (!isAllModulesCompleted(taskId)) {
                QStringList missingModules = getMissingModuleTypes(taskId);
                qWarning() << QString("[NetworkManager] Task %1: 타임아웃 → 실패 알림 (누락: %2)")
                                  .arg(taskId, missingModules.join(", "));
                notifyTaskFailureForMissingModules(taskId, missingModules);
            }
        });
    }
}

void NetworkManager::notifyTaskFailureForMissingModules(const QString& taskId, const QStringList& missingModules) {
    if (!backendApiClient_ || !backendApiConfigured_) {
        qWarning() << "[NetworkManager] 백엔드 API가 없어 실패 알림 건너뜀:" << taskId;
        finalizeTaskCompletion(taskId, false);
        return;
    }

    QString errorMessage = QString("Missing modules: %1").arg(missingModules.join(", "));

    qDebug() << QString("[NetworkManager] 백엔드에 Task 실패 알림 전송: %1 (이유: %2)")
                    .arg(taskId, errorMessage);

    // Task 완료 상태 업데이트
    QMutexLocker locker(&taskCompletionMutex_);
    auto it = taskCompletionStatuses_.find(taskId);
    if (it != taskCompletionStatuses_.end()) {
        it.value().currentStatus = "failed";
        it.value().errorMessage = errorMessage;
    }

    // 백엔드 실패 알림 전송
    backendApiClient_->notifyTaskFailure(taskId, errorMessage);

    emit backendRequestSent("task-failure", taskId);
    emit taskCompletionFinished(taskId, false);
}
