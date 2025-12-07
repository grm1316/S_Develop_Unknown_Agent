// BackendApiClient.h - 백엔드 API 호출 클라이언트
// 포렌식 서버에서 백엔드로 나가는 HTTP 요청 전담 클라이언트

#ifndef BACKEND_API_CLIENT_H
#define BACKEND_API_CLIENT_H

#include "pch.h"
#include "backend_types.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QMutex>
#include <QQueue>

// =================================================================
// BackendApiClient - 백엔드 API 전용 HTTP 클라이언트
// QNetworkAccessManager 기반 비동기 클라이언트
// 3개 백엔드 API 호출 전담: Owner 검증, PC 정보 변경, Task 완료
// =================================================================

class BackendApiClient : public QObject {
    Q_OBJECT

public:
    // =================================================================
    // 생성자/소멸자
    // =================================================================

    explicit BackendApiClient(QObject* parent = nullptr);
    explicit BackendApiClient(const BackendApi::BackendConfig& config, QObject* parent = nullptr);
    virtual ~BackendApiClient();

    // =================================================================
    // 설정 관리
    // =================================================================

    void setConfig(const BackendApi::BackendConfig& config);
    BackendApi::BackendConfig getConfig() const { return config_; }

    // 개별 설정 메서드들
    void setBaseUrl(const QString& baseUrl);
    void setTimeout(int timeoutMs);
    void setRetryCount(int retryCount);
    void setRetryDelay(int retryDelayMs);

    // 상태 조회
    bool isConfigValid() const;
    QString getLastError() const { return lastError_; }

    // =================================================================
    // 3개 백엔드 API 호출 메서드들
    // =================================================================

    // 1. Owner_ID 검증 API (POST /api/verify-owner)
    void verifyOwner(const BackendApi::VerifyOwnerRequest& request);

    // 2. PC 정보 변경 알림 API (POST /api/client-update)
    void notifyClientUpdate(const BackendApi::ClientUpdateRequest& request);

    // 3. Task 완료 알림 API (POST /api/task-complete)
    void notifyTaskComplete(const BackendApi::TaskCompleteRequest& request);

    // 🆕 4. Task 실패 알림 API (POST /api/internal/tasks/{task_id}/fail)
    void notifyTaskFailure(const QString& taskId, const QString& errorMessage);

    // =================================================================
    // 동기식 호출 메서드들 (필요시 사용)
    // =================================================================

    BackendApi::ApiResponse verifyOwnerSync(const BackendApi::VerifyOwnerRequest& request, int timeoutMs = 10000);
    BackendApi::ApiResponse notifyClientUpdateSync(const BackendApi::ClientUpdateRequest& request, int timeoutMs = 5000);
    BackendApi::ApiResponse notifyTaskCompleteSync(const BackendApi::TaskCompleteRequest& request, int timeoutMs = 5000);
    BackendApi::ApiResponse notifyTaskFailureSync(const QString& taskId, const QString& errorMessage, int timeoutMs = 5000);

    // =================================================================
    // 연결 테스트 및 상태 확인
    // =================================================================

    void testConnection();                          // 백엔드 연결 테스트
    bool isBackendAvailable() const;                // 백엔드 가용성 확인
    void checkBackendHealth();                      // 헬스 체크 API 호출

signals:
    // =================================================================
    // 비동기 응답 시그널들
    // =================================================================
    // Owner_ID 검증 응답
    void ownerVerificationResult(const BackendApi::VerifyOwnerRequest& request,
                                 const BackendApi::VerifyOwnerResponse& response,
                                 bool success);

    // PC 정보 변경 알림 응답
    void clientUpdateNotified(const BackendApi::ClientUpdateRequest& request,
                              const BackendApi::ClientUpdateResponse& response,
                              bool success);

    // Task 완료 알림 응답
    void taskCompleteNotified(const BackendApi::TaskCompleteRequest& request,
                              const BackendApi::TaskCompleteResponse& response,
                              bool success);

    //task 실패 알림 응답
    void taskFailureNotified(const QString& taskId, const QString& errorMessage, bool success);

    // 연결 상태 관련 시그널들
    void connectionTestResult(bool success, const QString& message);
    void backendHealthCheck(bool healthy, const QString& status);
    void backendAvailabilityChanged(bool available);

    // 에러 시그널들
    void networkError(BackendApi::BackendApiError errorType, const QString& message);
    void requestFailed(const QString& apiName, const BackendApi::ApiResponse& response);

private slots:
    // =================================================================
    // 네트워크 응답 처리 슬롯들
    // =================================================================

    void handleNetworkReply();                      // 공통 응답 처리
    void handleNetworkError(QNetworkReply::NetworkError error);  // 네트워크 에러 처리
    void handleRequestTimeout();                    // 타임아웃 처리
    void handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors);  // SSL 에러 처리

private:
    // =================================================================
    // 내부 구조체들
    // =================================================================

    // 진행 중인 요청 정보
    struct PendingRequest {
        QNetworkReply* reply;
        QString apiName;                    // "verify-owner", "client-update", "task-complete"
        QJsonObject requestData;            // 원본 요청 데이터
        QTimer* timeoutTimer;               // 타임아웃 타이머
        int retryAttempts;                  // 재시도 횟수
        QDateTime startTime;                // 요청 시작 시간

        PendingRequest() : reply(nullptr), timeoutTimer(nullptr), retryAttempts(0) {
            startTime = QDateTime::currentDateTime();
        }
    };

    // =================================================================
    // 내부 멤버 변수들
    // =================================================================

    QNetworkAccessManager* networkManager_;         // HTTP 클라이언트
    BackendApi::BackendConfig config_;              // 백엔드 설정

    QHash<QNetworkReply*, PendingRequest> pendingRequests_;  // 진행 중인 요청들
    mutable QMutex requestsMutex_;                  // 요청 관리용 뮤텍스

    QString lastError_;                             // 마지막 에러 메시지
    QDateTime lastSuccessTime_;                     // 마지막 성공 시간
    bool backendAvailable_;                         // 백엔드 가용성 플래그

    // 통계 정보
    struct Statistics {
        int totalRequests;
        int successfulRequests;
        int failedRequests;
        int retryCount;
        QDateTime startTime;

        Statistics() : totalRequests(0), successfulRequests(0), failedRequests(0), retryCount(0) {
            startTime = QDateTime::currentDateTime();
        }
    } stats_;

    // =================================================================
    // 내부 헬퍼 메서드들
    // =================================================================

    // HTTP 요청 생성 및 전송
    QNetworkReply* createHttpRequest(const QString& endpoint, const QJsonObject& requestData);
    QNetworkRequest prepareNetworkRequest(const QString& endpoint);
    void sendHttpRequest(const QString& apiName, const QString& endpoint, const QJsonObject& requestData);
    // 🆕 Python API용 헤더 포함 HTTP 요청 메서드
    void sendHttpRequestWithHeaders(const QString& requestId, const QString& endpoint, const QJsonObject& data);
        void sendHttpRequestPUT(const QString& apiName, const QString& endpoint, const QJsonObject& requestData);

    // 응답 처리
    void processApiResponse(const QString& apiName, QNetworkReply* reply, const QJsonObject& originalRequest);
    BackendApi::ApiResponse parseApiResponse(QNetworkReply* reply);
    void handleApiError(const QString& apiName, const BackendApi::ApiResponse& response, const QJsonObject& originalRequest);

    // 재시도 로직
    bool shouldRetryRequest(const PendingRequest& request, QNetworkReply::NetworkError error);
    void retryRequest(const PendingRequest& request);
    void scheduleRetry(const QString& apiName, const QString& endpoint, const QJsonObject& requestData, int delay);

    // 요청 관리
    void addPendingRequest(QNetworkReply* reply, const QString& apiName, const QJsonObject& requestData);
    void removePendingRequest(QNetworkReply* reply);
    void cleanupRequest(QNetworkReply* reply);
    void setupRequestTimeout(QNetworkReply* reply);

    // 상태 관리
    void updateBackendAvailability(bool available);
    void updateStatistics(bool success, bool retry = false);

    // 로깅 및 에러 처리
    void logRequest(const QString& apiName, const QJsonObject& requestData);
    void logResponse(const QString& apiName, const BackendApi::ApiResponse& response);
    void logError(const QString& operation, const QString& error);

    // 유틸리티
    QString formatRequestForLog(const QJsonObject& request);
    QString getNetworkErrorString(QNetworkReply::NetworkError error);
    BackendApi::BackendApiError mapNetworkError(QNetworkReply::NetworkError error);

public:
    // =================================================================
    // 통계 및 진단 정보
    // =================================================================

    struct DiagnosticInfo {
        bool isConfigured;
        bool isBackendAvailable;
        QString lastErrorMessage;
        QDateTime lastSuccessTime;
        int totalRequests;
        int successfulRequests;
        int failedRequests;
        double successRate;
        int pendingRequestsCount;
    };

    DiagnosticInfo getDiagnosticInfo() const;
    void printStatistics() const;                   // 통계 출력
    void resetStatistics();                         // 통계 초기화
};

#endif // BACKEND_API_CLIENT_H
