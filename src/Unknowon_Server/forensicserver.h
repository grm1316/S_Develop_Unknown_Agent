#ifndef FORENSICSERVER_H
#define FORENSICSERVER_H

#include "pch.h"
#include "NetworkManager.h"
#include "DatabaseManager.h"
#include "BackendApiClient.h"
#include "httpapihandler.h"
#include "backend_types.h"
#include <QTimer>

// =================================================================
// ForensicServer - 새로운 NetworkManager와 완전 연동
// 4가지 핵심 기능 지원: PC등록, 변경감지, Task완료, 백엔드연동
// =================================================================

class ForensicServer : public QObject {
    Q_OBJECT

public:
    // 서버 상태 열거형
    enum ServerState {
        STATE_STOPPED = 0,      // 중지됨
        STATE_STARTING = 1,     // 시작 중
        STATE_RUNNING = 2,      // 실행 중
        STATE_STOPPING = 3,     // 중지 중
        STATE_ERROR = 4         // 오류 상태
    };

    // 서버 설정 구조체
    struct ServerConfig {
        // 네트워크 설정
        QString listenAddress = "0.0.0.0";
        uint16_t port = 8443;
        QString certPath;
        QString keyPath;

        // 데이터베이스 설정
        QString dbHost = "localhost";
        int dbPort = 5432;
        QString dbName = "forensic_agent";
        QString dbUser = "forensic_agent";
        QString dbPassword = "0814";

        // 🆕 백엔드 API 설정
        QString backendBaseUrl = "http://backend.unknownlite.com";
        QString backendApiKey = "adfawirovansdifuhaworgnkjsdfh2345h2woeg8w3rgakljshdf";
        int backendTimeout = 10000;
        int backendRetryCount = 3;
        bool enableBackendApi = false;

        // 🔐 암호화 설정 (새로 추가)
        bool enableEncryption = true;                           // 암호화 활성화 여부
        QString encryptionKey = "Unknownserver2025!securekey"; // AES-256 암호화 키 (32자 이상 권장)

        // 서버 옵션
        int maxClients = 100;
        int heartbeatTimeout = 60;
        bool autoStart = true;
        bool logToFile = true;
        QString logPath = "forensic_server.log";

        ServerConfig() = default;
    };

    // 서버 통계 구조체
    struct ServerStats {
        QDateTime startTime;
        int totalConnections = 0;
        int currentConnections = 0;
        int totalDataReceived = 0;
        int totalErrors = 0;
        QDateTime lastActivity;

        // 🆕 4가지 핵심 기능 통계
        int totalPCRegistrations = 0;
        int successfulPCRegistrations = 0;
        int failedPCRegistrations = 0;
        int pcChangesDetected = 0;
        int tasksCompleted = 0;
        int backendRequestsSent = 0;
        int backendRequestsSucceeded = 0;

        ServerStats() {
            lastActivity = QDateTime::currentDateTime();
        }
    };

public:
    // 생성자 및 소멸자
    explicit ForensicServer(QObject* parent = nullptr);
    explicit ForensicServer(const ServerConfig& config, QObject* parent = nullptr);
    virtual ~ForensicServer();

    // 서버 제어
    bool start();
    void stop();

    // 상태 조회
    ServerState getState() const;
    bool isRunning() const { return currentState_ == STATE_RUNNING; }
    ServerStats getStats() const;
    ServerConfig getConfig() const { return config_; }

    // 설정 관리
    void setConfig(const ServerConfig& config);
    bool loadConfigFromFile(const QString& filePath);
    bool saveConfigToFile(const QString& filePath);
    static ServerConfig getDefaultConfig();

    // 🆕 백엔드 API 설정
    void configureBackendApi(const BackendApi::BackendConfig& backendConfig);
    bool isBackendApiEnabled() const;
    void testBackendConnection();

    // 클라이언트 관리 (기존 인터페이스 유지)
    QList<NetworkManager::ClientInfo> getConnectedClients() const;
    bool disconnectClient(const QString& clientId);
    bool sendTaskToClient(const QString& clientId, const QString& taskType,
                          const QJsonObject& parameters);

    // 🆕 새로운 4가지 핵심 기능 인터페이스

    // 1. PC 등록 상태 조회
    NetworkManager::PCRegistrationStatus getPCRegistrationStatus(const QString& pcId);
    bool isPCRegistered(const QString& pcId);

    // 2. PC 변경 감지 결과 조회
    NetworkManager::PCChangeDetectionResult getPCChangeResult(const QString& pcId);

    // 3. Task 완료 상태 조회
    NetworkManager::TaskCompletionStatus getTaskCompletionStatus(const QString& taskId);
    bool isTaskCompleted(const QString& taskId);

    // 4. 백엔드 API 상태 조회
    bool isBackendAvailable() const;
    BackendApiClient::DiagnosticInfo getBackendDiagnostics() const;

    // 데이터베이스 관리
    bool isDatabaseConnected() const;
    QString getDatabaseStatus() const;  // DatabaseStats 대신 상태 문자열 반환

    // 진단 및 모니터링
    void printServerStatus() const;
    void printDiagnostics() const;
    QString getServerStatusJson() const;

signals:
    // 서버 상태 신호들
    void serverStarted();
    void serverStopped();
    void serverError(const QString& error);
    void stateChanged(ServerState newState);

    // 클라이언트 관련 신호들 (기존 호환성)
    void clientConnected(const QString& clientId, const QString& ipAddress, const QString& hostname);
    void clientDisconnected(const QString& clientId, const QString& hostname);
    void clientRegistered(const QString& clientId, const QJsonObject& clientInfo);

    // 데이터 관련 신호들 (기존 호환성)
    void forensicDataReceived(const QString& clientId, const QString& moduleType, int dataSize);
    void forensicDataStored(const QString& dataId, const QString& clientId, const QString& moduleType);

    // 작업 관련 신호들 (기존 호환성)
    void taskCreated(const QString& taskId, const QString& clientId, const QString& taskType);
    void taskCompleted(const QString& taskId, const QString& clientId);

    // 🆕 새로운 4가지 핵심 기능 신호들

    // 1. PC 등록 프로세스
    void pcRegistrationStarted(const QString& pcId);
    void pcRegistrationCompleted(const QString& pcId, bool success);
    void ownerIdVerificationRequested(const QString& pcId);
    void ownerIdVerificationCompleted(const QString& pcId, bool verified);

    // 2. PC 정보 변경 감지
    void pcInfoChanged(const QString& pcId, const QStringList& changedFields);
    void pcChangeNotificationSent(const QString& pcId, bool success);

    // 3. Task 완료 알림
    void taskCompletionProcessStarted(const QString& taskId, const QString& pcId);
    void taskCompletionProcessFinished(const QString& taskId, bool success);
    void forensicDataStoredInDatabase(const QString& taskId, int forensicId);

    // 4. 백엔드 API 연동
    void backendApiConfigured(bool success);
    void backendConnectionStatusChanged(bool available);
    void backendRequestCompleted(const QString& apiName, bool success);

private slots:
    // =================================================================
    // 🔄 기존 NetworkManager 슬롯들 (기존 호환성 유지)
    // =================================================================
    void onClientConnected(const QString& clientId);
    void onClientDisconnected(const QString& clientId);
    void onForensicDataReceived(const QString& clientId, const ForensicData& data);
    void onTaskCompleted(const QString& clientId, const QString& taskId);
    void onTaskFailed(const QString& clientId, const QString& taskId, const QString& error);
    void onPCInfoReceived(const QString& clientId, const NetworkManager::PCRegistrationInfo& pcInfo);
    void onClientNeedsRegistration(const QString& clientId, const QString& ipAddress, const QString& hostname);

    // =================================================================
    // 🆕 새로운 4가지 핵심 기능 슬롯들
    // =================================================================

    // 1. PC 등록 프로세스 슬롯들
    void onPCRegistrationStarted(const QString& pcId, const NetworkManager::PCRegistrationStatus& status);
    void onPCRegistrationCompleted(const QString& pcId, bool success);
    void onOwnerIdVerificationNeeded(const QString& pcId, const BackendApi::VerifyOwnerRequest& request);
    void onOwnerIdVerificationResult(const QString& pcId, bool verified, const QString& ownerId);

    // 2. PC 정보 변경 감지 슬롯들
    void onPCChangesDetected(const QString& pcId, const NetworkManager::PCChangeDetectionResult& changeResult);
    void onPCChangesProcessed(const QString& pcId, bool success);

    // 3. Task 완료 알림 슬롯들
    void onTaskCompletionStarted(const QString& taskId, const NetworkManager::TaskCompletionStatus& status);
    void onTaskCompletionFinished(const QString& taskId, bool success);
    void onForensicDataStoredInDB(const QString& taskId, int forensicId);
    void onTaskCompletionNotifiedToBackend(const QString& taskId, bool success);

    // 4. 백엔드 API 연동 슬롯들
    void onBackendApiConfigured(bool success);
    void onBackendConnectionTested(bool available);
    void onBackendRequestSent(const QString& apiName, const QString& requestId);
    void onBackendResponseReceived(const QString& apiName, const QString& requestId, bool success);

    // =================================================================
    // 🔄 기존 DatabaseManager 슬롯들 (기존 호환성 유지)
    // =================================================================
    void onDatabaseConnected();
    void onDatabaseDisconnected();
    void onDatabaseError(const QString& error);
    void onForensicDataStored(int forensicId, const QString& taskId);

    // =================================================================
    // 주기적 작업들
    // =================================================================
    void performHeartbeatCheck();
    void performStatisticsUpdate();
    void performMaintenanceTasks();

private:
    // =================================================================
    // 핵심 멤버 변수들
    // =================================================================
    ServerConfig config_;
    ServerState currentState_;
    ServerStats stats_;
    mutable QMutex statsMutex_;
    mutable QMutex stateMutex_;

    // =================================================================
    // 🔧 핵심 컴포넌트들 (3개 주요 매니저)
    // =================================================================
    NetworkManager* networkManager_;       // 네트워크 관리자 (새로운 버전)
    DatabaseManager* databaseManager_;     // 데이터베이스 관리자
    BackendApiClient* backendApiClient_;   // 🆕 백엔드 API 클라이언트

    // =================================================================
    // 🔧 추가 컴포넌트들
    // =================================================================
    HttpApiHandler* httpApiHandler_;       // HTTP API 핸들러

    // =================================================================
    // 타이머들
    // =================================================================
    QTimer* heartbeatTimer_;
    QTimer* statsTimer_;
    QTimer* maintenanceTimer_;

private:
    // =================================================================
    // 내부 메서드들
    // =================================================================

    // 초기화 및 정리
    bool initializeComponents();
    void cleanupComponents();
    void setupSignalConnections();
    void setupTimers();

    // 🆕 백엔드 API 초기화
    bool initializeBackendApi();
    void setupBackendApiConnections();

    // 상태 관리
    void setState(ServerState newState);
    void updateStatistics();

    // 설정 관리
    void applyConfig();
    bool validateConfig() const;

    // 🆕 4가지 핵심 기능 내부 처리
    void handlePCRegistrationProcess(const QString& pcId, const NetworkManager::PCRegistrationInfo& pcInfo);
    void handlePCChangeDetection(const QString& pcId, const NetworkManager::PCChangeDetectionResult& changeResult);
    void handleTaskCompletionProcess(const QString& taskId, const NetworkManager::TaskCompletionStatus& status);
    void handleBackendApiIntegration();

    // 진단 및 로깅
    void logServerActivity(const QString& activity);
    void logError(const QString& error);
    void logWarning(const QString& warning);

    // 기존 호환성 메서드들
    void handleLegacyClientConnection(const QString& clientId, const QString& ipAddress, const QString& hostname);
    void handleLegacyForensicData(const QString& clientId, const ForensicData& data);
    void handleLegacyTaskCompletion(const QString& clientId, const QString& taskId);

    // 통계 업데이트 헬퍼들
    void incrementPCRegistrationStats(bool success);
    void incrementPCChangeStats();
    void incrementTaskCompletionStats();
    void incrementBackendRequestStats(bool success);
};

#endif // FORENSICSERVER_H
