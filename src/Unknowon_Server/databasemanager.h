// DatabaseManager.h - 새로운 간소화된 데이터베이스 관리자
// client_info, forensic_info 테이블 전용, DatabaseSchema 완전 의존

#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include "pch.h"
#include "databaseschema.h"
#include "backend_types.h"
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QMutex>
#include <QDateTime>
#include <QJsonObject>
#include <QSettings>
#include <QFile>

// =================================================================
// DatabaseManager - DatabaseSchema 완전 의존 방식
// 모든 쿼리는 DatabaseSchema에서 가져와서 사용
// 직접 SQL 문자열 작성 금지
// =================================================================

class DatabaseManager : public QObject {
    Q_OBJECT

public:
    // =================================================================
    // 설정 구조체
    // =================================================================

    struct DatabaseConfig {
        QString host = "localhost";
        int port = 5432;
        QString database = "forensic_agent";
        QString username = "forensic_agent";
        QString password;  // 기본값 제거 - config.ini에서 읽어야 함
        bool sslEnabled = false;
        int connectionTimeout = 30;
        QString charset = "UTF8";
        // 🔐 암호화 설정
        bool enableEncryption = true;
        QString encryptionKey;  // 기본값 제거 - config.ini에서 읽어야 함
    };

    // =================================================================
    // 데이터 구조체 (새 테이블 구조에 맞춤)
    // =================================================================

    // client_info 테이블 구조체
    struct ClientInfo {
        QString pcId;               // pc_id
        QString pcName;             // pc_name
        QString ip;                 // ip
        QString os;                 // os
        QDateTime firstConnect;     // first_connect
        QDateTime lastConnect;      // last_connect
        QDateTime recentScan;       // recent_scan (NULL 가능)

        ClientInfo() {
            QDateTime now = QDateTime::currentDateTime();
            firstConnect = now;
            lastConnect = now;
            // recentScan은 초기화하지 않음 (NULL 상태)
        }

        bool isValid() const {
            return !pcId.isEmpty();
        }
    };

    // forensic_info 테이블 구조체
    struct ForensicInfo {
        int id;                     // id (auto increment)
        QString pcId;               // pc_id
        QString taskId;             // task_id
        QString moduleType;         // module_type
        QDateTime collectionTime;   // collection_time
        qint64 fileSize;            // file_size
        QJsonObject jsonData;       // json_data
        QDateTime createdAt;        // created_at

        ForensicInfo() : id(0), fileSize(0) {
            collectionTime = QDateTime::currentDateTime();
        }

        bool isValid() const {
            return !pcId.isEmpty() && !taskId.isEmpty() && !moduleType.isEmpty();
        }
    };

    // PC 정보 변경 감지용 구조체
    struct ClientChangeInfo {
        QString pcId;
        QString oldPcName, newPcName;
        QString oldIp, newIp;
        QString oldOs, newOs;
        QStringList changedFields;

        bool hasChanges() const {
            return !changedFields.isEmpty();
        }
    };

public:
    // =================================================================
    // 생성자/소멸자
    // =================================================================

    explicit DatabaseManager(QObject* parent = nullptr);
    explicit DatabaseManager(const DatabaseConfig& config, QObject* parent = nullptr);
    virtual ~DatabaseManager();

    // =================================================================
    // 연결 관리
    // =================================================================

    bool connect();
    bool connect(const DatabaseConfig& config);
    void disconnect();
    bool isConnected() const { return isConnected_; }
    bool testConnection();
    void setConfig(const DatabaseConfig& config) { config_ = config; }
    DatabaseConfig getConfig() const { return config_; }

    // =================================================================
    // 초기화 (DatabaseSchema 쿼리 사용)
    // =================================================================

    bool initializeDatabase();      // 전체 DB 초기화
    bool createTables();           // 테이블 생성 (DatabaseSchema 사용)
    bool createIndexes();          // 인덱스 생성 (DatabaseSchema 사용)

    // =================================================================
    // PC 관리 (client_info 테이블)
    // =================================================================

    // PC 등록/업데이트 (DatabaseSchema::UPSERT_CLIENT_INFO 사용)
    bool registerOrUpdateClient(const ClientInfo& clientInfo);

    // PC 정보 조회 (DatabaseSchema::SELECT_CLIENT_INFO_BY_PC_ID 사용)
    ClientInfo getClientInfo(const QString& pcId);

    // 전체 PC 목록 조회 (DatabaseSchema::SELECT_ALL_CLIENT_INFO 사용)
    QList<ClientInfo> getAllClients();

    // 마지막 연결 시간 업데이트 (DatabaseSchema::UPDATE_CLIENT_LAST_CONNECT 사용)
    bool updateClientLastConnect(const QString& pcId, const QDateTime& lastConnect = QDateTime::currentDateTime());

    // recent_scan 업데이트 - Task 완료 시 호출 (DatabaseSchema::UPDATE_CLIENT_RECENT_SCAN 사용)
    bool updateClientRecentScan(const QString& pcId, const QDateTime& recentScan = QDateTime::currentDateTime());

    // PC 정보 변경 감지 (DatabaseSchema::SELECT_CLIENT_INFO_FOR_CHANGE_DETECTION 사용)
    ClientChangeInfo detectClientChanges(const QString& pcId, const ClientInfo& newInfo);

    // =================================================================
    // 포렌식 데이터 관리 (forensic_info 테이블)
    // =================================================================

    // 포렌식 데이터 저장 (DatabaseSchema::INSERT_FORENSIC_INFO 사용)
    int storeForensicData(const ForensicInfo& forensicInfo);

    // PC별 포렌식 데이터 조회 (DatabaseSchema::SELECT_FORENSIC_INFO_BY_PC_ID 사용)
    QList<ForensicInfo> getForensicDataByPcId(const QString& pcId, int limit = 100, int offset = 0);

    // Task별 포렌식 데이터 조회 (DatabaseSchema::SELECT_FORENSIC_INFO_BY_TASK_ID 사용)
    QList<ForensicInfo> getForensicDataByTaskId(const QString& taskId);

    // 모듈별 포렌식 데이터 조회 (DatabaseSchema::SELECT_FORENSIC_INFO_BY_MODULE_TYPE 사용)
    QList<ForensicInfo> getForensicDataByModuleType(const QString& moduleType, int limit = 100, int offset = 0);

    // 최신 포렌식 데이터 조회 (DatabaseSchema::SELECT_LATEST_FORENSIC_INFO 사용)
    QList<ForensicInfo> getLatestForensicData(int limit = 100, int offset = 0);

    // Task 완료 여부 확인 (DatabaseSchema::CHECK_TASK_COMPLETION 사용)
    bool isTaskCompleted(const QString& taskId);

    // PC별 Task 통계 조회 (DatabaseSchema::SELECT_PC_TASK_STATS 사용)
    struct PCTaskStats {
        QString pcId;
        int totalTasks;
        int moduleTypes;
        QDateTime latestCollection;
        qint64 totalSize;
    };
    PCTaskStats getPCTaskStats(const QString& pcId);

    // 🆕 Task별 모듈 완료 상태 조회 함수들
    /**
     * @brief Task의 저장된 모듈 개수를 반환
     * @param taskId 확인할 Task ID
     * @return 저장된 고유 모듈 타입 개수 (0-6)
     */
    int getModuleCountForTask(const QString& taskId);

    /**
     * @brief Task에서 누락된 모듈 타입들을 반환
     * @param taskId 확인할 Task ID
     * @return 누락된 모듈 타입 리스트 (예: ["NETWORK_DATA", "REGISTRY_DATA"])
     */
    QStringList getMissingModulesForTask(const QString& taskId);

    // =================================================================
    // 백엔드 API 연동 헬퍼 메서드들
    // =================================================================

    // Owner_ID 검증 (PC가 이미 등록되어 있으면 검증 완료로 간주)
    bool isOwnerIdVerified(const QString& pcId);

    // PC 정보 변경 감지 및 백엔드 알림 준비
    BackendApi::ClientUpdateRequest prepareClientUpdateRequest(const ClientChangeInfo& changeInfo, const QString& ownerId = "default");

    // Task 완료 알림 준비
    BackendApi::TaskCompleteRequest prepareTaskCompleteRequest(const QString& pcId, const QString& taskId,
                                                               const QString& moduleType, const ForensicInfo& forensicInfo,
                                                               const QString& ownerId = "default");

signals:
    // 데이터베이스 연결 관련 시그널
    void databaseConnected();
    void databaseDisconnected();
    void databaseError(const QString& error);

    // 데이터 변경 관련 시그널
    void clientInfoUpdated(const QString& pcId);
    void forensicDataStored(int forensicId, const QString& taskId);
    void taskCompleted(const QString& taskId, const QString& pcId, const QString& moduleType);

    // 백엔드 API 알림용 시그널
    void clientInfoChanged(const BackendApi::ClientUpdateRequest& updateRequest);
    void taskCompletionNotification(const BackendApi::TaskCompleteRequest& completeRequest);

private:
    // =================================================================
    // 내부 멤버 변수
    // =================================================================

    QSqlDatabase database_;
    DatabaseConfig config_;
    QString connectionName_;
    bool isConnected_;
    mutable QMutex dbMutex_;

    // 연결 풀링을 위한 정적 카운터
    static int connectionCounter_;

    // =================================================================
    // 내부 헬퍼 메서드들 (DatabaseSchema 쿼리만 사용)
    // =================================================================

    // 쿼리 실행 (DatabaseSchema 쿼리 상수만 받음)
    bool executeQuery(QSqlQuery& query, const QString& schemaQuery, const QVariantMap& params = QVariantMap());

    // SELECT 쿼리 실행 (DatabaseSchema 쿼리 상수만 받음)
    bool executeSelectQuery(QSqlQuery& query, const QString& schemaQuery, const QVariantMap& params = QVariantMap());

    // 결과를 구조체로 변환
    ClientInfo recordToClientInfo(const QSqlRecord& record);
    ForensicInfo recordToForensicInfo(const QSqlRecord& record);

    // 매개변수 바인딩 헬퍼
    void bindQueryParams(QSqlQuery& query, const QVariantMap& params);

    // 에러 로깅
    void logDatabaseError(const QString& operation, const QSqlError& error) const;

    // UUID 생성
    QString generateUuid() const;

    // JSON 데이터 검증
    bool validateJsonData(const QJsonObject& jsonData) const;

    // 🔐 암호화 헬퍼 메서드 (새로 추가)
    QString encryptJsonData(const QJsonObject& jsonData) const;
    QJsonObject decryptJsonData(const QString& encryptedData) const;
};

#endif // DATABASEMANAGER_H
