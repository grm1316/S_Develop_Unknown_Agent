#ifndef NETWORKMANAGER_H
#define NETWORKMANAGER_H

#include "pch.h"
#include "DatabaseManager.h"
#include "BackendApiClient.h"
#include "backend_types.h"
#include <QHash>
#include <QMutex>
#include <QTimer>
#include <thread>
#include <atomic>
#include <vector>

// =================================================================
// NetworkManager - 4가지 핵심 기능 중심으로 완전 재설계
// 1. PC 등록 프로세스 (MAC 기반 + Owner_ID 검증)
// 2. PC 정보 변경 감지 (pc_name, ip, os)
// 3. Task 처리 및 완료 알림 (forensic_info + 백엔드 알림)
// 4. 백엔드 API 연동 (DatabaseManager + BackendApiClient 완전 통합)
// =================================================================

class NetworkManager : public QObject {
    Q_OBJECT

public:
    // =================================================================
    // 🔄 기존 호환성을 위한 구조체들 (기존 코드와 호환)
    // =================================================================

    struct ClientInfo {
        QString pcId;               // pc_id (PRIMARY KEY) - MAC_00-15-5D-00-02-01 형식
        QString pcName;             // pc_name - Windows PC 이름
        QString ip;                 // ip - 현재 IP 주소
        QString os;                 // os - OS 버전 정보
        QDateTime firstConnect;     // first_connect - 최초 연결 시간
        QDateTime lastConnect;      // last_connect - 마지막 연결 시간
        QDateTime recentScan;       // recent_scan - 마지막 Task 완료 시간 (NULL 가능)

        // =================================================================
        SOCKET socket;              // Windows 소켓 (연결 관리용)
        QString macAddress;         // MAC 주소 원본 (00:15:5D:00:02:01 형식) - pcId 생성용
        QDateTime lastSeen;         // 마지막 활동 시간 (하트비트 등) - 연결 관리용
        ClientInfo() : socket(INVALID_SOCKET) {}
    };

    struct TaskRequest {
        QString taskId;
        QString tasktype;      // "USB_DATA", "BROWSER_DATA", "ALL_DATA" 등
        QJsonObject parameters;
        QDateTime requestTime;

        TaskRequest() {
            requestTime = QDateTime::currentDateTime();
        }
    };

    struct PCRegistrationInfo {
        // =================================================================
        // 데이터베이스 스키마와 일치하는 필드들 (ClientInfo와 동일)
        // =================================================================
        QString pcId;               // pc_id (MAC 기반)
        QString pcName;             // pc_name (PC 이름)
        QString ip;                 // ip (IP 주소)
        QString os;                 // os (OS 버전)

        // =================================================================
        // 등록 처리용 추가 정보
        // =================================================================
        QString primaryMac;         // 주 MAC 주소 (00:15:5D:00:02:01 형식)
        QDateTime timestamp;        // 수신 시간

        PCRegistrationInfo() {
            timestamp = QDateTime::currentDateTime();
        }

        // 유효성 검사
        bool isValid() const {
            return !pcId.isEmpty() && !pcName.isEmpty() && !primaryMac.isEmpty();
        }

        // ClientInfo로 변환 (필드명이 동일하므로 단순 복사)
        ClientInfo toClientInfo(SOCKET clientSocket = INVALID_SOCKET) const {
            ClientInfo clientInfo;
            clientInfo.pcId = pcId;
            clientInfo.pcName = pcName;
            clientInfo.ip = ip;
            clientInfo.os = os;
            clientInfo.socket = clientSocket;
            clientInfo.macAddress = primaryMac;
            clientInfo.firstConnect = timestamp;
            clientInfo.lastConnect = timestamp;
            clientInfo.recentScan = timestamp;
            // recentScan은 기본적으로 invalid (null)
            return clientInfo;
        }
    };

    enum class BinaryMessageType : uint8_t {
        DATA_PACKET = 0x01,      // 클라이언트 → 서버 (포렌식 데이터 전송)
        TASK_REQUEST = 0x02,     // 서버 → 클라이언트 (수집 작업 지시)
        TASK_RESPONSE = 0x03,    // 클라이언트 → 서버 (작업 완료 응답)
        PC_INFO = 0x04,          // 클라이언트 → 서버 (PC 정보 등록)
        HEARTBEAT = 0x05         // 양방향 (연결 상태 확인)
    };

    // =================================================================
    // 🆕 새로운 핵심 기능용 구조체들
    // =================================================================

    // PC 등록 상태 추적
    struct PCRegistrationStatus {
        QString pcId;                           // MAC 기반 PC ID
        bool isExistingPC;                      // 기존 PC 여부
        bool ownerIdVerified;                   // Owner_ID 검증 완료 여부
        QString ownerIdRequestId;               // Owner_ID 검증 요청 ID
        QDateTime registrationStartTime;        // 등록 프로세스 시작 시간
        QString currentStatus;                  // "pending", "verifying", "completed", "failed"
        QString errorMessage;                   // 오류 메시지

        PCRegistrationStatus() : isExistingPC(false), ownerIdVerified(false) {
            registrationStartTime = QDateTime::currentDateTime();
            currentStatus = "pending";
        }
    };

    // PC 정보 변경 감지 결과
    struct PCChangeDetectionResult {
        QString pcId;
        bool hasChanges;
        QStringList changedFields;              // "pc_name", "ip", "os" 등
        DatabaseManager::ClientChangeInfo changeDetails;
        BackendApi::ClientUpdateRequest updateRequest;

        PCChangeDetectionResult() : hasChanges(false) {}
    };

    // Task 완료 처리 상태
    struct TaskCompletionStatus {
        QString taskId;
        QString pcId;
        QString moduleType;
        bool forensicDataStored;                // forensic_info 테이블 저장 완료
        bool backendNotified;                   // 백엔드 알림 완료
        QString backendRequestId;               // 백엔드 요청 ID
        QDateTime completionStartTime;
        QString currentStatus;                  // "storing", "notifying", "completed", "failed"
        QString errorMessage;

        TaskCompletionStatus() : forensicDataStored(false), backendNotified(false) {
            completionStartTime = QDateTime::currentDateTime();
            currentStatus = "storing";
        }
    };

public:
    // =================================================================
    // 생성자 및 서버 제어
    // =================================================================

    explicit NetworkManager(const QString& address = "0.0.0.0", uint16_t port = 8443, QObject* parent = nullptr);
    virtual ~NetworkManager();

    // 서버 제어 (기존 인터페이스 유지)
    bool Init();
    bool Start();
    void Stop();

    // =================================================================
    // 🔧 의존성 주입 (DatabaseManager + BackendApiClient)
    // =================================================================

    void setDatabaseManager(DatabaseManager* dbManager);
    void setBackendApiClient(BackendApiClient* apiClient);

    DatabaseManager* getDatabaseManager() const { return databaseManager_; }
    BackendApiClient* getBackendApiClient() const { return backendApiClient_; }

    // =================================================================
    // 🔄 기존 호환 인터페이스 (ForensicServer, HttpApiHandler용)
    // =================================================================

    // 클라이언트 관리 (기존 인터페이스 유지)
    QList<ClientInfo> getConnectedClients() const;
    bool sendTaskToClient(const QString& clientId, const TaskRequest& task);
    QString getClientIdByPcId(const QString& pcId);
    bool isClientConnected(const QString& pcId);

    // Task ID 관리 (기존 인터페이스 유지)
    void setClientTaskId(const QString& clientId, const QString& taskId);
    QString getClientTaskId(const QString& clientId) const;
    void clearClientTaskId(const QString& clientId);

    // =================================================================
    // 🎯 새로운 4가지 핵심 기능 인터페이스
    // =================================================================

    // 1. PC 등록 프로세스
    PCRegistrationStatus startPCRegistration(const QString& clientId, const PCRegistrationInfo& pcInfo);
    bool isPCRegistrationComplete(const QString& pcId);
    PCRegistrationStatus getPCRegistrationStatus(const QString& pcId);
    void completePCRegistration(const QString& pcId);

    // 2. PC 정보 변경 감지
    PCChangeDetectionResult detectPCChanges(const QString& pcId, const PCRegistrationInfo& currentInfo);
    void processPCChanges(const PCChangeDetectionResult& changeResult);

    // 3. Task 처리 및 완료 알림
    TaskCompletionStatus startTaskCompletion(const QString& taskId, const QString& pcId,
                                             const QString& moduleType, const QJsonObject& forensicData);
    bool isTaskCompletionInProgress(const QString& taskId);
    TaskCompletionStatus getTaskCompletionStatus(const QString& taskId);
    // 🆕 목표 1: Task 완료 상태 관리 헬퍼 함수들
    bool isAllModulesCompleted(const QString& taskId);
    QStringList getMissingModuleTypes(const QString& taskId);
    void checkAndProcessTaskCompletion(const QString& taskId);
    void notifyTaskFailureForMissingModules(const QString& taskId, const QStringList& missingModules);


    // 4. 백엔드 API 연동 상태
    bool isBackendApiConfigured() const;
    bool isBackendAvailable() const;
    void configureBackendApi(const BackendApi::BackendConfig& config);
    void testBackendConnection();

    // =================================================================
    // 📊 상태 조회 및 진단
    // =================================================================

    struct NetworkManagerStatus {
        bool isRunning;
        bool isDatabaseConnected;
        bool isBackendApiConfigured;
        int connectedClientCount;
        int pendingRegistrations;
        int activeTaskCompletions;
        QDateTime lastActivity;
    };

    NetworkManagerStatus getStatus() const;
    void printDiagnostics() const;

signals:
    // =================================================================
    // 🔄 기존 호환 시그널들 (ForensicServer에서 사용)
    // =================================================================

    // 클라이언트 연결 관련 (기존)
    void clientConnected(const QString& clientId);
    void clientDisconnected(const QString& clientId);

    // 데이터 처리 관련 (기존)
    void forensicDataReceived(const QString& clientId, const ForensicData& data);

    // 작업 관리 관련 (기존)
    void taskCompleted(const QString& clientId, const QString& taskId);
    void taskFailed(const QString& clientId, const QString& taskId, const QString& error);

    // PC 등록 관련 (기존)
    void pcInfoReceived(const QString& clientId, const PCRegistrationInfo& pcInfo);
    void clientNeedsRegistration(const QString& clientId, const QString& ipAddress, const QString& hostname);

    // =================================================================
    // 🆕 새로운 4가지 핵심 기능 시그널들
    // =================================================================

    // 1. PC 등록 프로세스
    void pcRegistrationStarted(const QString& pcId, const PCRegistrationStatus& status);
    void pcRegistrationCompleted(const QString& pcId, bool success);
    void ownerIdVerificationNeeded(const QString& pcId, const BackendApi::VerifyOwnerRequest& request);
    void ownerIdVerificationResult(const QString& pcId, bool verified, const QString& ownerId);
    void verifyOwnerRequested(const BackendApi::VerifyOwnerRequest& request);

    // 2. PC 정보 변경 감지
    void pcChangesDetected(const QString& pcId, const PCChangeDetectionResult& changeResult);
    void pcChangesProcessed(const QString& pcId, bool success);

    // 3. Task 완료 알림
    void taskCompletionStarted(const QString& taskId, const TaskCompletionStatus& status);
    void taskCompletionFinished(const QString& taskId, bool success);
    void forensicDataStoredInDB(const QString& taskId, int forensicId);
    void taskCompletionNotifiedToBackend(const QString& taskId, bool success);

    // 4. 백엔드 API 연동
    void backendApiConfigured(bool success);
    void backendConnectionTested(bool available);
    void backendRequestSent(const QString& apiName, const QString& requestId);
    void backendResponseReceived(const QString& apiName, const QString& requestId, bool success);

private slots:
    // =================================================================
    // 🔄 기존 네트워크 처리 슬롯들
    // =================================================================
    void acceptClients();
    void handleClient(SOCKET clientSocket);

    // =================================================================
    // 🆕 새로운 4가지 핵심 기능 슬롯들
    // =================================================================

    // DatabaseManager 연동 슬롯들
    void onDatabaseConnected();
    void onDatabaseDisconnected();
    void onClientInfoChanged(const BackendApi::ClientUpdateRequest& updateRequest);
    void onTaskCompletionNotification(const BackendApi::TaskCompleteRequest& completeRequest);
    void onForensicDataStored(int forensicId, const QString& taskId);

    // BackendApiClient 연동 슬롯들
    void onOwnerVerificationResult(const BackendApi::VerifyOwnerRequest& request,
                                   const BackendApi::VerifyOwnerResponse& response,
                                   bool success);
    void onClientUpdateNotified(const BackendApi::ClientUpdateRequest& request,
                                const BackendApi::ClientUpdateResponse& response, bool success);
    void onTaskCompleteNotified(const BackendApi::TaskCompleteRequest& request,
                                const BackendApi::TaskCompleteResponse& response, bool success);
    void onBackendConnectionTested(bool available);


    // 1. PC 등록 프로세스 슬롯들
    void onPCRegistrationStarted(const QString& pcId, const PCRegistrationStatus& status);
    void onPCRegistrationCompleted(const QString& pcId, bool success);

    // 🆕 Owner_ID 검증 결과 처리 슬롯 (새로 추가)


private:
    // =================================================================
    // 🆕 PC 등록 대기 상태 관리 (새로 추가)
    // =================================================================
    QHash<QString, SOCKET> pendingRegistrations_;     // PC ID → Socket 매핑 (백엔드 응답 대기)
    mutable QMutex pendingMutex_;

    // =================================================================
    // 네트워크 설정 및 소켓 관리
    // =================================================================
    QString serverAddress_;
    uint16_t port_;
    SOCKET serverSocket_;
    sockaddr_in serverAddr_;

    // 클라이언트 관리
    QList<ClientInfo> connectedClients_;
    mutable QMutex clientsMutex_;
    QHash<SOCKET, QString> socketToClientId_;

    // Task ID 추적
    QHash<QString, QString> clientTaskIds_;
    mutable QMutex taskIdsMutex_;

    // 스레드 관리
    std::thread acceptThread_;
    std::vector<std::thread> clientThreads_;
    mutable QMutex clientThreadsMutex_;
    std::atomic<bool> isRunning_;

    // =================================================================
    // 🔧 의존성 (DatabaseManager + BackendApiClient)
    // =================================================================
    DatabaseManager* databaseManager_;
    BackendApiClient* backendApiClient_;

    // =================================================================
    // 🆕 4가지 핵심 기능별 상태 관리
    // =================================================================

    // 1. PC 등록 상태 추적
    QHash<QString, PCRegistrationStatus> pcRegistrationStatuses_;
    mutable QMutex registrationMutex_;

    // 2. PC 변경 감지 상태
    QHash<QString, PCChangeDetectionResult> pcChangeResults_;
    mutable QMutex changeDetectionMutex_;

    // 3. Task 완료 상태 추적
    QHash<QString, TaskCompletionStatus> taskCompletionStatuses_;
    mutable QMutex taskCompletionMutex_;

    // 🆕 목표 1: 6개 모듈 타입 상수 정의
    static const QStringList REQUIRED_MODULE_TYPES;
    static const int TOTAL_REQUIRED_MODULES = 6;

    // 4. 백엔드 API 상태
    bool backendApiConfigured_;
    bool backendApiAvailable_;
    mutable QMutex backendApiMutex_;

    // =================================================================
    // 📊 진단 및 통계
    // =================================================================
    QDateTime lastActivityTime_;
    mutable QMutex statusMutex_;

    // =================================================================
    // 🔧 내부 처리 메서드들
    // =================================================================

    // 네트워크 초기화
    bool initServerSocket();
    void cleanupClient(SOCKET clientSocket);
    bool sendBinaryMessageToSocket(SOCKET socket, BinaryMessageType messageType, const QByteArray& payload);

    // 메시지 처리
    void processForensicData(const ClientInfo& client, const QByteArray& data);
    QString handlePCInfoMessage(SOCKET clientSocket, const QByteArray& messageData);
    void updateConnectedClientsList(SOCKET clientSocket, const QString& pcId, const PCRegistrationInfo& pcInfo);

    // 🆕 PC_INFO 응답 전송 (새로 추가)
    bool sendPCInfoResponse(SOCKET clientSocket, bool success, bool needsOwnerID, const QString& message);

    // 유틸리티
    QString detectModuleType(const QJsonObject& jsonObj);
    PCRegistrationInfo parsePCInfoJson(const QJsonObject& jsonData);
    QString generateClientIdFromMAC(const QString& macAddress);

    QString extractMacFromPcId(const QString& pcId);

    // =================================================================
    // 🎯 4가지 핵심 기능 내부 처리 메서드들
    // =================================================================

    // 1. PC 등록 프로세스 내부 처리
    void processPCRegistration(const QString& pcId, const PCRegistrationInfo& pcInfo);
    void requestOwnerIdVerification(const QString& pcId);
    void finalizePCRegistration(const QString& pcId, bool success, const QString& ownerId = "");


    // 2. PC 정보 변경 감지 내부 처리
    void performChangeDetection(const QString& pcId, const PCRegistrationInfo& currentInfo);
    void notifyBackendOfChanges(const PCChangeDetectionResult& changeResult);

    // 3. Task 완료 알림 내부 처리
    void storeForensicDataInDB(const QString& taskId, const QString& pcId,
                               const QString& moduleType, const QJsonObject& forensicData);
    void notifyBackendOfTaskCompletion(const QString& taskId);
    void finalizeTaskCompletion(const QString& taskId, bool success);

    // 4. 백엔드 API 연동 내부 처리
    void setupBackendApiConnections();
    void updateBackendApiStatus(bool available);

    // 상태 업데이트
    void updateLastActivity();
    void logActivity(const QString& activity);
};

#endif // NETWORKMANAGER_H
