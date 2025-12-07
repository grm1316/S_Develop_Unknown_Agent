// HttpApiHandler.h - 새로운 NetworkManager와 완전 연동 (완성 버전)
#ifndef HTTPAPI_HANDLER_H
#define HTTPAPI_HANDLER_H

#include "pch.h"
#include "NetworkManager.h"
#include "DatabaseManager.h"
#include "backend_types.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QProcess>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QHash>
#include <QMutex>

// =================================================================
// HttpApiHandler - 새로운 NetworkManager와 완전 연동
// 기존 HTTP 엔드포인트 + 4가지 핵심 기능 API 엔드포인트
// Python API 서버와 C++ ForensicServer 연동
// =================================================================

class HttpApiHandler : public QObject {
    Q_OBJECT

public:
    // =================================================================
    // 생성자 및 소멸자
    // =================================================================
    explicit HttpApiHandler(NetworkManager* networkManager, QObject* parent = nullptr);
    virtual ~HttpApiHandler();

    // =================================================================
    // 의존성 설정
    // =================================================================
    void setDatabaseManager(DatabaseManager* databaseManager);

    // =================================================================
    // HTTP 서버 제어
    // =================================================================
    bool startServer(const QString& host = "localhost", uint16_t port = 8080);
    void stopServer();
    bool isRunning() const { return isRunning_; }

    // 향상된 서버 시작 메서드들
    bool startServerWithFallback(const QString& host = "localhost", uint16_t preferredPort = 8080);
    uint16_t findAvailablePort(const QString& host = "localhost", uint16_t startPort = 8080, uint16_t endPort = 8090);

    // 네트워크 진단 메서드들
    QString checkPortUsage(uint16_t port);
    void diagnoseNetworkIssues();

    // 설정
    void setPort(uint16_t port) { httpPort_ = port; }
    uint16_t getPort() const { return httpPort_; }

private slots:
    // =================================================================
    // HTTP 연결 처리
    // =================================================================
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();

    // =================================================================
    // 🔄 기존 DatabaseManager 시그널들 (기존 호환성)
    // =================================================================
    void onForensicDataStored(int forensicId, const QString& taskId);

    // =================================================================
    // 🆕 새로운 NetworkManager 4가지 핵심 기능 시그널들
    // =================================================================

    // 1. PC 등록 프로세스
    void onPCRegistrationStarted(const QString& pcId, const NetworkManager::PCRegistrationStatus& status);
    void onPCRegistrationCompleted(const QString& pcId, bool success);
    void onOwnerIdVerificationNeeded(const QString& pcId, const BackendApi::VerifyOwnerRequest& request);
    void onOwnerIdVerificationResult(const QString& pcId, bool verified, const QString& ownerId);

    // 2. PC 정보 변경 감지
    void onPCChangesDetected(const QString& pcId, const NetworkManager::PCChangeDetectionResult& changeResult);
    void onPCChangesProcessed(const QString& pcId, bool success);

    // 3. Task 완료 알림
    void onTaskCompletionStarted(const QString& taskId, const NetworkManager::TaskCompletionStatus& status);
    void onTaskCompletionFinished(const QString& taskId, bool success);
    void onForensicDataStoredInDB(const QString& taskId, int forensicId);
    void onTaskCompletionNotifiedToBackend(const QString& taskId, bool success);

    // 4. 백엔드 API 연동
    void onBackendApiConfigured(bool success);
    void onBackendConnectionTested(bool available);
    void onBackendRequestSent(const QString& apiName, const QString& requestId);
    void onBackendResponseReceived(const QString& apiName, const QString& requestId, bool success);

private:
    // =================================================================
    // 멤버 변수
    // =================================================================
    NetworkManager* networkManager_;
    DatabaseManager* databaseManager_;
    QTcpServer* httpServer_;
    bool isRunning_;
    QString httpHost_;
    uint16_t httpPort_;

    // =================================================================
    // 🔄 기존 작업 상태 관리 (기존 호환성)
    // =================================================================
    QHash<QString, QString> taskStatuses_;  // taskId -> status ("sent", "processing", "completed", "failed")
    mutable QMutex taskStatusMutex_;

    QHash<QTcpSocket*, QByteArray> requestBuffers_;
    mutable QMutex requestBufferMutex_;

    // =================================================================
    // 🆕 새로운 4가지 핵심 기능 상태 관리
    // =================================================================
    QHash<QString, NetworkManager::PCRegistrationStatus> pcRegistrationStatuses_;
    QHash<QString, NetworkManager::PCChangeDetectionResult> pcChangeResults_;
    QHash<QString, NetworkManager::TaskCompletionStatus> taskCompletionStatuses_;
    QHash<QString, bool> backendRequestStatuses_;
    mutable QMutex coreFeaturesMutex_;

    // =================================================================
    // 🔗 내부 초기화 메서드
    // =================================================================
    void setupNetworkManagerConnections();

    // =================================================================
    // HTTP 요청 처리 (코어)
    // =================================================================
    void handleHttpRequest(QTcpSocket* client, const QString& request);
    void sendHttpResponse(QTcpSocket* client, int statusCode,
                          const QString& contentType, const QByteArray& body, bool cors = true);

    // =================================================================
    // 🔄 기존 API 엔드포인트 핸들러들 (기존 호환성)
    // =================================================================
    void handleInspectRequest(QTcpSocket* client, const QJsonObject& requestData);
    void handleHealthCheck(QTcpSocket* client);
    void handleTaskStatus(QTcpSocket* client, const QString& taskId);
    void handlePCsList(QTcpSocket* client);

    // =================================================================
    // 🆕 새로운 4가지 핵심 기능 API 엔드포인트 핸들러들
    // =================================================================

    // 1. PC 등록 프로세스 API
    void handlePCRegistrationStatus(QTcpSocket* client, const QString& pcId);
    void handlePCRegistrationList(QTcpSocket* client);

    // 2. PC 정보 변경 감지 API
    void handlePCChangeStatus(QTcpSocket* client, const QString& pcId);
    void handlePCChangeHistory(QTcpSocket* client, const QString& pcId);

    // 3. Task 완료 알림 API
    void handleTaskCompletionStatus(QTcpSocket* client, const QString& taskId);
    void handleTaskCompletionList(QTcpSocket* client);

    // 4. 백엔드 API 연동 API
    void handleBackendStatus(QTcpSocket* client);
    void handleBackendRequestHistory(QTcpSocket* client);

    // 통합 상태 조회 API
    void handleNetworkManagerStatus(QTcpSocket* client);
    void handleSystemStatus(QTcpSocket* client);

    // =================================================================
    // 🔄 기존 작업 상태 관리 메서드들 (기존 호환성)
    // =================================================================
    void setTaskStatus(const QString& taskId, const QString& status);
    QString getTaskStatus(const QString& taskId) const;

    // =================================================================
    // 🆕 새로운 상태 관리 메서드들
    // =================================================================
    void updatePCRegistrationStatus(const QString& pcId, const NetworkManager::PCRegistrationStatus& status);
    void updatePCChangeResult(const QString& pcId, const NetworkManager::PCChangeDetectionResult& result);
    void updateTaskCompletionStatus(const QString& taskId, const NetworkManager::TaskCompletionStatus& status);
    void updateBackendRequestStatus(const QString& requestId, bool success);

    // =================================================================
    // 유틸리티 메서드들
    // =================================================================

    // HTTP 파싱
    QJsonObject parseJsonFromRequest(const QString& request);
    QString extractPathFromRequest(const QString& request);
    QString extractMethodFromRequest(const QString& request);

    // 응답 생성
    QString createSuccessResponse(const QJsonObject& data);
    QString createErrorResponse(const QString& error, int code = 400);
    QString createListResponse(const QJsonArray& items, int totalCount = -1);
    QString createStatusResponse(const QString& status, const QString& message = "");

    // =================================================================
    // 🆕 JSON 변환 헬퍼들
    // =================================================================
    QJsonObject pcRegistrationStatusToJson(const NetworkManager::PCRegistrationStatus& status);
    QJsonObject pcChangeResultToJson(const NetworkManager::PCChangeDetectionResult& result);
    QJsonObject taskCompletionStatusToJson(const NetworkManager::TaskCompletionStatus& status);
    QJsonObject networkManagerStatusToJson();
    QJsonObject systemStatusToJson();

    // 리스트 변환
    QJsonArray pcRegistrationListToJson();
    QJsonArray pcChangeHistoryToJson(const QString& pcId);
    QJsonArray taskCompletionListToJson();
    QJsonArray backendRequestHistoryToJson();

    // =================================================================
    // 🔧 진단 및 모니터링
    // =================================================================
    void logHttpRequest(const QString& method, const QString& path, const QString& clientIp = "");
    void logHttpResponse(int statusCode, const QString& path);
    void logError(const QString& error, const QString& context = "");

    // 성능 모니터링
    void updateRequestMetrics(const QString& endpoint, qint64 responseTimeMs);
    QJsonObject getApiMetrics();

    // =================================================================
    // 🔧 보안 및 검증
    // =================================================================
    bool validateApiRequest(const QString& path, const QString& method);
    bool isAuthorizedRequest(const QString& clientIp, const QString& endpoint);
    QString sanitizeInput(const QString& input);

    // =================================================================
    // 🔧 캐싱 (선택적)
    // =================================================================
    struct CacheEntry {
        QJsonObject data;
        QDateTime timestamp;
        int ttlSeconds;
    };

    QHash<QString, CacheEntry> responseCache_;
    mutable QMutex cacheMutex_;

    bool getCachedResponse(const QString& cacheKey, QJsonObject& data);
    void setCachedResponse(const QString& cacheKey, const QJsonObject& data, int ttlSeconds = 300);
    void clearExpiredCache();

    // =================================================================
    // 🔧 요청 통계 및 메트릭스
    // =================================================================
    struct RequestMetrics {
        QString endpoint;
        int totalRequests;
        int successfulRequests;
        int failedRequests;
        qint64 totalResponseTime;
        qint64 averageResponseTime;
        QDateTime lastRequestTime;

        RequestMetrics() : totalRequests(0), successfulRequests(0),
            failedRequests(0), totalResponseTime(0), averageResponseTime(0) {}
    };

    QHash<QString, RequestMetrics> requestMetrics_;
    mutable QMutex metricsMutex_;

    void initializeRequestMetrics();
    RequestMetrics getEndpointMetrics(const QString& endpoint);
    QJsonObject getAllMetrics();
};

#endif // HTTPAPI_HANDLER_H
