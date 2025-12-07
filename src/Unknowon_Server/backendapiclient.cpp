// BackendApiClient.cpp - 백엔드 API 클라이언트 완전한 구현
// QNetworkAccessManager 기반 HTTP 클라이언트

#include "BackendApiClient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QEventLoop>
#include <QSslConfiguration>
#include <QNetworkProxy>

// =================================================================
// 생성자/소멸자
// =================================================================

BackendApiClient::BackendApiClient(QObject* parent)
    : QObject(parent), networkManager_(nullptr), backendAvailable_(false) {

    // QNetworkAccessManager 초기화
    networkManager_ = new QNetworkAccessManager(this);

    // SSL 설정
    QSslConfiguration sslConfig = QSslConfiguration::defaultConfiguration();
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    QSslConfiguration::setDefaultConfiguration(sslConfig);

    // 기본 설정
    config_.baseUrl = "http://backend.unknwonlite.com";
    config_.timeoutMs = 30000;  // 10초 → 30초로 증가
    config_.retryCount = 3;
    config_.retryDelayMs = 1000;
    config_.apiKey = "adfawirovansdifuhaworgnkjsdfh2345h2woeg8w3rgakljshdf";
    config_.userAgent = "ForensicServer/1.0";

    // ⚠️ finished 시그널 연결 제거 (각 요청마다 개별 연결)
    // connect(networkManager_, &QNetworkAccessManager::finished,
    //          this, &BackendApiClient::handleNetworkReply);

    qDebug() << "[BackendApiClient] Initialized with default config";
    getConfig();
}

BackendApiClient::BackendApiClient(const BackendApi::BackendConfig& config, QObject* parent)
    : BackendApiClient(parent) {
    setConfig(config);
}

BackendApiClient::~BackendApiClient() {
    // 진행 중인 모든 요청 정리
    QMutexLocker locker(&requestsMutex_);
    for (auto it = pendingRequests_.begin(); it != pendingRequests_.end(); ++it) {
        cleanupRequest(it.key());
    }
    pendingRequests_.clear();

    qDebug() << "[BackendApiClient] Destroyed";
}

// =================================================================
// 설정 관리
// =================================================================

void BackendApiClient::setConfig(const BackendApi::BackendConfig& config) {
    config_ = config;

    if (!config_.isValid()) {
        qWarning() << "[BackendApiClient] Invalid config provided";
        lastError_ = "Invalid configuration";
        return;
    }

    qDebug() << "[BackendApiClient] Configuration updated:";
    qDebug() << "   Base URL:" << config_.baseUrl;
    qDebug() << "   Timeout:" << config_.timeoutMs << "ms";
    qDebug() << "   Retry count:" << config_.retryCount;
    qDebug() << "   Retry delay:" << config_.retryDelayMs << "ms";
}

void BackendApiClient::setBaseUrl(const QString& baseUrl) {
    config_.baseUrl = baseUrl;
}

void BackendApiClient::setTimeout(int timeoutMs) {
    config_.timeoutMs = timeoutMs;
}

void BackendApiClient::setRetryCount(int retryCount) {
    config_.retryCount = retryCount;
}

void BackendApiClient::setRetryDelay(int retryDelayMs) {
    config_.retryDelayMs = retryDelayMs;
}

bool BackendApiClient::isConfigValid() const {
    return config_.isValid();
}

// =================================================================
// 3개 백엔드 API 호출 메서드들 (비동기)
// =================================================================

void BackendApiClient::verifyOwner(const BackendApi::VerifyOwnerRequest& request) {
    if (!request.isValid()) {
        qWarning() << "[BackendApiClient] Invalid verify owner request";
        emit ownerVerificationResult(request, BackendApi::VerifyOwnerResponse(), false);
        return;
    }

    QJsonObject requestData = request.toJson();
    logRequest("verify-owner", requestData);

    // 🆕 엔드포인트 변경: api/verify-owner → api/internal/agent/pc
    sendHttpRequestWithHeaders("verify-owner", "api/internal/agent/pc", requestData);
}

void BackendApiClient::sendHttpRequestWithHeaders(const QString& requestId, const QString& endpoint, const QJsonObject& data) {
    if (!config_.isValid()) {
        qWarning() << "[BackendApiClient] Invalid configuration";
        return;
    }

    QString fullUrl = config_.getApiUrl(endpoint);
    QNetworkRequest request(fullUrl);

    // 헤더 설정
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Internal-Key", config_.apiKey.toUtf8());
    request.setRawHeader("User-Agent", config_.userAgent.toUtf8());

    // 전송 타임아웃 설정 (Qt 5.15+)
    request.setTransferTimeout(config_.timeoutMs);

    QJsonDocument doc(data);
    QByteArray postData = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[BackendApiClient] POST" << fullUrl << "with X-Internal-Key header";
    qDebug() << "[BackendApiClient] Body:" << QString::fromUtf8(postData);

    QNetworkReply* reply = networkManager_->post(request, postData);

    if (!reply) {
        qCritical() << "[BackendApiClient] Failed to create network reply";
        return;
    }

    // PendingRequest 생성 및 추가
    addPendingRequest(reply, requestId, data);

    // 타임아웃 설정
    setupRequestTimeout(reply);

    // finished 시그널을 직접 처리 (sender() 문제 해결)
    connect(reply, &QNetworkReply::finished, [this, reply, requestId]() {
        qDebug() << "[BackendApiClient] Reply finished for" << requestId;

        QMutexLocker locker(&requestsMutex_);

        // PendingRequest 찾기
        if (!pendingRequests_.contains(reply)) {
            qWarning() << "[BackendApiClient] Reply not found in pending requests";
            reply->deleteLater();
            return;
        }

        PendingRequest request = pendingRequests_.value(reply);

        // 타이머 정지
        if (request.timeoutTimer && request.timeoutTimer->isActive()) {
            request.timeoutTimer->stop();
            request.timeoutTimer->deleteLater();
        }

        // 요청 제거
        pendingRequests_.remove(reply);
        locker.unlock();

        // 응답 처리
        processApiResponse(requestId, reply, request.requestData);

        // reply 삭제
        reply->deleteLater();
    });

    // 에러 시그널 연결
    connect(reply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            [this, requestId](QNetworkReply::NetworkError error) {
                if (error != QNetworkReply::NoError && error != QNetworkReply::OperationCanceledError) {
                    qWarning() << "[BackendApiClient] Network error for" << requestId << ":" << error;
                }
            });

    updateStatistics(false);
    qDebug() << "[BackendApiClient] Request sent, waiting for response...";
}

void BackendApiClient::notifyClientUpdate(const BackendApi::ClientUpdateRequest& request) {
    if (!request.isValid()) {
        qWarning() << "[BackendApiClient] Invalid client update request";
        emit clientUpdateNotified(request, BackendApi::ClientUpdateResponse(), false);
        return;
    }

    QJsonObject requestData = request.toJson();
    logRequest("client-update", requestData);

    // 🆕 JSON에서 pc_id 추출해서 URL에 포함
    QString pcId = requestData["pc_id"].toString();
    if (pcId.isEmpty()) {
        qWarning() << "[BackendApiClient] Missing pc_id in client update request";
        emit clientUpdateNotified(request, BackendApi::ClientUpdateResponse(), false);
        return;
    }

    // 🆕 Python 서버 엔드포인트로 변경 + PUT 메서드
    QString endpoint = QString("api/internal/agent/pc/%1").arg(pcId);
    sendHttpRequestPUT("client-update", endpoint, requestData);
}

void BackendApiClient::notifyTaskComplete(const BackendApi::TaskCompleteRequest& request) {
    if (!request.isValid()) {
        qWarning() << "[BackendApiClient] Invalid task complete request";
        emit taskCompleteNotified(request, BackendApi::TaskCompleteResponse(), false);
        return;
    }

    QJsonObject requestData = request.toJson();
    logRequest("task-complete", requestData);

    // JSON에서 task_id 추출해서 URL에 포함
    QString taskId = requestData["task_id"].toString();
    if (taskId.isEmpty()) {
        qWarning() << "[BackendApiClient] Missing task_id in task complete request";
        emit taskCompleteNotified(request, BackendApi::TaskCompleteResponse(), false);
        return;
    }

    // Python 서버 엔드포인트로 변경 (POST 유지)
    QString endpoint = QString("api/internal/tasks/%1/complete").arg(taskId);

    // 변경: sendHttpRequest → sendHttpRequestWithHeaders
    sendHttpRequestWithHeaders("task-complete", endpoint, requestData);
}

void BackendApiClient::notifyTaskFailure(const QString& taskId, const QString& errorMessage) {
    if (taskId.isEmpty() || errorMessage.isEmpty()) {
        qWarning() << "[BackendApiClient] Invalid task failure request - missing taskId or errorMessage";
        emit taskFailureNotified(taskId, errorMessage, false);
        return;
    }

    QJsonObject requestData;
    requestData["error_message"] = errorMessage;

    logRequest("task-failure", requestData);

    // Python 서버 엔드포인트로 전송
    QString endpoint = QString("api/internal/tasks/%1/fail").arg(taskId);
    sendHttpRequestWithHeaders("task-failure", endpoint, requestData);
}

void BackendApiClient::sendHttpRequestPUT(const QString& apiName, const QString& endpoint, const QJsonObject& requestData) {
    if (!isConfigValid()) {
        qCritical() << "[BackendApiClient] Invalid configuration";
        return;
    }

    QString fullUrl = config_.getApiUrl(endpoint);
    QNetworkRequest request(fullUrl);

    // 헤더 설정 (X-Internal-Key 포함)
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("X-Internal-Key", config_.apiKey.toUtf8());
    request.setRawHeader("User-Agent", config_.userAgent.toUtf8());
    request.setTransferTimeout(config_.timeoutMs);

    QJsonDocument doc(requestData);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    qDebug() << "[BackendApiClient] PUT" << fullUrl << "with X-Internal-Key header";
    qDebug() << "[BackendApiClient] Body:" << QString::fromUtf8(jsonData);

    QNetworkReply* reply = networkManager_->put(request, jsonData);

    if (!reply) {
        qCritical() << "[BackendApiClient] Failed to create network reply";
        return;
    }

    // 기존 로직 유지
    addPendingRequest(reply, apiName, requestData);
    setupRequestTimeout(reply);

    // finished 시그널 직접 처리
    connect(reply, &QNetworkReply::finished, [this, reply, apiName, requestData]() {
        qDebug() << "[BackendApiClient] PUT Reply finished for" << apiName;

        QMutexLocker locker(&requestsMutex_);

        if (!pendingRequests_.contains(reply)) {
            qWarning() << "[BackendApiClient] Reply not found in pending requests";
            reply->deleteLater();
            return;
        }

        PendingRequest request = pendingRequests_.value(reply);

        // 타이머 정지
        if (request.timeoutTimer && request.timeoutTimer->isActive()) {
            request.timeoutTimer->stop();
            request.timeoutTimer->deleteLater();
        }

        pendingRequests_.remove(reply);
        locker.unlock();

        // 응답 처리
        processApiResponse(apiName, reply, requestData);

        reply->deleteLater();
    });

    updateStatistics(false);
    qDebug() << "[BackendApiClient] PUT request sent, waiting for response...";
}

// =================================================================
// 동기식 호출 메서드들
// =================================================================

BackendApi::ApiResponse BackendApiClient::verifyOwnerSync(const BackendApi::VerifyOwnerRequest& request, int timeoutMs) {
    if (!request.isValid()) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Invalid request";
        return response;
    }

    QJsonObject requestData = request.toJson();
    QNetworkReply* reply = createHttpRequest("api/verify-owner", requestData);

    if (!reply) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Failed to create HTTP request";
        return response;
    }

    // 동기식 대기
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(timeoutMs);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();

    BackendApi::ApiResponse response;
    if (timeoutTimer.isActive()) {
        timeoutTimer.stop();
        response = parseApiResponse(reply);
    } else {
        response.success = false;
        response.statusCode = 408;
        response.message = "Request timeout";
        reply->abort();
    }

    reply->deleteLater();
    return response;
}

BackendApi::ApiResponse BackendApiClient::notifyClientUpdateSync(const BackendApi::ClientUpdateRequest& request, int timeoutMs) {
    if (!request.isValid()) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Invalid request";
        return response;
    }

    QJsonObject requestData = request.toJson();
    QNetworkReply* reply = createHttpRequest("api/client-update", requestData);

    if (!reply) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Failed to create HTTP request";
        return response;
    }

    // 동기식 대기
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(timeoutMs);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();

    BackendApi::ApiResponse response;
    if (timeoutTimer.isActive()) {
        timeoutTimer.stop();
        response = parseApiResponse(reply);
    } else {
        response.success = false;
        response.statusCode = 408;
        response.message = "Request timeout";
        reply->abort();
    }

    reply->deleteLater();
    return response;
}

BackendApi::ApiResponse BackendApiClient::notifyTaskCompleteSync(const BackendApi::TaskCompleteRequest& request, int timeoutMs) {
    if (!request.isValid()) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Invalid request";
        return response;
    }

    QJsonObject requestData = request.toJson();
    QNetworkReply* reply = createHttpRequest("api/task-complete", requestData);

    if (!reply) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Failed to create HTTP request";
        return response;
    }

    // 동기식 대기
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(timeoutMs);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();

    BackendApi::ApiResponse response;
    if (timeoutTimer.isActive()) {
        timeoutTimer.stop();
        response = parseApiResponse(reply);
    } else {
        response.success = false;
        response.statusCode = 408;
        response.message = "Request timeout";
        reply->abort();
    }

    reply->deleteLater();
    return response;
}

BackendApi::ApiResponse BackendApiClient::notifyTaskFailureSync(const QString& taskId, const QString& errorMessage, int timeoutMs) {
    if (taskId.isEmpty() || errorMessage.isEmpty()) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Invalid request - missing taskId or errorMessage";
        return response;
    }

    QJsonObject requestData;
    requestData["error_message"] = errorMessage;

    QString endpoint = QString("api/internal/tasks/%1/fail").arg(taskId);
    QNetworkReply* reply = createHttpRequest(endpoint, requestData);

    if (!reply) {
        BackendApi::ApiResponse response;
        response.success = false;
        response.message = "Failed to create HTTP request";
        return response;
    }

    // 동기식 대기
    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.start(timeoutMs);

    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    loop.exec();

    BackendApi::ApiResponse response;
    if (timeoutTimer.isActive()) {
        timeoutTimer.stop();
        response = parseApiResponse(reply);
    } else {
        response.success = false;
        response.statusCode = 408;
        response.message = "Request timeout";
        reply->abort();
    }

    reply->deleteLater();
    return response;
}

// =================================================================
// 연결 테스트 및 상태 확인
// =================================================================

void BackendApiClient::testConnection() {
    QString healthEndpoint = config_.getApiUrl("api/internal/health");
    QNetworkRequest request(healthEndpoint);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, config_.userAgent);

    QNetworkReply* reply = networkManager_->get(request);
    setupRequestTimeout(reply);

    // 연결 테스트 전용 처리
    connect(reply, &QNetworkReply::finished, [this, reply]() {
        bool success = (reply->error() == QNetworkReply::NoError);
        QString message;

        if (success) {
            message = "Backend connection successful";
            updateBackendAvailability(true);
        } else {
            message = QString("Backend connection failed: %1").arg(reply->errorString());
            updateBackendAvailability(false);
        }

        qDebug() << "[BackendApiClient]" << message;
        emit connectionTestResult(success, message);

        reply->deleteLater();
    });
}

bool BackendApiClient::isBackendAvailable() const {
    return backendAvailable_;
}

void BackendApiClient::checkBackendHealth() {
    testConnection(); // health check는 connection test와 동일
}

// =================================================================
// 네트워크 응답 처리 슬롯들
// =================================================================

void BackendApiClient::handleNetworkReply() {
    qDebug() << "[BackendApiClient] ##### handleNetworkReply CALLED #####";

    QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
    if (!reply) {
        qWarning() << "[BackendApiClient] Reply is null!";
        return;
    }

    // HTTP 상태 먼저 확인
    int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray responseData = reply->readAll();

    qDebug() << "[BackendApiClient] HTTP Status:" << httpStatus;
    qDebug() << "[BackendApiClient] Response Body:" << QString::fromUtf8(responseData);
    qDebug() << "[BackendApiClient] Error Code:" << reply->error();

    QMutexLocker locker(&requestsMutex_);

    if (!pendingRequests_.contains(reply)) {
        qWarning() << "[BackendApiClient] Received reply for unknown request";
        reply->deleteLater();
        return;
    }

    PendingRequest request = pendingRequests_.value(reply);
    QString apiName = request.apiName;
    QJsonObject originalRequest = request.requestData;

    // 요청 정리
    removePendingRequest(reply);
    locker.unlock();

    // 응답 처리
    processApiResponse(apiName, reply, originalRequest);

    // 네트워크 에러 체크
    if (reply->error() != QNetworkReply::NoError) {
        handleNetworkError(reply->error());
    }

    reply->deleteLater();
}

void BackendApiClient::handleNetworkError(QNetworkReply::NetworkError error) {
    BackendApi::BackendApiError apiError = mapNetworkError(error);
    QString errorMessage = getNetworkErrorString(error);

    lastError_ = errorMessage;
    updateBackendAvailability(false);
    updateStatistics(false);

    logError("Network Error", errorMessage);
    emit networkError(apiError, errorMessage);
}

void BackendApiClient::handleRequestTimeout() {
    QTimer* timer = qobject_cast<QTimer*>(sender());
    if (!timer) return;

    // 타임아웃된 요청 찾기
    QMutexLocker locker(&requestsMutex_);
    for (auto it = pendingRequests_.begin(); it != pendingRequests_.end(); ++it) {
        if (it.value().timeoutTimer == timer) {
            QNetworkReply* reply = it.key();
            QString apiName = it.value().apiName;

            qWarning() << "[BackendApiClient] Request timeout:" << apiName;

            reply->abort();
            removePendingRequest(reply);

            lastError_ = "Request timeout";
            updateStatistics(false);

            emit networkError(BackendApi::BackendApiError::TimeoutError, "Request timeout");
            break;
        }
    }
}

void BackendApiClient::handleSslErrors(QNetworkReply* reply, const QList<QSslError>& errors) {
    if (!reply) return;

    for (const QSslError& error : errors) {
        qWarning() << "[BackendApiClient] SSL Error:" << error.errorString();
    }

    // 개발 환경에서는 SSL 에러 무시
    reply->ignoreSslErrors();
}

// =================================================================
// HTTP 요청 생성 및 전송
// =================================================================

QNetworkReply* BackendApiClient::createHttpRequest(const QString& endpoint, const QJsonObject& requestData) {
    if (!isConfigValid()) {
        qCritical() << "[BackendApiClient] Invalid configuration";
        return nullptr;
    }

    QNetworkRequest request = prepareNetworkRequest(endpoint);
    QJsonDocument doc(requestData);
    QByteArray jsonData = doc.toJson(QJsonDocument::Compact);

    QNetworkReply* reply = networkManager_->post(request, jsonData);
    return reply;
}

QNetworkRequest BackendApiClient::prepareNetworkRequest(const QString& endpoint) {
    QString url = config_.getApiUrl(endpoint);
    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setHeader(QNetworkRequest::UserAgentHeader, config_.userAgent);

    // ✅ X-Internal-Key로 변경 (Python 서버 요구사항)
    if (!config_.apiKey.isEmpty()) {
        request.setRawHeader("X-Internal-Key", config_.apiKey.toUtf8());
    }

    // Authorization 헤더는 필요시 유지
    if (!config_.authToken.isEmpty()) {
        request.setRawHeader("Authorization", QString("Bearer %1").arg(config_.authToken).toUtf8());
    }

    return request;
}

void BackendApiClient::sendHttpRequest(const QString& apiName, const QString& endpoint, const QJsonObject& requestData) {
    QNetworkReply* reply = createHttpRequest(endpoint, requestData);

    if (!reply) {
        qCritical() << "[BackendApiClient] Failed to create request for" << apiName;
        return;
    }

    // 요청 추가 및 타임아웃 설정
    addPendingRequest(reply, apiName, requestData);
    setupRequestTimeout(reply);

    updateStatistics(false); // 요청 카운트 증가

    qDebug() << "[BackendApiClient] Sent" << apiName << "request to" << config_.getApiUrl(endpoint);
}

// =================================================================
// 응답 처리
// =================================================================

void BackendApiClient::processApiResponse(const QString& apiName, QNetworkReply* reply, const QJsonObject& originalRequest) {
    BackendApi::ApiResponse response = parseApiResponse(reply);
    bool success = response.success && (reply->error() == QNetworkReply::NoError);

    logResponse(apiName, response);

    if (success) {
        updateBackendAvailability(true);
        updateStatistics(true);
        lastSuccessTime_ = QDateTime::currentDateTime();
    } else {
        updateStatistics(false);
        handleApiError(apiName, response, originalRequest);
    }

    // API별 응답 시그널 발생
    if (apiName == "verify-owner") {
        // originalRequest에서 요청 재구성
        BackendApi::VerifyOwnerRequest request;
        request.pcId = originalRequest.value("pc_id").toString();
        request.pcName = originalRequest.value("pc_name").toString();
        request.ip = originalRequest.value("ip").toString();
        request.os = originalRequest.value("os").toString();
        request.macAddress = originalRequest.value("mac_address").toString();
        request.ownerId = originalRequest.value("user_login_id").toString();

        BackendApi::VerifyOwnerResponse ownerResponse;
        if (success) {
            ownerResponse = BackendApi::VerifyOwnerResponse::fromJson(response.data);
        }
        emit ownerVerificationResult(request, ownerResponse, success);

    } else if (apiName == "client-update") {
        BackendApi::ClientUpdateRequest request;
        BackendApi::ClientUpdateResponse updateResponse;
        if (success) {
            updateResponse = BackendApi::ClientUpdateResponse::fromJson(response.data);
        }
        emit clientUpdateNotified(request, updateResponse, success);

    } else if (apiName == "task-complete") {
        BackendApi::TaskCompleteRequest request;
        BackendApi::TaskCompleteResponse completeResponse;
        if (success) {
            completeResponse = BackendApi::TaskCompleteResponse::fromJson(response.data);
        }
        emit taskCompleteNotified(request, completeResponse, success);
    } else if (apiName == "task-complete") {
        BackendApi::TaskCompleteRequest request;
        BackendApi::TaskCompleteResponse completeResponse;
        if (success) {
            completeResponse = BackendApi::TaskCompleteResponse::fromJson(response.data);
        }
        emit taskCompleteNotified(request, completeResponse, success);

    } else if (apiName == "task-failure") {
        // task-failure는 간단한 문자열 응답만 처리
        QString taskId = originalRequest["task_id"].toString();
        QString errorMessage = originalRequest["error_message"].toString();
        emit taskFailureNotified(taskId, errorMessage, success);
    }
}

BackendApi::ApiResponse BackendApiClient::parseApiResponse(QNetworkReply* reply) {
    BackendApi::ApiResponse response;
    response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    bool isHttpSuccess = (response.statusCode >= 200 && response.statusCode < 300);

    if (reply->error() != QNetworkReply::NoError) {
        response.success = false;
        response.message = reply->errorString();
        return response;
    }

    QByteArray data = reply->readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);

    // 응답 본문이 비어있거나 JSON이 아닌 경우
    if (data.isEmpty() || parseError.error != QJsonParseError::NoError) {
        response.success = isHttpSuccess; // HTTP 상태 코드로만 성공 여부 판단
        if (!isHttpSuccess) {
            response.message = QString("HTTP Error: %1").arg(response.statusCode);
        } else if (parseError.error != QJsonParseError::NoError && !data.isEmpty()) {
            response.message = "Response is not valid JSON.";
        } else {
            response.message = "Success with empty body";
        }
        return response;
    }

    // JSON 본문이 있는 경우
    QJsonObject jsonResponse = doc.object();
    response = BackendApi::ApiResponse::fromJson(jsonResponse); // 'success' 필드를 포함한 모든 필드 파싱
    response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); // statusCode는 HTTP 헤더가 더 정확하므로 덮어쓰기

    // 만약 JSON에 'success' 필드가 없다면, HTTP 상태 코드를 기준으로 성공 여부 결정
    if (!jsonResponse.contains("success")) {
        response.success = isHttpSuccess;
    }

    return response;
}

void BackendApiClient::handleApiError(const QString& apiName, const BackendApi::ApiResponse& response, const QJsonObject& originalRequest) {
    qWarning() << "[BackendApiClient]" << apiName << "API error:" << response.message;
    emit requestFailed(apiName, response);
}

// =================================================================
// 재시도 로직
// =================================================================

bool BackendApiClient::shouldRetryRequest(const PendingRequest& request, QNetworkReply::NetworkError error) {
    // 재시도 횟수 확인
    if (request.retryAttempts >= config_.retryCount) {
        return false;
    }

    // 재시도 가능한 에러인지 확인
    switch (error) {
    case QNetworkReply::TimeoutError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::TemporaryNetworkFailureError:
        return true;
    default:
        return false;
    }
}

void BackendApiClient::scheduleRetry(const QString& apiName, const QString& endpoint, const QJsonObject& requestData, int delay) {
    QTimer::singleShot(delay, this, [this, apiName, endpoint, requestData]() {
        qDebug() << "[BackendApiClient] Retrying" << apiName << "request";
        sendHttpRequest(apiName, endpoint, requestData);
        updateStatistics(false, true); // 재시도 카운트
    });
}

// =================================================================
// 요청 관리
// =================================================================

void BackendApiClient::addPendingRequest(QNetworkReply* reply, const QString& apiName, const QJsonObject& requestData) {
    QMutexLocker locker(&requestsMutex_);

    PendingRequest request;
    request.reply = reply;
    request.apiName = apiName;
    request.requestData = requestData;
    request.retryAttempts = 0;
    request.startTime = QDateTime::currentDateTime();

    pendingRequests_[reply] = request;
}

void BackendApiClient::removePendingRequest(QNetworkReply* reply) {
    if (pendingRequests_.contains(reply)) {
        PendingRequest request = pendingRequests_.take(reply);
        if (request.timeoutTimer) {
            request.timeoutTimer->stop();
            request.timeoutTimer->deleteLater();
        }
    }
}

void BackendApiClient::cleanupRequest(QNetworkReply* reply) {
    removePendingRequest(reply);
    if (reply && !reply->isFinished()) {
        reply->abort();
    }
}

void BackendApiClient::setupRequestTimeout(QNetworkReply* reply) {
    QMutexLocker locker(&requestsMutex_);

    if (!pendingRequests_.contains(reply)) {
        return;
    }

    QTimer* timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    timeoutTimer->setInterval(config_.timeoutMs);  // 30초 사용

    // 람다 함수로 직접 타임아웃 처리 (reply 캡처)
    connect(timeoutTimer, &QTimer::timeout, [this, reply, timeoutTimer]() {
        qWarning() << "[BackendApiClient] Request timeout for reply:" << reply;

        QMutexLocker locker(&requestsMutex_);
        if (pendingRequests_.contains(reply)) {
            PendingRequest request = pendingRequests_.value(reply);
            QString apiName = request.apiName;

            qWarning() << "[BackendApiClient] Request timeout:" << apiName;
            emit networkError(BackendApi::BackendApiError::TimeoutError,
                              QString("Request timeout: %1").arg(apiName));

            // 요청 정리
            removePendingRequest(reply);
            reply->abort();
            reply->deleteLater();

            updateStatistics(false);
        }
        timeoutTimer->deleteLater();
    });

    pendingRequests_[reply].timeoutTimer = timeoutTimer;
    timeoutTimer->start();
}

// =================================================================
// 상태 관리
// =================================================================

void BackendApiClient::updateBackendAvailability(bool available) {
    if (backendAvailable_ != available) {
        backendAvailable_ = available;
        qDebug() << "[BackendApiClient] Backend availability changed:" << (available ? "Available" : "Unavailable");
        emit backendAvailabilityChanged(available);
    }
}

void BackendApiClient::updateStatistics(bool success, bool retry) {
    stats_.totalRequests++;

    if (success) {
        stats_.successfulRequests++;
    } else {
        stats_.failedRequests++;
    }

    if (retry) {
        stats_.retryCount++;
    }
}

// =================================================================
// 로깅 및 에러 처리
// =================================================================

void BackendApiClient::logRequest(const QString& apiName, const QJsonObject& requestData) {
    qDebug() << BackendApi::requestToLogString(requestData, apiName);
}

void BackendApiClient::logResponse(const QString& apiName, const BackendApi::ApiResponse& response) {
    qDebug() << BackendApi::responseToLogString(response, apiName);
}

void BackendApiClient::logError(const QString& operation, const QString& error) {
    qWarning() << "[BackendApiClient]" << operation << ":" << error;
}

// =================================================================
// 유틸리티
// =================================================================

QString BackendApiClient::formatRequestForLog(const QJsonObject& request) {
    QJsonDocument doc(request);
    return doc.toJson(QJsonDocument::Compact);
}

QString BackendApiClient::getNetworkErrorString(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::NoError: return "No error";
    case QNetworkReply::ConnectionRefusedError: return "Connection refused";
    case QNetworkReply::RemoteHostClosedError: return "Remote host closed connection";
    case QNetworkReply::HostNotFoundError: return "Host not found";
    case QNetworkReply::TimeoutError: return "Request timeout";
    case QNetworkReply::OperationCanceledError: return "Operation canceled";
    case QNetworkReply::SslHandshakeFailedError: return "SSL handshake failed";
    case QNetworkReply::TemporaryNetworkFailureError: return "Temporary network failure";
    case QNetworkReply::NetworkSessionFailedError: return "Network session failed";
    case QNetworkReply::BackgroundRequestNotAllowedError: return "Background request not allowed";
    case QNetworkReply::ProxyConnectionRefusedError: return "Proxy connection refused";
    case QNetworkReply::ProxyConnectionClosedError: return "Proxy connection closed";
    case QNetworkReply::ProxyNotFoundError: return "Proxy not found";
    case QNetworkReply::ProxyTimeoutError: return "Proxy timeout";
    case QNetworkReply::ProxyAuthenticationRequiredError: return "Proxy authentication required";
    case QNetworkReply::ContentAccessDenied: return "Content access denied";
    case QNetworkReply::ContentOperationNotPermittedError: return "Content operation not permitted";
    case QNetworkReply::ContentNotFoundError: return "Content not found";
    case QNetworkReply::AuthenticationRequiredError: return "Authentication required";
    case QNetworkReply::ContentReSendError: return "Content re-send error";
    case QNetworkReply::ProtocolUnknownError: return "Unknown protocol";
    case QNetworkReply::ProtocolInvalidOperationError: return "Invalid protocol operation";
    case QNetworkReply::UnknownNetworkError: return "Unknown network error";
    case QNetworkReply::UnknownProxyError: return "Unknown proxy error";
    case QNetworkReply::UnknownContentError: return "Unknown content error";
    case QNetworkReply::ProtocolFailure: return "Protocol failure";
    default: return QString("Unknown error (%1)").arg(static_cast<int>(error));
    }
}

BackendApi::BackendApiError BackendApiClient::mapNetworkError(QNetworkReply::NetworkError error) {
    switch (error) {
    case QNetworkReply::NoError:
        return BackendApi::BackendApiError::NoError;
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TemporaryNetworkFailureError:
        return BackendApi::BackendApiError::NetworkError;
    case QNetworkReply::TimeoutError:
        return BackendApi::BackendApiError::TimeoutError;
    case QNetworkReply::AuthenticationRequiredError:
        return BackendApi::BackendApiError::AuthenticationError;
    case QNetworkReply::ContentNotFoundError:
    case QNetworkReply::ContentAccessDenied:
        return BackendApi::BackendApiError::HttpError;
    default:
        return BackendApi::BackendApiError::UnknownError;
    }
}

// =================================================================
// 통계 및 진단 정보
// =================================================================

BackendApiClient::DiagnosticInfo BackendApiClient::getDiagnosticInfo() const {
    DiagnosticInfo info;
    info.isConfigured = isConfigValid();
    info.isBackendAvailable = backendAvailable_;
    info.lastErrorMessage = lastError_;
    info.lastSuccessTime = lastSuccessTime_;
    info.totalRequests = stats_.totalRequests;
    info.successfulRequests = stats_.successfulRequests;
    info.failedRequests = stats_.failedRequests;
    info.successRate = (stats_.totalRequests > 0) ?
                           (static_cast<double>(stats_.successfulRequests) / stats_.totalRequests * 100.0) : 0.0;
    info.pendingRequestsCount = pendingRequests_.size();

    return info;
}

void BackendApiClient::printStatistics() const {
    qDebug() << "[BackendApiClient] Statistics:";
    qDebug() << "   Total requests:" << stats_.totalRequests;
    qDebug() << "   Successful:" << stats_.successfulRequests;
    qDebug() << "   Failed:" << stats_.failedRequests;
    qDebug() << "   Retries:" << stats_.retryCount;
    qDebug() << "   Success rate:" << QString::number(getDiagnosticInfo().successRate, 'f', 1) << "%";
    qDebug() << "   Backend available:" << (backendAvailable_ ? "Yes" : "No");
    qDebug() << "   Pending requests:" << pendingRequests_.size();
}

void BackendApiClient::resetStatistics() {
    stats_ = Statistics();
    qDebug() << "[BackendApiClient] Statistics reset";
}

