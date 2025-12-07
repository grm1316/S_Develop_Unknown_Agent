#include "ForensicServer.h"
#include <QTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QDir>

// =================================================================
// ForensicServer - 새로운 NetworkManager와 완전 연동
// =================================================================

ForensicServer::ForensicServer(QObject* parent)
    : QObject(parent), currentState_(STATE_STOPPED),
    networkManager_(nullptr), databaseManager_(nullptr), backendApiClient_(nullptr),
    httpApiHandler_(nullptr),
    heartbeatTimer_(nullptr), statsTimer_(nullptr), maintenanceTimer_(nullptr) {

    qDebug() << "[ForensicServer] 기본 설정으로 서버 생성 중...";
    config_ = getDefaultConfig();

    // 메타타입 등록
    qRegisterMetaType<ForensicData>("ForensicData");
    qRegisterMetaType<NetworkManager::PCRegistrationInfo>("NetworkManager::PCRegistrationInfo");
    qRegisterMetaType<NetworkManager::PCRegistrationStatus>("NetworkManager::PCRegistrationStatus");
    qRegisterMetaType<NetworkManager::PCChangeDetectionResult>("NetworkManager::PCChangeDetectionResult");
    qRegisterMetaType<NetworkManager::TaskCompletionStatus>("NetworkManager::TaskCompletionStatus");
    qRegisterMetaType<BackendApi::VerifyOwnerRequest>("BackendApi::VerifyOwnerRequest");

    initializeComponents();
}

ForensicServer::ForensicServer(const ServerConfig& config, QObject* parent)
    : QObject(parent), config_(config), currentState_(STATE_STOPPED),
    networkManager_(nullptr), databaseManager_(nullptr), backendApiClient_(nullptr),
    httpApiHandler_(nullptr),
    heartbeatTimer_(nullptr), statsTimer_(nullptr), maintenanceTimer_(nullptr) {

    qDebug() << "[ForensicServer] 사용자 설정으로 서버 생성 중...";

    // 메타타입 등록
    qRegisterMetaType<ForensicData>("ForensicData");
    qRegisterMetaType<NetworkManager::PCRegistrationInfo>("NetworkManager::PCRegistrationInfo");

    initializeComponents();
}

ForensicServer::~ForensicServer() {
    qDebug() << "[ForensicServer] 서버 소멸자 호출";

    if (currentState_ == STATE_RUNNING) {
        stop();
    }

    cleanupComponents();
}

// =================================================================
// 🔧 핵심 초기화 (가장 중요한 부분!)
// =================================================================

bool ForensicServer::initializeComponents() {
    qDebug() << "[ForensicServer] 컴포넌트 초기화 시작...";

    try {
        // 🎯 1. NetworkManager 초기화 (새로운 버전)
        qDebug() << "[ForensicServer] 새로운 NetworkManager 생성 중...";
        networkManager_ = new NetworkManager(config_.listenAddress, config_.port, this);
        if (!networkManager_) {
            throw std::runtime_error("NetworkManager 생성 실패");
        }

        // 🎯 2. DatabaseManager 초기화
        qDebug() << "[ForensicServer] DatabaseManager 생성 중...";
        DatabaseManager::DatabaseConfig dbConfig;
        dbConfig.host = config_.dbHost;
        dbConfig.port = config_.dbPort;
        dbConfig.database = config_.dbName;
        dbConfig.username = config_.dbUser;
        dbConfig.password = config_.dbPassword;

        // 🔐 암호화 설정 전달 (중요!)
        dbConfig.enableEncryption = config_.enableEncryption;
        dbConfig.encryptionKey = config_.encryptionKey;

        qDebug() << "[ForensicServer] 암호화 설정:"
                 << (dbConfig.enableEncryption ? "활성화" : "비활성화");

        databaseManager_ = new DatabaseManager(dbConfig, this);
        if (!databaseManager_) {
            throw std::runtime_error("DatabaseManager 생성 실패");
        }

        // 🔗 NetworkManager ↔ DatabaseManager 연결
        networkManager_->setDatabaseManager(databaseManager_);
        qDebug() << "[ForensicServer] NetworkManager ↔ DatabaseManager 연결 완료";

        // BackendApiClient 초기화 부분
        if (config_.enableBackendApi) {
            qDebug() << "[ForensicServer] BackendApiClient 생성 중...";
            backendApiClient_ = new BackendApiClient(this);

            BackendApi::BackendConfig backendConfig;
            backendConfig.baseUrl = config_.backendBaseUrl;
            backendConfig.apiKey = "adfawirovansdifuhaworgnkjsdfh2345h2woeg8w3rgakljshdf";  // API 키 설정 추가
            backendConfig.timeoutMs = config_.backendTimeout;
            backendConfig.retryCount = config_.backendRetryCount;
            backendConfig.userAgent = "ForensicServer/1.0";

            backendApiClient_->setConfig(backendConfig);

            // NetworkManager ↔ BackendApiClient 연결
            networkManager_->setBackendApiClient(backendApiClient_);
            qDebug() << "[ForensicServer] NetworkManager ↔ BackendApiClient 연결 완료";
        } else {
            qDebug() << "[ForensicServer] 백엔드 API 비활성화됨";
        }

        httpApiHandler_ = new HttpApiHandler(networkManager_, this);
        httpApiHandler_->setDatabaseManager(databaseManager_);

        // 🔗 5. 시그널-슬롯 연결 (가장 중요!)
        setupSignalConnections();

        qDebug() << "[ForensicServer] 모든 컴포넌트 초기화 완료";
        return true;

    } catch (const std::exception& e) {
        qCritical() << "[ForensicServer] 초기화 실패:" << e.what();
        cleanupComponents();
        return false;
    }
}

// =================================================================
// 🔗 시그널-슬롯 연결 (핵심!)
// =================================================================

void ForensicServer::setupSignalConnections() {
    qDebug() << "[ForensicServer] 시그널-슬롯 연결 설정 중...";

    if (!networkManager_) {
        qCritical() << "[ForensicServer] NetworkManager가 없어 시그널 연결 실패";
        return;
    }

    // =================================================================
    // 🔄 기존 NetworkManager 시그널들 (기존 호환성)
    // =================================================================
    connect(networkManager_, &NetworkManager::clientConnected,
            this, &ForensicServer::onClientConnected);
    connect(networkManager_, &NetworkManager::clientDisconnected,
            this, &ForensicServer::onClientDisconnected);
    connect(networkManager_, &NetworkManager::forensicDataReceived,
            this, &ForensicServer::onForensicDataReceived);
    connect(networkManager_, &NetworkManager::taskCompleted,
            this, &ForensicServer::onTaskCompleted);
    connect(networkManager_, &NetworkManager::taskFailed,
            this, &ForensicServer::onTaskFailed);
    connect(networkManager_, &NetworkManager::pcInfoReceived,
            this, &ForensicServer::onPCInfoReceived);
    connect(networkManager_, &NetworkManager::clientNeedsRegistration,
            this, &ForensicServer::onClientNeedsRegistration);

    // =================================================================
    // 🆕 새로운 4가지 핵심 기능 시그널들
    // =================================================================

    // 1. PC 등록 프로세스
    connect(networkManager_, &NetworkManager::pcRegistrationStarted,
            this, &ForensicServer::onPCRegistrationStarted);
    connect(networkManager_, &NetworkManager::pcRegistrationCompleted,
            this, &ForensicServer::onPCRegistrationCompleted);
    connect(networkManager_, &NetworkManager::ownerIdVerificationNeeded,
            this, &ForensicServer::onOwnerIdVerificationNeeded);
    connect(networkManager_, &NetworkManager::ownerIdVerificationResult,
            this, &ForensicServer::onOwnerIdVerificationResult);

    // 2. PC 정보 변경 감지
    connect(networkManager_, &NetworkManager::pcChangesDetected,
            this, &ForensicServer::onPCChangesDetected);
    connect(networkManager_, &NetworkManager::pcChangesProcessed,
            this, &ForensicServer::onPCChangesProcessed);

    // 3. Task 완료 알림
    connect(networkManager_, &NetworkManager::taskCompletionStarted,
            this, &ForensicServer::onTaskCompletionStarted);
    connect(networkManager_, &NetworkManager::taskCompletionFinished,
            this, &ForensicServer::onTaskCompletionFinished);
    connect(networkManager_, &NetworkManager::forensicDataStoredInDB,
            this, &ForensicServer::onForensicDataStoredInDB);
    connect(networkManager_, &NetworkManager::taskCompletionNotifiedToBackend,
            this, &ForensicServer::onTaskCompletionNotifiedToBackend);

    // 4. 백엔드 API 연동
    connect(networkManager_, &NetworkManager::backendApiConfigured,
            this, &ForensicServer::onBackendApiConfigured);
    connect(networkManager_, &NetworkManager::backendConnectionTested,
            this, &ForensicServer::onBackendConnectionTested);
    connect(networkManager_, &NetworkManager::backendRequestSent,
            this, &ForensicServer::onBackendRequestSent);
    connect(networkManager_, &NetworkManager::backendResponseReceived,
            this, &ForensicServer::onBackendResponseReceived);

    // 스레드 문제를 해결하기 위해 NetworkManager의 요청을 받아서 처리
    if (backendApiClient_) {
        connect(networkManager_, &NetworkManager::verifyOwnerRequested,
                backendApiClient_, &BackendApiClient::verifyOwner);
    }

    // =================================================================
    // 🔄 DatabaseManager 시그널들 (기존 호환성)
    // =================================================================
    if (databaseManager_) {
        connect(databaseManager_, &DatabaseManager::databaseConnected,
                this, &ForensicServer::onDatabaseConnected);
        connect(databaseManager_, &DatabaseManager::databaseDisconnected,
                this, &ForensicServer::onDatabaseDisconnected);
        connect(databaseManager_, &DatabaseManager::databaseError,
                this, &ForensicServer::onDatabaseError);
        connect(databaseManager_, &DatabaseManager::forensicDataStored,
                this, &ForensicServer::onForensicDataStored);
    }

    qDebug() << "[ForensicServer] 모든 시그널-슬롯 연결 완료";
}

// =================================================================
// 서버 제어 (기존 인터페이스 유지)
// =================================================================

bool ForensicServer::start() {
    QMutexLocker locker(&stateMutex_);

    if (currentState_ == STATE_RUNNING) {
        qWarning() << "[ForensicServer] 서버가 이미 실행 중입니다";
        return true;
    }

    qDebug() << "[ForensicServer] 서버 시작 중...";
    setState(STATE_STARTING);

    try {
        // 🔗 1. 데이터베이스 연결
        if (!databaseManager_->connect()) {
            throw std::runtime_error("데이터베이스 연결 실패");
        }

        // 🔗 2. NetworkManager 초기화 및 시작
        if (!networkManager_->Init()) {
            throw std::runtime_error("NetworkManager 초기화 실패");
        }

        if (!networkManager_->Start()) {
            throw std::runtime_error("NetworkManager 시작 실패");
        }

        // 🔗 3. 백엔드 API 연결 테스트 (선택적)
        if (backendApiClient_ && config_.enableBackendApi) {
            qDebug() << "[ForensicServer] 백엔드 API 연결 테스트 중...";
            networkManager_->testBackendConnection();
        }

        // 🔗 4. 타이머 설정
        setupTimers();

        // 🔗 5. HTTP API 서버 시작 (선택적)
        if (httpApiHandler_) {
            httpApiHandler_->startServer("0.0.0.0", 8080);
        }

        setState(STATE_RUNNING);
        stats_.startTime = QDateTime::currentDateTime();

        qDebug() << "[ForensicServer] 서버 시작 완료";
        emit serverStarted();
        return true;

    } catch (const std::exception& e) {
        setState(STATE_ERROR);
        QString errorMsg = QString("서버 시작 실패: %1").arg(e.what());
        qCritical() << "[ForensicServer]" << errorMsg;
        emit serverError(errorMsg);
        return false;
    }
}

void ForensicServer::stop() {
    QMutexLocker locker(&stateMutex_);

    if (currentState_ == STATE_STOPPED) {
        return;
    }

    qDebug() << "[ForensicServer] 서버 중지 중...";
    setState(STATE_STOPPING);

    // 타이머 정지
    if (heartbeatTimer_) heartbeatTimer_->stop();
    if (statsTimer_) statsTimer_->stop();
    if (maintenanceTimer_) maintenanceTimer_->stop();

    // HTTP API 서버 정지
    if (httpApiHandler_) {
        httpApiHandler_->stopServer();
    }

    // NetworkManager 정지
    if (networkManager_) {
        networkManager_->Stop();
    }

    // 데이터베이스 연결 해제
    if (databaseManager_) {
        databaseManager_->disconnect();
    }

    setState(STATE_STOPPED);
    qDebug() << "[ForensicServer] 서버 중지 완료";
    emit serverStopped();
}




// =================================================================
// 🆕 새로운 4가지 핵심 기능 슬롯들
// =================================================================

// 1. PC 등록 프로세스 슬롯들
void ForensicServer::onPCRegistrationStarted(const QString& pcId,
                                             const NetworkManager::PCRegistrationStatus& status) {
    qDebug() << QString("[ForensicServer] PC 등록 프로세스 시작: %1").arg(pcId);

    handlePCRegistrationProcess(pcId, NetworkManager::PCRegistrationInfo());

    {
        QMutexLocker locker(&statsMutex_);
        stats_.totalPCRegistrations++;
    }

    emit pcRegistrationStarted(pcId);
}

void ForensicServer::onPCRegistrationCompleted(const QString& pcId, bool success) {
    qDebug() << QString("[ForensicServer] PC 등록 완료: %1, 성공: %2").arg(pcId).arg(success);

    incrementPCRegistrationStats(success);

    emit pcRegistrationCompleted(pcId, success);
}

void ForensicServer::onOwnerIdVerificationNeeded(const QString& pcId,
                                                 const BackendApi::VerifyOwnerRequest& request) {
    qDebug() << QString("[ForensicServer] Owner_ID 검증 필요: %1").arg(pcId);

    // 여기서 사용자에게 Owner_ID 입력을 요청하거나
    // 자동으로 기본 Owner_ID로 처리할 수 있음
    emit ownerIdVerificationRequested(pcId);
}

void ForensicServer::onOwnerIdVerificationResult(const QString& pcId, bool verified, const QString& ownerId) {
    qDebug() << QString("[ForensicServer] Owner_ID 검증 결과: %1, 검증됨: %2, Owner: %3")
                    .arg(pcId).arg(verified).arg(ownerId);

    emit ownerIdVerificationCompleted(pcId, verified);
}

// 2. PC 정보 변경 감지 슬롯들
void ForensicServer::onPCChangesDetected(const QString& pcId,
                                         const NetworkManager::PCChangeDetectionResult& changeResult) {
    qDebug() << QString("[ForensicServer] PC 정보 변경 감지: %1, 변경 필드: %2")
                    .arg(pcId).arg(changeResult.changedFields.join(", "));

    handlePCChangeDetection(pcId, changeResult);

    incrementPCChangeStats();

    emit pcInfoChanged(pcId, changeResult.changedFields);
}

void ForensicServer::onPCChangesProcessed(const QString& pcId, bool success) {
    qDebug() << QString("[ForensicServer] PC 변경 처리 완료: %1, 성공: %2").arg(pcId).arg(success);

    emit pcChangeNotificationSent(pcId, success);
}

// 3. Task 완료 알림 슬롯들
void ForensicServer::onTaskCompletionStarted(const QString& taskId,
                                             const NetworkManager::TaskCompletionStatus& status) {
    qDebug() << QString("[ForensicServer] Task 완료 처리 시작: %1 (PC: %2, 모듈: %3)")
                    .arg(taskId).arg(status.pcId).arg(status.moduleType);

    handleTaskCompletionProcess(taskId, status);

    emit taskCompletionProcessStarted(taskId, status.pcId);
}

void ForensicServer::onTaskCompletionFinished(const QString& taskId, bool success) {
    qDebug() << QString("[ForensicServer] Task 완료 처리 완료: %1, 성공: %2").arg(taskId).arg(success);

    if (success) {
        incrementTaskCompletionStats();
    }

    emit taskCompletionProcessFinished(taskId, success);
}

void ForensicServer::onForensicDataStoredInDB(const QString& taskId, int forensicId) {
    qDebug() << QString("[ForensicServer] 포렌식 데이터 DB 저장 완료: Task %1, ID %2")
                    .arg(taskId).arg(forensicId);

    emit forensicDataStoredInDatabase(taskId, forensicId);
}

void ForensicServer::onTaskCompletionNotifiedToBackend(const QString& taskId, bool success) {
    qDebug() << QString("[ForensicServer] 백엔드 Task 완료 알림: %1, 성공: %2").arg(taskId).arg(success);

    incrementBackendRequestStats(success);
}

// 4. 백엔드 API 연동 슬롯들
void ForensicServer::onBackendApiConfigured(bool success) {
    qDebug() << QString("[ForensicServer] 백엔드 API 설정: %1").arg(success ? "성공" : "실패");

    emit backendApiConfigured(success);
}

void ForensicServer::onBackendConnectionTested(bool available) {
    qDebug() << QString("[ForensicServer] 백엔드 연결 상태: %1")
                    .arg(available ? "사용 가능" : "사용 불가");

    emit backendConnectionStatusChanged(available);
}

void ForensicServer::onBackendRequestSent(const QString& apiName, const QString& requestId) {
    qDebug() << QString("[ForensicServer] 백엔드 요청 전송: %1, ID: %2").arg(apiName).arg(requestId);
}

void ForensicServer::onBackendResponseReceived(const QString& apiName, const QString& requestId, bool success) {
    qDebug() << QString("[ForensicServer] 백엔드 응답 수신: %1, ID: %2, 성공: %3")
                    .arg(apiName).arg(requestId).arg(success);

    incrementBackendRequestStats(success);

    emit backendRequestCompleted(apiName, success);
}

// =================================================================
// 🔄 기존 호환 슬롯들 (기존 코드와 호환성 유지)
// =================================================================

void ForensicServer::onClientConnected(const QString& clientId) {
    qDebug() << QString("[ForensicServer] 클라이언트 연결: %1").arg(clientId);

    {
        QMutexLocker locker(&statsMutex_);
        stats_.totalConnections++;
        stats_.currentConnections++;
        stats_.lastActivity = QDateTime::currentDateTime();
    }

    // 기존 호환 신호
    emit clientConnected(clientId, "unknown", "unknown");
}

void ForensicServer::onClientDisconnected(const QString& clientId) {
    qDebug() << QString("[ForensicServer] 클라이언트 연결 해제: %1").arg(clientId);

    {
        QMutexLocker locker(&statsMutex_);
        stats_.currentConnections--;
        stats_.lastActivity = QDateTime::currentDateTime();
    }

    emit clientDisconnected(clientId, "unknown");
}

void ForensicServer::onForensicDataReceived(const QString& clientId, const ForensicData& data) {
    qDebug() << QString("[ForensicServer] 포렌식 데이터 수신: %1, 모듈: %2, 크기: %3")
                    .arg(clientId).arg(data.moduleType).arg(data.payload.size());

    {
        QMutexLocker locker(&statsMutex_);
        stats_.totalDataReceived++;
        stats_.lastActivity = QDateTime::currentDateTime();
    }

    emit forensicDataReceived(clientId, data.moduleType, data.payload.size());
}

void ForensicServer::onTaskCompleted(const QString& clientId, const QString& taskId) {
    qDebug() << QString("[ForensicServer] Task 완료: %1 (클라이언트: %2)").arg(taskId).arg(clientId);

    emit taskCompleted(taskId, clientId);
}

void ForensicServer::onTaskFailed(const QString& clientId, const QString& taskId, const QString& error) {
    qWarning() << QString("[ForensicServer] Task 실패: %1 (클라이언트: %2, 오류: %3)")
                      .arg(taskId).arg(clientId).arg(error);

    {
        QMutexLocker locker(&statsMutex_);
        stats_.totalErrors++;
    }
}

// =================================================================
// 🔧 내부 처리 메서드들
// =================================================================

void ForensicServer::handlePCRegistrationProcess(const QString& pcId,
                                                 const NetworkManager::PCRegistrationInfo& pcInfo) {
    Q_UNUSED(pcInfo)
    qDebug() << QString("[ForensicServer] PC 등록 프로세스 처리: %1").arg(pcId);
    // 필요시 추가 로직 구현
}

void ForensicServer::handlePCChangeDetection(const QString& pcId,
                                             const NetworkManager::PCChangeDetectionResult& changeResult) {
    Q_UNUSED(changeResult)
    qDebug() << QString("[ForensicServer] PC 변경 감지 처리: %1").arg(pcId);
    // 필요시 추가 로직 구현
}

void ForensicServer::handleTaskCompletionProcess(const QString& taskId,
                                                 const NetworkManager::TaskCompletionStatus& status) {
    Q_UNUSED(status)
    qDebug() << QString("[ForensicServer] Task 완료 처리: %1").arg(taskId);
    // 필요시 추가 로직 구현
}

// =================================================================
// 통계 업데이트 헬퍼들
// =================================================================

void ForensicServer::incrementPCRegistrationStats(bool success) {
    QMutexLocker locker(&statsMutex_);
    if (success) {
        stats_.successfulPCRegistrations++;
    } else {
        stats_.failedPCRegistrations++;
    }
}

void ForensicServer::incrementPCChangeStats() {
    QMutexLocker locker(&statsMutex_);
    stats_.pcChangesDetected++;
}

void ForensicServer::incrementTaskCompletionStats() {
    QMutexLocker locker(&statsMutex_);
    stats_.tasksCompleted++;
}

void ForensicServer::incrementBackendRequestStats(bool success) {
    QMutexLocker locker(&statsMutex_);
    stats_.backendRequestsSent++;
    if (success) {
        stats_.backendRequestsSucceeded++;
    }
}

// =================================================================
// 기타 필수 메서드들 (간략화)
// =================================================================

ForensicServer::ServerConfig ForensicServer::getDefaultConfig() {
    return ServerConfig();
}

void ForensicServer::setState(ServerState newState) {
    if (currentState_ != newState) {
        currentState_ = newState;
        emit stateChanged(newState);
    }
}

void ForensicServer::setupTimers() {
    // 간단한 타이머 설정
    heartbeatTimer_ = new QTimer(this);
    connect(heartbeatTimer_, &QTimer::timeout, this, &ForensicServer::performHeartbeatCheck);
    heartbeatTimer_->start(30000);

    statsTimer_ = new QTimer(this);
    connect(statsTimer_, &QTimer::timeout, this, &ForensicServer::performStatisticsUpdate);
    statsTimer_->start(60000);
}

void ForensicServer::cleanupComponents() {
    // 정리 로직
    if (networkManager_) {
        networkManager_->Stop();
    }
}

// 기타 필수 메서드들의 간단한 구현
void ForensicServer::onDatabaseConnected() { qDebug() << "[ForensicServer] DB 연결됨"; }
void ForensicServer::onDatabaseDisconnected() { qDebug() << "[ForensicServer] DB 연결 해제됨"; }
void ForensicServer::onDatabaseError(const QString& error) { qWarning() << "[ForensicServer] DB 오류:" << error; }
void ForensicServer::onForensicDataStored(int forensicId, const QString& taskId) {
    Q_UNUSED(forensicId);
    qDebug() << "[ForensicServer] 데이터 저장됨:" << taskId;
}
void ForensicServer::performHeartbeatCheck() { /* Heartbeat 로직 */ }
void ForensicServer::performStatisticsUpdate() { /* 통계 업데이트 로직 */ }
void ForensicServer::performMaintenanceTasks() { /* 유지보수 로직 */ }

ForensicServer::ServerState ForensicServer::getState() const { return currentState_; }
ForensicServer::ServerStats ForensicServer::getStats() const { return stats_; }
QList<NetworkManager::ClientInfo> ForensicServer::getConnectedClients() const {
    // 🆕 링크 에러 해결: NetworkManager 호출 제거
    // return networkManager_ ? networkManager_->getConnectedClients() : QList<NetworkManager::ClientInfo>();

    // 임시로 빈 목록 반환 (나중에 NetworkManager 구현 완료 후 활성화)
    QList<NetworkManager::ClientInfo> emptyList;
    qDebug() << "[ForensicServer] getConnectedClients() 호출됨 - 임시 구현";
    return emptyList;
}

bool ForensicServer::isDatabaseConnected() const {
    return databaseManager_ && databaseManager_->isConnected();
}

QString ForensicServer::getDatabaseStatus() const {
    if (!databaseManager_) return "No DatabaseManager";
    if (!databaseManager_->isConnected()) return "Disconnected";
    return "Connected";
}

void ForensicServer::onPCInfoReceived(const QString& clientId, const NetworkManager::PCRegistrationInfo& pcInfo) {
    Q_UNUSED(pcInfo);
    qDebug() << "[ForensicServer] Legacy PC info received from:" << clientId;
    // 새로운 등록 로직은 NetworkManager가 직접 처리하므로, 이 슬롯은 로깅용으로만 사용됩니다.
}

void ForensicServer::onClientNeedsRegistration(const QString& clientId, const QString& ipAddress, const QString& hostname) {
    Q_UNUSED(ipAddress);
    Q_UNUSED(hostname);
    qDebug() << "[ForensicServer] Legacy client needs registration signal from:" << clientId;
    // 새로운 등록 로직은 NetworkManager가 직접 처리하므로, 이 슬롯은 로깅용으로만 사용됩니다.
}
