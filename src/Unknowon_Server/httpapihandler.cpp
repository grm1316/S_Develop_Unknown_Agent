// HttpApiHandler.cpp - 새로운 NetworkManager와 완전 연동
#include "HttpApiHandler.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QDateTime>
#include <QProcess>
#include <QNetworkInterface>

HttpApiHandler::HttpApiHandler(NetworkManager* networkManager, QObject* parent)
    : QObject(parent)
    , networkManager_(networkManager)
    , databaseManager_(nullptr)
    , httpServer_(nullptr)
    , isRunning_(false)
    , httpHost_("localhost")
    , httpPort_(8080) {

    qDebug() << "[HttpApiHandler] 새로운 NetworkManager와 함께 초기화 중...";

    // 🔗 새로운 NetworkManager 시그널들과 연결
    if (networkManager_) {
        setupNetworkManagerConnections();
    } else {
        qWarning() << "[HttpApiHandler] NetworkManager가 null입니다";
    }
}

HttpApiHandler::~HttpApiHandler() {
    stopServer();
    qDebug() << "[HttpApiHandler] HttpApiHandler 소멸됨";
}

// =================================================================
// 🔗 새로운 NetworkManager 시그널 연결 설정
// =================================================================

void HttpApiHandler::setupNetworkManagerConnections() {
    if (!networkManager_) return;

    qDebug() << "[HttpApiHandler] 새로운 NetworkManager 시그널 연결 중...";

    // 🆕 1. PC 등록 프로세스 시그널들
    connect(networkManager_, &NetworkManager::pcRegistrationStarted,
            this, &HttpApiHandler::onPCRegistrationStarted);
    connect(networkManager_, &NetworkManager::pcRegistrationCompleted,
            this, &HttpApiHandler::onPCRegistrationCompleted);
    connect(networkManager_, &NetworkManager::ownerIdVerificationNeeded,
            this, &HttpApiHandler::onOwnerIdVerificationNeeded);
    connect(networkManager_, &NetworkManager::ownerIdVerificationResult,
            this, &HttpApiHandler::onOwnerIdVerificationResult);

    // 🆕 2. PC 정보 변경 감지 시그널들
    connect(networkManager_, &NetworkManager::pcChangesDetected,
            this, &HttpApiHandler::onPCChangesDetected);
    connect(networkManager_, &NetworkManager::pcChangesProcessed,
            this, &HttpApiHandler::onPCChangesProcessed);

    // 🆕 3. Task 완료 알림 시그널들
    connect(networkManager_, &NetworkManager::taskCompletionStarted,
            this, &HttpApiHandler::onTaskCompletionStarted);
    connect(networkManager_, &NetworkManager::taskCompletionFinished,
            this, &HttpApiHandler::onTaskCompletionFinished);
    connect(networkManager_, &NetworkManager::forensicDataStoredInDB,
            this, &HttpApiHandler::onForensicDataStoredInDB);
    connect(networkManager_, &NetworkManager::taskCompletionNotifiedToBackend,
            this, &HttpApiHandler::onTaskCompletionNotifiedToBackend);

    // 🆕 4. 백엔드 API 연동 시그널들
    connect(networkManager_, &NetworkManager::backendApiConfigured,
            this, &HttpApiHandler::onBackendApiConfigured);
    connect(networkManager_, &NetworkManager::backendConnectionTested,
            this, &HttpApiHandler::onBackendConnectionTested);
    connect(networkManager_, &NetworkManager::backendRequestSent,
            this, &HttpApiHandler::onBackendRequestSent);
    connect(networkManager_, &NetworkManager::backendResponseReceived,
            this, &HttpApiHandler::onBackendResponseReceived);

    qDebug() << "[HttpApiHandler] 새로운 NetworkManager 시그널 연결 완료";
}

void HttpApiHandler::setDatabaseManager(DatabaseManager* databaseManager) {
    databaseManager_ = databaseManager;

    if (databaseManager_) {
        connect(databaseManager_, &DatabaseManager::forensicDataStored,
                this, &HttpApiHandler::onForensicDataStored);
        qDebug() << "[HttpApiHandler] DatabaseManager 시그널 연결 완료";
    } else {
        qWarning() << "[HttpApiHandler] DatabaseManager가 nullptr입니다";
    }
}

// =================================================================
// 🆕 새로운 4가지 핵심 기능 시그널 슬롯들
// =================================================================

// 1. PC 등록 프로세스 슬롯들
void HttpApiHandler::onPCRegistrationStarted(const QString& pcId,
                                             const NetworkManager::PCRegistrationStatus& status) {
    qDebug() << QString("[HttpApiHandler] PC 등록 시작: %1").arg(pcId);
    updatePCRegistrationStatus(pcId, status);
}

void HttpApiHandler::onPCRegistrationCompleted(const QString& pcId, bool success) {
    qDebug() << QString("[HttpApiHandler] PC 등록 완료: %1, 성공: %2").arg(pcId).arg(success);

    // 상태 업데이트
    QMutexLocker locker(&coreFeaturesMutex_);
    if (pcRegistrationStatuses_.contains(pcId)) {
        pcRegistrationStatuses_[pcId].currentStatus = success ? "completed" : "failed";
    }
}

void HttpApiHandler::onOwnerIdVerificationNeeded(const QString& pcId,
                                                 const BackendApi::VerifyOwnerRequest& request) {
    Q_UNUSED(request)
    qDebug() << QString("[HttpApiHandler] Owner_ID 검증 필요: %1").arg(pcId);
}

void HttpApiHandler::onOwnerIdVerificationResult(const QString& pcId, bool verified, const QString& ownerId) {
    qDebug() << QString("[HttpApiHandler] Owner_ID 검증 결과: %1, 검증됨: %2").arg(pcId).arg(verified);
    Q_UNUSED(ownerId)
}

// 2. PC 정보 변경 감지 슬롯들
void HttpApiHandler::onPCChangesDetected(const QString& pcId,
                                         const NetworkManager::PCChangeDetectionResult& changeResult) {
    qDebug() << QString("[HttpApiHandler] PC 변경 감지: %1, 변경 필드: %2")
                    .arg(pcId).arg(changeResult.changedFields.join(", "));
    updatePCChangeResult(pcId, changeResult);
}

void HttpApiHandler::onPCChangesProcessed(const QString& pcId, bool success) {
    qDebug() << QString("[HttpApiHandler] PC 변경 처리 완료: %1, 성공: %2").arg(pcId).arg(success);
}

// 3. Task 완료 알림 슬롯들
void HttpApiHandler::onTaskCompletionStarted(const QString& taskId,
                                             const NetworkManager::TaskCompletionStatus& status) {
    qDebug() << QString("[HttpApiHandler] Task 완료 처리 시작: %1").arg(taskId);
    updateTaskCompletionStatus(taskId, status);

    // 🔄 기존 호환성: taskStatuses_도 업데이트
    setTaskStatus(taskId, "processing");
}

void HttpApiHandler::onTaskCompletionFinished(const QString& taskId, bool success) {
    qDebug() << QString("[HttpApiHandler] Task 완료 처리 완료: %1, 성공: %2").arg(taskId).arg(success);

    // 🔄 기존 호환성: taskStatuses_ 업데이트
    setTaskStatus(taskId, success ? "completed" : "failed");
}

void HttpApiHandler::onForensicDataStoredInDB(const QString& taskId, int forensicId) {
    qDebug() << QString("[HttpApiHandler] 포렌식 데이터 DB 저장: Task %1, ID %2")
                    .arg(taskId).arg(forensicId);
}

void HttpApiHandler::onTaskCompletionNotifiedToBackend(const QString& taskId, bool success) {
    qDebug() << QString("[HttpApiHandler] 백엔드 알림 완료: %1, 성공: %2").arg(taskId).arg(success);
}

// 4. 백엔드 API 연동 슬롯들
void HttpApiHandler::onBackendApiConfigured(bool success) {
    qDebug() << QString("[HttpApiHandler] 백엔드 API 설정: %1").arg(success ? "성공" : "실패");
}

void HttpApiHandler::onBackendConnectionTested(bool available) {
    qDebug() << QString("[HttpApiHandler] 백엔드 연결 테스트: %1")
                    .arg(available ? "사용 가능" : "사용 불가");
}

void HttpApiHandler::onBackendRequestSent(const QString& apiName, const QString& requestId) {
    qDebug() << QString("[HttpApiHandler] 백엔드 요청 전송: %1, ID: %2").arg(apiName, requestId);
    updateBackendRequestStatus(requestId, false); // 전송됨, 아직 응답 안됨
}

void HttpApiHandler::onBackendResponseReceived(const QString& apiName, const QString& requestId, bool success) {
    qDebug() << QString("[HttpApiHandler] 백엔드 응답 수신: %1, ID: %2, 성공: %3")
                    .arg(apiName, requestId).arg(success);
    updateBackendRequestStatus(requestId, success);
}

// =================================================================
// 🔄 기존 슬롯들 (기존 호환성 유지)
// =================================================================

void HttpApiHandler::onForensicDataStored(int forensicId, const QString& taskId) {
    Q_UNUSED(forensicId);
    qDebug() << QString("[HttpApiHandler] 포렌식 데이터 저장됨: Task %1").arg(taskId);

    // 🔄 기존 로직: taskId로 상태 업데이트
    QMutexLocker locker(&taskStatusMutex_);
    if (taskStatuses_.contains(taskId)) {
        taskStatuses_[taskId] = "completed";
        qInfo() << QString("[HttpApiHandler] Task 상태 업데이트: %1 -> completed").arg(taskId);
    }
}

// =================================================================
// HTTP 서버 제어 (기존 코드 유지)
// =================================================================

bool HttpApiHandler::startServer(const QString& host, uint16_t port) {
    if (isRunning_) {
        qWarning() << "[HttpApiHandler] HTTP 서버가 이미 실행 중입니다";
        return false;
    }

    httpHost_ = host;
    httpPort_ = port;

    httpServer_ = new QTcpServer(this);
    connect(httpServer_, &QTcpServer::newConnection, this, &HttpApiHandler::onNewConnection);

    if (!httpServer_->listen(QHostAddress(host), port)) {
        qCritical() << QString("[HttpApiHandler] 서버 시작 실패: %1:%2 - %3 (Error Code: %4)")
                           .arg(host).arg(port).arg(httpServer_->errorString()).arg(httpServer_->serverError());
        delete httpServer_;
        httpServer_ = nullptr;
        return false;
    }

    isRunning_ = true;
    qDebug() << QString("[HttpApiHandler] HTTP 서버 시작됨: http://%1:%2").arg(host).arg(port);
    return true;
}

void HttpApiHandler::stopServer() {
    if (!isRunning_) return;

    if (httpServer_) {
        httpServer_->close();
        delete httpServer_;
        httpServer_ = nullptr;
    }

    isRunning_ = false;
    qDebug() << "[HttpApiHandler] HTTP 서버 중지됨";
}

// =================================================================
// HTTP 요청 처리 (기존 + 새로운 엔드포인트 추가)
// =================================================================

void HttpApiHandler::onNewConnection() {
    QTcpSocket* client = httpServer_->nextPendingConnection();
    connect(client, &QTcpSocket::readyRead, this, &HttpApiHandler::onClientReadyRead);
    connect(client, &QTcpSocket::disconnected, this, &HttpApiHandler::onClientDisconnected);
}

void HttpApiHandler::onClientReadyRead() {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (!client) return;

    // ✅ 1. 데이터 누적 (여러 번 도착 가능)
    QByteArray newData = client->readAll();

    {
        QMutexLocker locker(&requestBufferMutex_);
        if (!requestBuffers_.contains(client)) {
            requestBuffers_[client] = QByteArray();
        }
        requestBuffers_[client].append(newData);
    }

    qDebug() << QString("[HttpApiHandler] Data received: %1 bytes (total buffer: %2 bytes)")
                    .arg(newData.size())
                    .arg(requestBuffers_[client].size());

    // ✅ 2. 완전한 HTTP 요청인지 확인
    QString request = QString::fromUtf8(requestBuffers_[client]);

    // HTTP 헤더 끝 찾기
    int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd == -1) {
        qDebug() << "[HttpApiHandler] Incomplete headers, waiting for more data...";
        return;  // 헤더가 완전하지 않음 - 더 기다림
    }

    // Content-Length 확인
    QRegularExpression contentLengthRegex(
        "Content-Length:\\s*(\\d+)",
        QRegularExpression::CaseInsensitiveOption
        );
    QRegularExpressionMatch match = contentLengthRegex.match(request);

    if (match.hasMatch()) {
        // POST/PUT 등 본문이 있는 요청
        int contentLength = match.captured(1).toInt();
        int bodyStart = headerEnd + 4;
        int currentBodyLength = requestBuffers_[client].size() - bodyStart;

        qDebug() << QString("[HttpApiHandler] Content-Length: %1, Current body: %2")
                        .arg(contentLength).arg(currentBodyLength);

        if (currentBodyLength < contentLength) {
            qDebug() << QString("[HttpApiHandler] Incomplete body, waiting... (%1/%2 bytes)")
            .arg(currentBodyLength).arg(contentLength);
            return;  // 본문이 완전하지 않음 - 더 기다림
        }

        // ✅ 본문 완전히 도착 - 처리
        qDebug() << "[HttpApiHandler] Complete request received (with body)";
        handleHttpRequest(client, request);

        {
            QMutexLocker locker(&requestBufferMutex_);
            requestBuffers_.remove(client);
        }

    } else {
        // GET 등 본문이 없는 요청 - 헤더만 있으면 OK
        qDebug() << "[HttpApiHandler] Complete request received (no body)";
        handleHttpRequest(client, request);

        {
            QMutexLocker locker(&requestBufferMutex_);
            requestBuffers_.remove(client);
        }
    }
}

void HttpApiHandler::onClientDisconnected() {
    QTcpSocket* client = qobject_cast<QTcpSocket*>(sender());
    if (client) {
        // ✅ 버퍼 정리
        {
            QMutexLocker locker(&requestBufferMutex_);
            if (requestBuffers_.contains(client)) {
                qDebug() << QString("[HttpApiHandler] Cleaning up buffer for disconnected client: %1 bytes")
                .arg(requestBuffers_[client].size());
                requestBuffers_.remove(client);
            }
        }

        client->deleteLater();
    }
}

void HttpApiHandler::handleHttpRequest(QTcpSocket* client, const QString& request) {
    QString method = extractMethodFromRequest(request);
    QString path = extractPathFromRequest(request);

    qDebug() << QString("[HttpApiHandler] %1 %2").arg(method, path);

    // 🔄 기존 엔드포인트들 (기존 호환성)
    if (path == "/api/health") {
        handleHealthCheck(client);
    }
    else if (path.startsWith("/api/task-status/")) {
        QString taskId = path.mid(QString("/api/task-status/").length());
        handleTaskStatus(client, taskId);
    }
    else if (path == "/api/pcs") {
        handlePCsList(client);
    }
    else if (path == "/api/inspect" && method == "POST") {
        QJsonObject requestData = parseJsonFromRequest(request);
        handleInspectRequest(client, requestData);
    }

    // 🆕새로운 4가지 핵심 기능 엔드포인트들
    else if (path.startsWith("/api/pc-registration-status/")) {
        QString pcId = path.mid(QString("/api/pc-registration-status/").length());
        handlePCRegistrationStatus(client, pcId);
    }
    else if (path.startsWith("/api/pc-changes/")) {
        QString pcId = path.mid(QString("/api/pc-changes/").length());
        handlePCChangeStatus(client, pcId);
    }
    else if (path.startsWith("/api/task-completion/")) {
        QString taskId = path.mid(QString("/api/task-completion/").length());
        handleTaskCompletionStatus(client, taskId);
    }
    else if (path == "/api/backend-status") {
        handleBackendStatus(client);
    }
    else if (path == "/api/networkmanager-status") {
        handleNetworkManagerStatus(client);
    }

    else {
        // 404 Not Found
        QString errorResponse = createErrorResponse("Endpoint not found", 404);
        sendHttpResponse(client, 404, "application/json", errorResponse.toUtf8());
    }
}

// =================================================================
// 🆕 새로운 4가지 핵심 기능 API 핸들러들
// =================================================================

void HttpApiHandler::handlePCRegistrationStatus(QTcpSocket* client, const QString& pcId) {
    QMutexLocker locker(&coreFeaturesMutex_);

    QJsonObject response;
    response["pcId"] = pcId;

    if (pcRegistrationStatuses_.contains(pcId)) {
        response = pcRegistrationStatusToJson(pcRegistrationStatuses_[pcId]);
    } else {
        response["status"] = "unknown";
        response["message"] = "PC registration status not found";
    }

    QString successResponse = createSuccessResponse(response);
    sendHttpResponse(client, 200, "application/json", successResponse.toUtf8());
}

void HttpApiHandler::handlePCChangeStatus(QTcpSocket* client, const QString& pcId) {
    QMutexLocker locker(&coreFeaturesMutex_);

    QJsonObject response;
    response["pcId"] = pcId;

    if (pcChangeResults_.contains(pcId)) {
        response = pcChangeResultToJson(pcChangeResults_[pcId]);
    } else {
        response["hasChanges"] = false;
        response["message"] = "No recent changes detected";
    }

    QString successResponse = createSuccessResponse(response);
    sendHttpResponse(client, 200, "application/json", successResponse.toUtf8());
}

void HttpApiHandler::handleTaskCompletionStatus(QTcpSocket* client, const QString& taskId) {
    QMutexLocker locker(&coreFeaturesMutex_);

    QJsonObject response;
    response["taskId"] = taskId;

    if (taskCompletionStatuses_.contains(taskId)) {
        response = taskCompletionStatusToJson(taskCompletionStatuses_[taskId]);
    } else {
        response["status"] = "unknown";
        response["message"] = "Task completion status not found";
    }

    QString successResponse = createSuccessResponse(response);
    sendHttpResponse(client, 200, "application/json", successResponse.toUtf8());
}

void HttpApiHandler::handleBackendStatus(QTcpSocket* client) {
    QJsonObject response;

    if (networkManager_) {
        response["configured"] = networkManager_->isBackendApiConfigured();
        response["available"] = networkManager_->isBackendAvailable();
    } else {
        response["configured"] = false;
        response["available"] = false;
        response["error"] = "NetworkManager not available";
    }

    QString successResponse = createSuccessResponse(response);
    sendHttpResponse(client, 200, "application/json", successResponse.toUtf8());
}

void HttpApiHandler::handleNetworkManagerStatus(QTcpSocket* client) {
    QJsonObject response = networkManagerStatusToJson();
    QString successResponse = createSuccessResponse(response);
    sendHttpResponse(client, 200, "application/json", successResponse.toUtf8());
}

// =================================================================
// 🆕 상태 관리 메서드들
// =================================================================

void HttpApiHandler::updatePCRegistrationStatus(const QString& pcId,
                                                const NetworkManager::PCRegistrationStatus& status) {
    QMutexLocker locker(&coreFeaturesMutex_);
    pcRegistrationStatuses_[pcId] = status;
}

void HttpApiHandler::updatePCChangeResult(const QString& pcId,
                                          const NetworkManager::PCChangeDetectionResult& result) {
    QMutexLocker locker(&coreFeaturesMutex_);
    pcChangeResults_[pcId] = result;
}

void HttpApiHandler::updateTaskCompletionStatus(const QString& taskId,
                                                const NetworkManager::TaskCompletionStatus& status) {
    QMutexLocker locker(&coreFeaturesMutex_);
    taskCompletionStatuses_[taskId] = status;
}

void HttpApiHandler::updateBackendRequestStatus(const QString& requestId, bool success) {
    QMutexLocker locker(&coreFeaturesMutex_);
    backendRequestStatuses_[requestId] = success;
}

// =================================================================
// 🔄 기존 메서드들 (기존 호환성 유지)
// =================================================================

void HttpApiHandler::setTaskStatus(const QString& taskId, const QString& status) {
    QMutexLocker locker(&taskStatusMutex_);
    taskStatuses_[taskId] = status;
}

QString HttpApiHandler::getTaskStatus(const QString& taskId) const {
    QMutexLocker locker(&taskStatusMutex_);
    return taskStatuses_.value(taskId, "unknown");
}

// =================================================================
// 🆕 JSON 변환 헬퍼들
// =================================================================

QJsonObject HttpApiHandler::pcRegistrationStatusToJson(const NetworkManager::PCRegistrationStatus& status) {
    QJsonObject json;
    json["pcId"] = status.pcId;
    json["isExistingPC"] = status.isExistingPC;
    json["ownerIdVerified"] = status.ownerIdVerified;
    json["currentStatus"] = status.currentStatus;
    json["registrationStartTime"] = status.registrationStartTime.toString(Qt::ISODate);
    if (!status.errorMessage.isEmpty()) {
        json["errorMessage"] = status.errorMessage;
    }
    return json;
}

QJsonObject HttpApiHandler::pcChangeResultToJson(const NetworkManager::PCChangeDetectionResult& result) {
    QJsonObject json;
    json["pcId"] = result.pcId;
    json["hasChanges"] = result.hasChanges;

    QJsonArray changedFieldsArray;
    for (const QString& field : result.changedFields) {
        changedFieldsArray.append(field);
    }
    json["changedFields"] = changedFieldsArray;

    return json;
}

QJsonObject HttpApiHandler::taskCompletionStatusToJson(const NetworkManager::TaskCompletionStatus& status) {
    QJsonObject json;
    json["taskId"] = status.taskId;
    json["pcId"] = status.pcId;
    json["moduleType"] = status.moduleType;
    json["forensicDataStored"] = status.forensicDataStored;
    json["backendNotified"] = status.backendNotified;
    json["currentStatus"] = status.currentStatus;
    json["completionStartTime"] = status.completionStartTime.toString(Qt::ISODate);
    if (!status.errorMessage.isEmpty()) {
        json["errorMessage"] = status.errorMessage;
    }
    return json;
}

QJsonObject HttpApiHandler::networkManagerStatusToJson() {
    QJsonObject json;

    if (networkManager_) {
        NetworkManager::NetworkManagerStatus status = networkManager_->getStatus();
        json["isRunning"] = status.isRunning;
        json["isDatabaseConnected"] = status.isDatabaseConnected;
        json["isBackendApiConfigured"] = status.isBackendApiConfigured;
        json["connectedClientCount"] = status.connectedClientCount;
        json["pendingRegistrations"] = status.pendingRegistrations;
        json["activeTaskCompletions"] = status.activeTaskCompletions;
        json["lastActivity"] = status.lastActivity.toString(Qt::ISODate);
    } else {
        json["error"] = "NetworkManager not available";
    }

    return json;
}

// =================================================================
// 기존 유틸리티 메서드들 (간단 구현)
// =================================================================

void HttpApiHandler::sendHttpResponse(QTcpSocket* client, int statusCode,
                                      const QString& contentType, const QByteArray& body, bool cors) {
    QString response = QString("HTTP/1.1 %1 OK\r\n").arg(statusCode);
    response += QString("Content-Type: %1\r\n").arg(contentType);
    response += QString("Content-Length: %1\r\n").arg(body.size());

    if (cors) {
        response += "Access-Control-Allow-Origin: *\r\n";
        response += "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n";
        response += "Access-Control-Allow-Headers: Content-Type, Authorization\r\n";
    }

    response += "\r\n";

    client->write(response.toUtf8());
    client->write(body);
    client->flush();
    client->disconnectFromHost();
}

QString HttpApiHandler::extractPathFromRequest(const QString& request) {
    QRegularExpression re(R"(^[A-Z]+ ([^\s\?]+))");
    QRegularExpressionMatch match = re.match(request);
    return match.hasMatch() ? match.captured(1) : "/";
}

QString HttpApiHandler::extractMethodFromRequest(const QString& request) {
    QRegularExpression re(R"(^([A-Z]+))");
    QRegularExpressionMatch match = re.match(request);
    return match.hasMatch() ? match.captured(1) : "GET";
}

QJsonObject HttpApiHandler::parseJsonFromRequest(const QString& request) {
    qDebug() << "========== JSON PARSING START ==========";
    qDebug() << QString("Request length: %1 characters").arg(request.length());

    int bodyStart = request.indexOf("\r\n\r\n");
    qDebug() << QString("Body separator position: %1").arg(bodyStart);

    if (bodyStart == -1) {
        qWarning() << "[HttpApiHandler] ❌ Body separator not found!";
        qDebug() << "Request preview:" << request.left(200);
        return QJsonObject();
    }

    QString headers = request.left(bodyStart);
    qDebug() << "Headers:" << headers;

    QString body = request.mid(bodyStart + 4);
    qDebug() << QString("Body length: %1 characters").arg(body.length());
    qDebug() << "Body content:" << body;

    if (body.isEmpty()) {
        qWarning() << "[HttpApiHandler] ❌ Body is empty!";
        return QJsonObject();
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8(), &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << QString("[HttpApiHandler] ❌ JSON parse error: %1 at offset %2")
                          .arg(parseError.errorString()).arg(parseError.offset);
    } else {
        qDebug() << "[HttpApiHandler] ✅ JSON parsed successfully";
        qDebug() << "Parsed object:" << doc.object();
    }

    qDebug() << "========== JSON PARSING END ==========";

    return doc.object();
}

QString HttpApiHandler::createSuccessResponse(const QJsonObject& data) {
    QJsonObject response;
    response["success"] = true;
    response["data"] = data;
    response["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

QString HttpApiHandler::createErrorResponse(const QString& error, int code) {
    QJsonObject response;
    response["success"] = false;
    response["error"] = error;
    response["code"] = code;
    response["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonDocument doc(response);
    return doc.toJson(QJsonDocument::Compact);
}

// 기존 핸들러들 간단 구현
void HttpApiHandler::handleHealthCheck(QTcpSocket* client) {
    QJsonObject data;
    data["status"] = "healthy";
    data["uptime"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QString response = createSuccessResponse(data);
    sendHttpResponse(client, 200, "application/json", response.toUtf8());
}

void HttpApiHandler::handleTaskStatus(QTcpSocket* client, const QString& taskId) {
    QJsonObject data;
    data["taskId"] = taskId;
    data["status"] = getTaskStatus(taskId);

    QString response = createSuccessResponse(data);
    sendHttpResponse(client, 200, "application/json", response.toUtf8());
}

void HttpApiHandler::handlePCsList(QTcpSocket* client) {
    QJsonObject data;
    data["message"] = "PC list functionality - implementation needed";

    QString response = createSuccessResponse(data);
    sendHttpResponse(client, 200, "application/json", response.toUtf8());
}

void HttpApiHandler::handleInspectRequest(QTcpSocket* client, const QJsonObject& requestData) {
    qDebug() << "[HttpApiHandler] POST /api/inspect" << requestData;

    // 🎯 간소화된 API: pc_id와 task_id만 받음
    QString pcId = requestData.value("pc_id").toString();
    QString taskId = requestData.value("task_id").toString();

    // 🔍 API 입력 검증 및 디버깅
    qDebug() << QString("[HttpApiHandler] API 입력 - PC ID: '%1', Task ID: '%2'").arg(pcId, taskId);

    if (pcId.isEmpty()) {
        qWarning() << "[HttpApiHandler] pc_id가 비어있음";
        QString errorResponse = createErrorResponse("pc_id is required", 400);
        sendHttpResponse(client, 400, "application/json", errorResponse.toUtf8());
        return;
    }

    // 🎯 task_id가 없으면 기본값 생성
    if (taskId.isEmpty()) {
        taskId = QString("inspection_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        qDebug() << QString("[HttpApiHandler] Task ID 자동 생성: %1").arg(taskId);
    }

    qDebug() << QString("[HttpApiHandler] 전체 검사 요청: Task=%1, PC=%2").arg(taskId, pcId);

    // NetworkManager를 통해 전체 검사 명령 전송
    if (networkManager_) {
        // TaskRequest 구조체 생성
        NetworkManager::TaskRequest task;
        task.taskId = taskId;           // ✅ 사용자가 보낸 task_id 그대로 사용
        task.tasktype = "ALL_DATA";     // ✅ 자동으로 ALL_DATA 설정 (API에서 받지 않음)

        // parameters 설정
        task.parameters["task_name"] = taskId;  // 호환성을 위해 task_name도 포함
        task.parameters["collect_pc_info"] = true;
        task.requestTime = QDateTime::currentDateTime();

        qDebug() << QString("[HttpApiHandler] TaskRequest 생성 완료 - ID: %1, Type: %2").arg(task.taskId, task.tasktype);

        // 클라이언트에게 전체 데이터 수집 명령 전송
        bool taskSent = networkManager_->sendTaskToClient(pcId, task);

        if (taskSent) {
            // Task 상태 초기화 (진행 중으로 설정)
            setTaskStatus(taskId, "running");

            // 성공 응답
            QJsonObject responseData;
            responseData["task_id"] = taskId;       // ✅ 사용자가 보낸 task_id 그대로 반환
            responseData["pc_id"] = pcId;
            responseData["task_type"] = task.tasktype; // 🆕 자동 설정된 task_type도 반환
            responseData["status"] = "started";
            responseData["message"] = QString("전체 검사가 시작되었습니다: %1").arg(taskId);

            QString successResponse = createSuccessResponse(responseData);
            sendHttpResponse(client, 200, "application/json", successResponse.toUtf8());

            qDebug() << QString("[HttpApiHandler] 전체 검사 명령 전송 성공: %1 -> %2").arg(taskId, pcId);
        } else {
            // 실패 응답
            QString errorResponse = createErrorResponse("Failed to send task to client", 500);
            sendHttpResponse(client, 500, "application/json", errorResponse.toUtf8());

            qWarning() << QString("[HttpApiHandler] 전체 검사 명령 전송 실패: %1 -> %2").arg(taskId, pcId);
        }
    } else {
        QString errorResponse = createErrorResponse("NetworkManager not available", 500);
        sendHttpResponse(client, 500, "application/json", errorResponse.toUtf8());
    }
}
