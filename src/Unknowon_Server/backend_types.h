// backend_types.h - 백엔드 API 연동 구조체 정의
// 포렌식 서버와 백엔드 시스템 간 데이터 교환용 구조체들

#ifndef BACKEND_TYPES_H
#define BACKEND_TYPES_H

#include "pch.h"
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>
#include <QNetworkReply>

namespace BackendApi {

// =================================================================
// 공통 HTTP 응답 구조체
// =================================================================

struct ApiResponse {
    bool success;
    int statusCode;
    QString message;
    QJsonObject data;
    QDateTime responseTime;

    ApiResponse() : success(false), statusCode(0) {
        responseTime = QDateTime::currentDateTime();
    }

    // JSON에서 ApiResponse 생성
    static ApiResponse fromJson(const QJsonObject& json) {
        ApiResponse response;
        response.success = json.value("success").toBool(false);
        response.statusCode = json.value("status_code").toInt(0);
        response.message = json.value("message").toString();
        response.data = json.value("data").toObject();
        return response;
    }

    // 네트워크 에러에서 ApiResponse 생성
    static ApiResponse fromNetworkError(QNetworkReply::NetworkError error, const QString& errorString) {
        ApiResponse response;
        response.success = false;
        response.statusCode = (error == QNetworkReply::TimeoutError) ? 408 : 500;
        response.message = QString("Network Error: %1").arg(errorString);
        return response;
    }
};

// =================================================================
// 1. Owner_ID 검증 API 구조체 (POST /api/verify-owner)
// =================================================================

struct VerifyOwnerRequest {
    QString ownerId;        // 검증할 Owner ID
    QString pcId;           // PC 식별자 (MAC_XX-XX-XX-XX-XX-XX)
    QString pcName;         // PC 이름
    QString ip;             // 현재 IP 주소
    QString os;             // OS 정보
    QString hostname;       // 호스트명
    QString macAddress;     // MAC 주소
    QDateTime requestTime;  // 요청 시간

    VerifyOwnerRequest() {
        requestTime = QDateTime::currentDateTime();
    }

    // 🔧 JSON 변환 - 5개 필드만 전송
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["user_login_id"] = ownerId;     // ✅ 필드 1
        obj["pc_id"] = pcId;                // ✅ 필드 2
        obj["pc_name"] = pcName;            // ✅ 필드 3
        obj["ip"] = ip;                     // ✅ 필드 4
        obj["os"] = os;                     // ✅ 필드 5

        // 🚫 제거된 필드들 (더 이상 전송 안 함):
        // obj["hostname"] = hostname;           // 제거
        // obj["mac_address"] = macAddress;      // 제거
        // obj["request_time"] = requestTime.toString(Qt::ISODate); // 제거

        return obj;
    }

    // 유효성 검사 - mac_address 의존성 제거
    bool isValid() const {
        return !ownerId.isEmpty() && !pcId.isEmpty();  // macAddress 체크 제거
    }
};

struct VerifyOwnerResponse {
    bool isValidOwner;      // Owner ID 유효 여부
    bool canRegister;       // 등록 가능 여부
    QString reason;         // 실패/성공 사유
    QJsonObject ownerInfo;  // 추가 소유자 정보 (선택사항)

    VerifyOwnerResponse() : isValidOwner(false), canRegister(false) {}

    // JSON에서 생성
    static VerifyOwnerResponse fromJson(const QJsonObject& json) {
        VerifyOwnerResponse response;
        response.isValidOwner = json.value("is_valid_owner").toBool(false);
        response.canRegister = json.value("can_register").toBool(false);
        response.reason = json.value("reason").toString();
        response.ownerInfo = json.value("owner_info").toObject();
        return response;
    }
};

// =================================================================
// 2. PC 정보 변경 알림 API 구조체 (POST /api/client-update)
// =================================================================

struct ClientUpdateRequest {
    QString pcId;           // PC 식별자
    QString ownerId;        // 소유자 ID

    // 변경된 정보들
    QString newPcName;      // 새로운 PC 이름
    QString newIp;          // 새로운 IP 주소
    QString newOs;          // 새로운 OS 정보
    QString newHostname;    // 새로운 호스트명

    // 이전 정보들 (비교용)
    QString oldPcName;      // 이전 PC 이름
    QString oldIp;          // 이전 IP 주소
    QString oldOs;          // 이전 OS 정보
    QString oldHostname;    // 이전 호스트명

    QDateTime changeTime;   // 변경 감지 시간
    QStringList changedFields; // 변경된 필드 목록 ("pc_name", "ip", "os", "hostname")

    ClientUpdateRequest() {
        changeTime = QDateTime::currentDateTime();
    }

    // JSON 변환
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["pc_id"] = pcId;
        obj["owner_id"] = ownerId;

        QJsonObject newInfo;
        newInfo["pc_name"] = newPcName;
        newInfo["ip"] = newIp;
        newInfo["os"] = newOs;
        newInfo["hostname"] = newHostname;
        obj["new_info"] = newInfo;

        QJsonObject oldInfo;
        oldInfo["pc_name"] = oldPcName;
        oldInfo["ip"] = oldIp;
        oldInfo["os"] = oldOs;
        oldInfo["hostname"] = oldHostname;
        obj["old_info"] = oldInfo;

        obj["change_time"] = changeTime.toString(Qt::ISODate);
        obj["changed_fields"] = QJsonArray::fromStringList(changedFields);

        return obj;
    }

    // 유효성 검사
    bool isValid() const {
        return !pcId.isEmpty() && !ownerId.isEmpty() && !changedFields.isEmpty();
    }

    // 변경 필드 추가 헬퍼
    void addChangedField(const QString& field) {
        if (!changedFields.contains(field)) {
            changedFields.append(field);
        }
    }
};

struct ClientUpdateResponse {
    bool notified;          // 알림 성공 여부
    QString message;        // 응답 메시지
    QDateTime processedTime; // 처리 시간

    ClientUpdateResponse() : notified(false) {
        processedTime = QDateTime::currentDateTime();
    }

    // JSON에서 생성
    static ClientUpdateResponse fromJson(const QJsonObject& json) {
        ClientUpdateResponse response;
        response.notified = json.value("notified").toBool(false);
        response.message = json.value("message").toString();

        QString timeStr = json.value("processed_time").toString();
        if (!timeStr.isEmpty()) {
            response.processedTime = QDateTime::fromString(timeStr, Qt::ISODate);
        }

        return response;
    }
};

// =================================================================
// 3. Task 완료 알림 API 구조체 (POST /api/task-complete)
// =================================================================

struct TaskCompleteRequest {
    QString pcId;           // PC 식별자
    QString taskId;         // 작업 ID
    QString moduleType;     // 모듈 타입 (BROWSER_DATA, USB_DATA 등)
    QString ownerId;        // 소유자 ID

    // Task 실행 정보
    QDateTime taskStartTime;    // 작업 시작 시간
    QDateTime taskEndTime;      // 작업 완료 시간
    bool isSuccess;             // 작업 성공 여부
    QString errorMessage;       // 실패 시 오류 메시지

    // 수집 결과 정보
    qint64 dataSize;            // 수집된 데이터 크기
    int fileCount;              // 수집된 파일 수
    QString checksum;           // 데이터 체크섬
    QJsonObject summary;        // 수집 요약 정보

    TaskCompleteRequest() : isSuccess(false), dataSize(0), fileCount(0) {
        taskEndTime = QDateTime::currentDateTime();
    }

    // JSON 변환
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["pc_id"] = pcId;
        obj["task_id"] = taskId;
        obj["module_type"] = moduleType;
        obj["owner_id"] = ownerId;

        obj["task_start_time"] = taskStartTime.toString(Qt::ISODate);
        obj["task_end_time"] = taskEndTime.toString(Qt::ISODate);
        obj["is_success"] = isSuccess;
        obj["error_message"] = errorMessage;

        QJsonObject resultInfo;
        resultInfo["data_size"] = static_cast<double>(dataSize);
        resultInfo["file_count"] = fileCount;
        resultInfo["checksum"] = checksum;
        resultInfo["summary"] = summary;
        obj["result_info"] = resultInfo;

        return obj;
    }

    // 유효성 검사
    bool isValid() const {
        return !pcId.isEmpty() && !taskId.isEmpty() && !moduleType.isEmpty() && !ownerId.isEmpty();
    }

    // 실행 시간 계산
    qint64 getExecutionTimeMs() const {
        if (!taskStartTime.isValid() || !taskEndTime.isValid()) {
            return 0;
        }
        return taskStartTime.msecsTo(taskEndTime);
    }
};

struct TaskCompleteResponse {
    bool acknowledged;      // 알림 수신 확인
    QString message;        // 응답 메시지
    QDateTime processedTime; // 처리 시간
    QJsonObject additionalInfo; // 추가 정보

    TaskCompleteResponse() : acknowledged(false) {
        processedTime = QDateTime::currentDateTime();
    }

    // JSON에서 생성
    static TaskCompleteResponse fromJson(const QJsonObject& json) {
        TaskCompleteResponse response;
        response.acknowledged = json.value("acknowledged").toBool(false);
        response.message = json.value("message").toString();
        response.additionalInfo = json.value("additional_info").toObject();

        QString timeStr = json.value("processed_time").toString();
        if (!timeStr.isEmpty()) {
            response.processedTime = QDateTime::fromString(timeStr, Qt::ISODate);
        }

        return response;
    }
};

// =================================================================
// HTTP 클라이언트 설정 구조체
// =================================================================

struct BackendConfig {
    QString baseUrl;        // 백엔드 서버 기본 URL
    int timeoutMs;          // 타임아웃 (밀리초)
    int retryCount;         // 재시도 횟수
    int retryDelayMs;       // 재시도 간격 (밀리초)
    bool enableSsl;         // SSL 사용 여부
    QString userAgent;      // User-Agent 헤더

    // 인증 관련 (향후 확장용)
    QString apiKey;         // API 키
    QString authToken;      // 인증 토큰

    BackendConfig() : timeoutMs(30000), retryCount(3), retryDelayMs(1000), enableSsl(false) {
        userAgent = "ForensicAgent/1.0";
    }

    // 유효성 검사
    bool isValid() const {
        return !baseUrl.isEmpty() && timeoutMs > 0;
    }

    // URL 생성 헬퍼
    QString getApiUrl(const QString& endpoint) const {
        QString url = baseUrl;
        if (!url.endsWith('/')) {
            url += '/';
        }
        if (endpoint.startsWith('/')) {
            url += endpoint.mid(1);
        } else {
            url += endpoint;
        }
        return url;
    }
};

// =================================================================
// 에러 코드 열거형
// =================================================================

enum class BackendApiError {
    NoError = 0,
    NetworkError,           // 네트워크 연결 오류
    TimeoutError,           // 타임아웃
    HttpError,              // HTTP 상태 오류 (4xx, 5xx)
    JsonParseError,         // JSON 파싱 오류
    ValidationError,        // 요청 데이터 유효성 검사 오류
    AuthenticationError,    // 인증 오류
    ServerError,            // 서버 내부 오류
    UnknownError           // 알 수 없는 오류
};

// 에러 코드를 문자열로 변환
inline QString backendApiErrorToString(BackendApiError error) {
    switch (error) {
    case BackendApiError::NoError: return "No Error";
    case BackendApiError::NetworkError: return "Network Error";
    case BackendApiError::TimeoutError: return "Timeout Error";
    case BackendApiError::HttpError: return "HTTP Error";
    case BackendApiError::JsonParseError: return "JSON Parse Error";
    case BackendApiError::ValidationError: return "Validation Error";
    case BackendApiError::AuthenticationError: return "Authentication Error";
    case BackendApiError::ServerError: return "Server Error";
    case BackendApiError::UnknownError: return "Unknown Error";
    }
    return "Unknown Error";
}

// =================================================================
// 로깅용 헬퍼 함수들
// =================================================================

inline QString requestToLogString(const QJsonObject& request, const QString& apiName) {
    QJsonDocument doc(request);
    return QString("[%1] Request: %2").arg(apiName, QString::fromUtf8(doc.toJson(QJsonDocument::Compact)));
}

inline QString responseToLogString(const ApiResponse& response, const QString& apiName) {
    return QString("[%1] Response: success=%2, status=%3, message=%4")
    .arg(apiName)
        .arg(response.success ? "true" : "false")
        .arg(response.statusCode)
        .arg(response.message);
}

} // namespace BackendApi

#endif // BACKEND_TYPES_H
