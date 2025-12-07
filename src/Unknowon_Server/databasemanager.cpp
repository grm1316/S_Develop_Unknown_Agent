// DatabaseManager.cpp - 새로운 간소화된 구현
// DatabaseSchema 완전 의존, client_info/forensic_info 테이블 전용

#include "DatabaseManager.h"
#include <QUuid>
#include <QVariant>
#include <QDebug>
#include <QSqlRecord>
#include <QJsonDocument>
#include "CryptoUtils.h"

// 정적 멤버 초기화
int DatabaseManager::connectionCounter_ = 0;

// =================================================================
// 생성자/소멸자
// =================================================================

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent), isConnected_(false) {
    connectionName_ = QString("DatabaseManager_%1").arg(++connectionCounter_);

    // 🔐 기본 암호화 설정 초기화
    config_.enableEncryption = true;
    config_.encryptionKey = "Unknownserver2025!securekey";

    qDebug() << "[DatabaseManager] 기본 생성자 - 암호화 활성화:" << config_.enableEncryption;
}

DatabaseManager::DatabaseManager(const DatabaseConfig& config, QObject* parent)
    : QObject(parent), config_(config), isConnected_(false) {
    connectionName_ = QString("DatabaseManager_%1").arg(++connectionCounter_);
}

DatabaseManager::~DatabaseManager() {
    disconnect();
}

// =================================================================
// 연결 관리
// =================================================================

bool DatabaseManager::connect() {
    QMutexLocker locker(&dbMutex_);

    if (isConnected_) {
        qDebug() << "[DatabaseManager] Already connected";
        return true;
    }

    try {
        database_ = QSqlDatabase::addDatabase("QPSQL", connectionName_);
        database_.setHostName(config_.host);
        database_.setPort(config_.port);
        database_.setDatabaseName(config_.database);
        database_.setUserName(config_.username);
        database_.setPassword(config_.password);

        if (config_.sslEnabled) {
            database_.setConnectOptions("sslmode=require");
        }

        if (!database_.open()) {
            QString error = database_.lastError().text();
            qCritical() << "[DatabaseManager] Connection failed:" << error;
            emit databaseError(error);
            return false;
        }

        // 연결 테스트
        QSqlQuery testQuery(database_);
        if (!testQuery.exec("SELECT version()")) {
            QString error = testQuery.lastError().text();
            qCritical() << "[DatabaseManager] Connection test failed:" << error;
            database_.close();
            emit databaseError(error);
            return false;
        }

        isConnected_ = true;
        qDebug() << "[DatabaseManager] Successfully connected to PostgreSQL";

        if (testQuery.next()) {
            QString version = testQuery.value(0).toString();
            qDebug() << "[DatabaseManager] PostgreSQL version:" << version;
        }

        emit databaseConnected();
        return true;

    } catch (const std::exception& e) {
        qCritical() << "[DatabaseManager] Exception during connection:" << e.what();
        isConnected_ = false;
        return false;
    }
}

bool DatabaseManager::connect(const DatabaseConfig& config) {
    config_ = config;
    return connect();
}

void DatabaseManager::disconnect() {
    QMutexLocker locker(&dbMutex_);

    if (isConnected_) {
        database_.close();
        QSqlDatabase::removeDatabase(connectionName_);
        isConnected_ = false;

        qDebug() << "[DatabaseManager] Disconnected from database";
        emit databaseDisconnected();
    }
}

bool DatabaseManager::testConnection() {
    if (!isConnected_) return false;

    QSqlQuery query(database_);
    return query.exec("SELECT 1");
}

// =================================================================
// 초기화 (DatabaseSchema 쿼리 사용)
// =================================================================

bool DatabaseManager::initializeDatabase() {
    qDebug() << "[DatabaseManager] Initializing database schema...";

    if (!isConnected_) {
        qCritical() << "[DatabaseManager] Database not connected";
        return false;
    }

    // DatabaseSchema의 초기화 쿼리 목록 사용
    QStringList queries = DatabaseSchema::getInitializationQueries();

    QSqlQuery query(database_);

    for (const QString& queryString : queries) {
        if (!query.exec(queryString)) {
            logDatabaseError("Database initialization", query.lastError());
            return false;
        }
        qDebug() << "[DatabaseManager] Executed initialization query successfully";
    }

    qDebug() << "[DatabaseManager] Database schema initialized successfully";
    return true;
}

bool DatabaseManager::createTables() {
    qDebug() << "[DatabaseManager] Creating tables...";

    QSqlQuery query(database_);

    // client_info 테이블 생성 (DatabaseSchema 사용)
    if (!query.exec(DatabaseSchema::CREATE_CLIENT_INFO_TABLE)) {
        logDatabaseError("Create client_info table", query.lastError());
        return false;
    }

    // forensic_info 테이블 생성 (DatabaseSchema 사용)
    if (!query.exec(DatabaseSchema::CREATE_FORENSIC_INFO_TABLE)) {
        logDatabaseError("Create forensic_info table", query.lastError());
        return false;
    }

    qDebug() << "[DatabaseManager] Tables created successfully";
    return true;
}

bool DatabaseManager::createIndexes() {
    qDebug() << "[DatabaseManager] Creating indexes...";

    QSqlQuery query(database_);

    // client_info 인덱스 생성 (DatabaseSchema 사용)
    if (!query.exec(DatabaseSchema::CREATE_CLIENT_INFO_INDEXES)) {
        logDatabaseError("Create client_info indexes", query.lastError());
        return false;
    }

    // forensic_info 인덱스 생성 (DatabaseSchema 사용)
    if (!query.exec(DatabaseSchema::CREATE_FORENSIC_INFO_INDEXES)) {
        logDatabaseError("Create forensic_info indexes", query.lastError());
        return false;
    }

    qDebug() << "[DatabaseManager] Indexes created successfully";
    return true;
}

// =================================================================
// PC 관리 (client_info 테이블) - DatabaseSchema 쿼리만 사용
// =================================================================

bool DatabaseManager::registerOrUpdateClient(const ClientInfo& clientInfo) {
    if (!clientInfo.isValid()) {
        qWarning() << "[DatabaseManager] Invalid client info";
        return false;
    }

    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = clientInfo.pcId;
    params["pc_name"] = clientInfo.pcName;
    params["ip"] = clientInfo.ip;
    params["os"] = clientInfo.os;
    params["first_connect"] = clientInfo.firstConnect;
    params["last_connect"] = clientInfo.lastConnect;

    // DatabaseSchema::UPSERT_CLIENT_INFO 사용
    if (!executeQuery(query, DatabaseSchema::UPSERT_CLIENT_INFO, params)) {
        return false;
    }

    if (query.next()) {
        QString returnedPcId = query.value("pc_id").toString();
        qDebug() << "[DatabaseManager] Client registered/updated:" << returnedPcId;
        emit clientInfoUpdated(returnedPcId);
        return true;
    }

    return false;
}

DatabaseManager::ClientInfo DatabaseManager::getClientInfo(const QString& pcId) {
    ClientInfo clientInfo;

    if (pcId.isEmpty()) {
        return clientInfo;
    }

    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = pcId;

    // DatabaseSchema::SELECT_CLIENT_INFO_BY_PC_ID 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_CLIENT_INFO_BY_PC_ID, params)) {
        return clientInfo;
    }

    if (query.next()) {
        clientInfo = recordToClientInfo(query.record());
    }

    return clientInfo;
}

QList<DatabaseManager::ClientInfo> DatabaseManager::getAllClients() {
    QList<ClientInfo> clients;

    QSqlQuery query(database_);

    // DatabaseSchema::SELECT_ALL_CLIENT_INFO 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_ALL_CLIENT_INFO)) {
        return clients;
    }

    while (query.next()) {
        clients.append(recordToClientInfo(query.record()));
    }

    qDebug() << "[DatabaseManager] Retrieved" << clients.size() << "clients";
    return clients;
}

bool DatabaseManager::updateClientLastConnect(const QString& pcId, const QDateTime& lastConnect) {
    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = pcId;
    params["last_connect"] = lastConnect;

    // DatabaseSchema::UPDATE_CLIENT_LAST_CONNECT 사용
    bool success = executeQuery(query, DatabaseSchema::UPDATE_CLIENT_LAST_CONNECT, params);

    if (success) {
        qDebug() << "[DatabaseManager] Updated last connect for PC:" << pcId;
        emit clientInfoUpdated(pcId);
    }

    return success;
}

bool DatabaseManager::updateClientRecentScan(const QString& pcId, const QDateTime& recentScan) {
    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = pcId;
    params["recent_scan"] = recentScan;

    // DatabaseSchema::UPDATE_CLIENT_RECENT_SCAN 사용
    bool success = executeQuery(query, DatabaseSchema::UPDATE_CLIENT_RECENT_SCAN, params);

    if (success) {
        qDebug() << "[DatabaseManager] Updated recent scan for PC:" << pcId << "at" << recentScan.toString();
        emit clientInfoUpdated(pcId);
    }

    return success;
}

DatabaseManager::ClientChangeInfo DatabaseManager::detectClientChanges(const QString& pcId, const ClientInfo& newInfo) {
    ClientChangeInfo changeInfo;
    changeInfo.pcId = pcId;

    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = pcId;

    // DatabaseSchema::SELECT_CLIENT_INFO_FOR_CHANGE_DETECTION 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_CLIENT_INFO_FOR_CHANGE_DETECTION, params)) {
        return changeInfo;
    }

    if (query.next()) {
        QString oldPcName = query.value("pc_name").toString();
        QString oldIp = query.value("ip").toString();
        QString oldOs = query.value("os").toString();

        // 변경 감지
        if (oldPcName != newInfo.pcName) {
            changeInfo.oldPcName = oldPcName;
            changeInfo.newPcName = newInfo.pcName;
            changeInfo.changedFields.append("pc_name");
        }

        if (oldIp != newInfo.ip) {
            changeInfo.oldIp = oldIp;
            changeInfo.newIp = newInfo.ip;
            changeInfo.changedFields.append("ip");
        }

        if (oldOs != newInfo.os) {
            changeInfo.oldOs = oldOs;
            changeInfo.newOs = newInfo.os;
            changeInfo.changedFields.append("os");
        }

        if (changeInfo.hasChanges()) {
            qDebug() << "[DatabaseManager] Client changes detected for PC:" << pcId
                     << "Fields:" << changeInfo.changedFields;

            // 백엔드 알림 신호 발생
            BackendApi::ClientUpdateRequest updateRequest = prepareClientUpdateRequest(changeInfo);
            emit clientInfoChanged(updateRequest);
        }
    }

    return changeInfo;
}

// =================================================================
// 포렌식 데이터 관리 (forensic_info 테이블) - DatabaseSchema 쿼리만 사용
// =================================================================

// =================================================================
// 1단계: getModuleCountForTask() 함수 구현 (DatabaseManager.cpp에 추가)
// =================================================================

int DatabaseManager::getModuleCountForTask(const QString& taskId) {
    if (taskId.isEmpty()) {
        return 0;
    }

    QSqlQuery query(database_);
    QVariantMap params;
    params["task_id"] = taskId;

    // 저장된 고유 모듈 개수 조회 쿼리
    const QString countQuery = R"(
        SELECT COUNT(DISTINCT module_type) as module_count
        FROM forensic_test
        WHERE task_id = :task_id
    )";

    if (!executeSelectQuery(query, countQuery, params)) {
        qWarning() << "[DatabaseManager] Failed to get module count for task:" << taskId;
        return 0;
    }

    if (query.next()) {
        int count = query.value("module_count").toInt();
        qDebug() << QString("[DatabaseManager] Task %1: 저장된 모듈 %2개").arg(taskId).arg(count);
        return count;
    }

    return 0;
}

QStringList DatabaseManager::getMissingModulesForTask(const QString& taskId) {
    // 필요한 모든 모듈 타입 (6개)
    QStringList requiredModules = {"USB_DATA", "BROWSER_DATA", "PREFETCH_DATA", "LNK_DATA", "DELETED_FILES", "MESSENGER_DATA"};

    if (taskId.isEmpty()) {
        return requiredModules; // 전체 반환
    }

    QSqlQuery query(database_);
    QVariantMap params;
    params["task_id"] = taskId;

    // 저장된 모듈 타입들 조회
    const QString storedQuery = R"(
        SELECT DISTINCT module_type
        FROM forensic_test
        WHERE task_id = :task_id
    )";

    QStringList storedModules;
    if (executeSelectQuery(query, storedQuery, params)) {
        while (query.next()) {
            storedModules.append(query.value("module_type").toString());
        }
    }

    // 누락된 모듈 계산
    QStringList missingModules;
    for (const QString& required : requiredModules) {
        if (!storedModules.contains(required)) {
            missingModules.append(required);
        }
    }

    if (!missingModules.isEmpty()) {
        qDebug() << QString("[DatabaseManager] Task %1: 누락된 모듈 %2개 (%3)")
                        .arg(taskId).arg(missingModules.size()).arg(missingModules.join(", "));
    }

    return missingModules;
}

int DatabaseManager::storeForensicData(const ForensicInfo& forensicInfo) {
    if (!forensicInfo.isValid()) {
        qWarning() << "[DatabaseManager] Invalid forensic info";
        return -1;
    }

    if (!validateJsonData(forensicInfo.jsonData)) {
        qWarning() << "[DatabaseManager] Invalid JSON data";
        return -1;
    }

    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = forensicInfo.pcId;
    params["task_id"] = forensicInfo.taskId;
    params["module_type"] = forensicInfo.moduleType;
    params["collection_time"] = forensicInfo.collectionTime;
    params["file_size"] = forensicInfo.fileSize;

    // 암호화 수행
    QString encryptedData = encryptJsonData(forensicInfo.jsonData);

    if (encryptedData.isEmpty()) {
        qCritical() << "[DatabaseManager] 암호화 실패 - 저장 중단";
        return -1;
    }

    params["json_data"] = encryptedData;

    qDebug() << "[DatabaseManager] forensic_test 테이블에 암호화 데이터 저장 중...";

    // DatabaseSchema::INSERT_FORENSIC_INFO 사용 (테이블명은 forensic_test로 변경됨)
    if (!executeQuery(query, DatabaseSchema::INSERT_FORENSIC_INFO, params)) {
        return -1;
    }

    if (query.next()) {
        int forensicId = query.value("id").toInt();
        qDebug() << "[DatabaseManager] Forensic data stored with ID:" << forensicId
                 << "Task:" << forensicInfo.taskId << "Module:" << forensicInfo.moduleType
                 << "(Encrypted)";

        // 기존 시그널들 (유지)
        emit forensicDataStored(forensicId, forensicInfo.taskId);
        emit taskCompleted(forensicInfo.taskId, forensicInfo.pcId, forensicInfo.moduleType);

        // recent_scan 업데이트 (유지)
        updateClientRecentScan(forensicInfo.pcId, forensicInfo.collectionTime);

        // 핵심 수정: 6개 모듈 완료 시에만 백엔드 알림
        int moduleCount = getModuleCountForTask(forensicInfo.taskId);
        if (moduleCount >= 6) {
            // 6개 완료 → 성공 알림
            BackendApi::TaskCompleteRequest completeRequest = prepareTaskCompleteRequest(
                forensicInfo.pcId, forensicInfo.taskId, forensicInfo.moduleType, forensicInfo);
            emit taskCompletionNotification(completeRequest);

            qDebug() << QString("[DatabaseManager] Task %1: 6개 모듈 완료 → 백엔드 성공 알림").arg(forensicInfo.taskId);
        } else {
            qDebug() << QString("[DatabaseManager] Task %1: %2/6개 모듈 저장됨 → 대기 중")
                            .arg(forensicInfo.taskId).arg(moduleCount);
        }

        return forensicId;
    }

    return -1;
}

QList<DatabaseManager::ForensicInfo> DatabaseManager::getForensicDataByPcId(const QString& pcId, int limit, int offset) {
    QList<ForensicInfo> forensicList;

    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = pcId;
    params["limit"] = limit;
    params["offset"] = offset;

    // DatabaseSchema::SELECT_FORENSIC_INFO_BY_PC_ID 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_FORENSIC_INFO_BY_PC_ID, params)) {
        return forensicList;
    }

    while (query.next()) {
        forensicList.append(recordToForensicInfo(query.record()));
    }

    qDebug() << "[DatabaseManager] Retrieved" << forensicList.size() << "forensic records for PC:" << pcId;
    return forensicList;
}

QList<DatabaseManager::ForensicInfo> DatabaseManager::getForensicDataByTaskId(const QString& taskId) {
    QList<ForensicInfo> forensicList;

    QSqlQuery query(database_);
    QVariantMap params;
    params["task_id"] = taskId;

    // DatabaseSchema::SELECT_FORENSIC_INFO_BY_TASK_ID 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_FORENSIC_INFO_BY_TASK_ID, params)) {
        return forensicList;
    }

    while (query.next()) {
        forensicList.append(recordToForensicInfo(query.record()));
    }

    return forensicList;
}

QList<DatabaseManager::ForensicInfo> DatabaseManager::getForensicDataByModuleType(const QString& moduleType, int limit, int offset) {
    QList<ForensicInfo> forensicList;

    QSqlQuery query(database_);
    QVariantMap params;
    params["module_type"] = moduleType;
    params["limit"] = limit;
    params["offset"] = offset;

    // DatabaseSchema::SELECT_FORENSIC_INFO_BY_MODULE_TYPE 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_FORENSIC_INFO_BY_MODULE_TYPE, params)) {
        return forensicList;
    }

    while (query.next()) {
        forensicList.append(recordToForensicInfo(query.record()));
    }

    return forensicList;
}

QList<DatabaseManager::ForensicInfo> DatabaseManager::getLatestForensicData(int limit, int offset) {
    QList<ForensicInfo> forensicList;

    QSqlQuery query(database_);
    QVariantMap params;
    params["limit"] = limit;
    params["offset"] = offset;

    // DatabaseSchema::SELECT_LATEST_FORENSIC_INFO 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_LATEST_FORENSIC_INFO, params)) {
        return forensicList;
    }

    while (query.next()) {
        forensicList.append(recordToForensicInfo(query.record()));
    }

    return forensicList;
}

bool DatabaseManager::isTaskCompleted(const QString& taskId) {
    QSqlQuery query(database_);
    QVariantMap params;
    params["task_id"] = taskId;

    // DatabaseSchema::CHECK_TASK_COMPLETION 사용
    if (!executeSelectQuery(query, DatabaseSchema::CHECK_TASK_COMPLETION, params)) {
        return false;
    }

    if (query.next()) {
        int taskCount = query.value("task_count").toInt();
        return taskCount > 0;
    }

    return false;
}

DatabaseManager::PCTaskStats DatabaseManager::getPCTaskStats(const QString& pcId) {
    PCTaskStats stats;
    stats.pcId = pcId;

    QSqlQuery query(database_);
    QVariantMap params;
    params["pc_id"] = pcId;

    // DatabaseSchema::SELECT_PC_TASK_STATS 사용
    if (!executeSelectQuery(query, DatabaseSchema::SELECT_PC_TASK_STATS, params)) {
        return stats;
    }

    if (query.next()) {
        stats.totalTasks = query.value("total_tasks").toInt();
        stats.moduleTypes = query.value("module_types").toInt();
        stats.latestCollection = query.value("latest_collection").toDateTime();
        stats.totalSize = query.value("total_size").toLongLong();
    }

    return stats;
}

// =================================================================
// 백엔드 API 연동 헬퍼 메서드들
// =================================================================

bool DatabaseManager::isOwnerIdVerified(const QString& pcId) {
    // Owner_ID 검증: PC가 이미 등록되어 있으면 검증 완료로 간주
    ClientInfo clientInfo = getClientInfo(pcId);
    return clientInfo.isValid();
}

BackendApi::ClientUpdateRequest DatabaseManager::prepareClientUpdateRequest(const ClientChangeInfo& changeInfo, const QString& ownerId) {
    BackendApi::ClientUpdateRequest request;

    if (!changeInfo.hasChanges()) {
        return request; // 빈 요청 반환
    }

    // 🔍 현재 DB에서 완전한 정보를 가져옴 (이전 상태)
    ClientInfo currentInfo = getClientInfo(changeInfo.pcId);
    if (!currentInfo.isValid()) {
        qWarning() << "[DatabaseManager] Cannot prepare update request: PC not found in DB:" << changeInfo.pcId;
        return request;
    }

    // 기본 정보 설정
    request.pcId = changeInfo.pcId;
    request.ownerId = ownerId.isEmpty() ? "default" : ownerId;
    request.changeTime = QDateTime::currentDateTime();
    request.changedFields = changeInfo.changedFields;

    // 🔄 이전 정보 (현재 DB 값)
    request.oldPcName = currentInfo.pcName;
    request.oldIp = currentInfo.ip;
    request.oldOs = currentInfo.os;
    request.oldHostname = ""; // hostname은 현재 사용하지 않음

    // 🆕 새로운 정보 (변경된 필드는 새 값, 변경되지 않은 필드는 현재 DB 값)
    request.newPcName = currentInfo.pcName; // 기본값
    request.newIp = currentInfo.ip;         // 기본값
    request.newOs = currentInfo.os;         // 기본값
    request.newHostname = "";               // hostname은 현재 사용하지 않음

    // 변경된 필드만 새 값으로 업데이트
    if (changeInfo.changedFields.contains("pc_name") && !changeInfo.newPcName.isEmpty()) {
        request.newPcName = changeInfo.newPcName;
    }
    if (changeInfo.changedFields.contains("ip") && !changeInfo.newIp.isEmpty()) {
        request.newIp = changeInfo.newIp;
    }
    if (changeInfo.changedFields.contains("os") && !changeInfo.newOs.isEmpty()) {
        request.newOs = changeInfo.newOs;
    }

    qDebug() << QString("[DatabaseManager] ClientUpdateRequest 준비 완료: PC=%1, 변경필드=%2")
                    .arg(request.pcId)
                    .arg(request.changedFields.join(","));
    qDebug() << QString("  이전: name=%1, ip=%2, os=%3")
                    .arg(request.oldPcName).arg(request.oldIp).arg(request.oldOs);
    qDebug() << QString("  새로: name=%1, ip=%2, os=%3")
                    .arg(request.newPcName).arg(request.newIp).arg(request.newOs);

    return request;
}

BackendApi::TaskCompleteRequest DatabaseManager::prepareTaskCompleteRequest(const QString& pcId, const QString& taskId,
                                                                            const QString& moduleType, const ForensicInfo& forensicInfo,
                                                                            const QString& ownerId) {
    BackendApi::TaskCompleteRequest request;

    request.pcId = pcId;
    request.taskId = taskId;
    request.moduleType = moduleType;
    request.ownerId = ownerId;

    request.taskEndTime = forensicInfo.collectionTime;
    request.isSuccess = true;  // 여기까지 왔다면 성공
    request.dataSize = forensicInfo.fileSize;
    request.fileCount = 1;  // JSON 파일 1개

    // 요약 정보 준비
    request.summary["module_type"] = moduleType;
    request.summary["data_size"] = static_cast<double>(forensicInfo.fileSize);
    request.summary["collection_time"] = forensicInfo.collectionTime.toString(Qt::ISODate);

    return request;
}

// =================================================================
// 내부 헬퍼 메서드들
// =================================================================

bool DatabaseManager::executeQuery(QSqlQuery& query, const QString& schemaQuery, const QVariantMap& params) {
    if (!isConnected_ || !database_.isOpen()) {
        qCritical() << "[DatabaseManager] Database not connected";
        return false;
    }

    query.clear();

    if (!query.prepare(schemaQuery)) {
        logDatabaseError("Query prepare", query.lastError());
        return false;
    }

    bindQueryParams(query, params);

    if (!query.exec()) {
        logDatabaseError("Query execution", query.lastError());
        return false;
    }

    return true;
}

bool DatabaseManager::executeSelectQuery(QSqlQuery& query, const QString& schemaQuery, const QVariantMap& params) {
    return executeQuery(query, schemaQuery, params);
}

DatabaseManager::ClientInfo DatabaseManager::recordToClientInfo(const QSqlRecord& record) {
    ClientInfo clientInfo;

    clientInfo.pcId = record.value("pc_id").toString();
    clientInfo.pcName = record.value("pc_name").toString();
    clientInfo.ip = record.value("ip").toString();
    clientInfo.os = record.value("os").toString();
    clientInfo.firstConnect = record.value("first_connect").toDateTime();
    clientInfo.lastConnect = record.value("last_connect").toDateTime();

    // recent_scan은 NULL 가능
    QVariant recentScanValue = record.value("recent_scan");
    if (!recentScanValue.isNull()) {
        clientInfo.recentScan = recentScanValue.toDateTime();
    }

    return clientInfo;
}

DatabaseManager::ForensicInfo DatabaseManager::recordToForensicInfo(const QSqlRecord& record) {
    ForensicInfo forensicInfo;

    forensicInfo.id = record.value("id").toInt();
    forensicInfo.pcId = record.value("pc_id").toString();
    forensicInfo.taskId = record.value("task_id").toString();
    forensicInfo.moduleType = record.value("module_type").toString();
    forensicInfo.collectionTime = record.value("collection_time").toDateTime();
    forensicInfo.fileSize = record.value("file_size").toLongLong();

    // forensic_test 테이블의 json_data는 암호화된 TEXT 데이터
    QString encryptedJsonString = record.value("json_data").toString();
    forensicInfo.jsonData = decryptJsonData(encryptedJsonString);

    if (forensicInfo.jsonData.isEmpty()) {
        qWarning() << "[DatabaseManager] 복호화 실패 또는 빈 JSON 데이터 (ID:" << forensicInfo.id << ")";
    } else {
        qDebug() << "[DatabaseManager] 복호화 성공 (ID:" << forensicInfo.id << ")";
    }

    return forensicInfo;
}

void DatabaseManager::bindQueryParams(QSqlQuery& query, const QVariantMap& params) {
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        query.bindValue(":" + it.key(), it.value());
    }
}

void DatabaseManager::logDatabaseError(const QString& operation, const QSqlError& error) const {
    qCritical() << "[DatabaseManager]" << operation << "failed:" << error.text();
    qCritical() << "   Error type:" << error.type();
    qCritical() << "   Native error code:" << error.nativeErrorCode();
}

QString DatabaseManager::generateUuid() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool DatabaseManager::validateJsonData(const QJsonObject& jsonData) const {
    return !jsonData.isEmpty();
}


// =================================================================
// 🔐 암호화 헬퍼 메서드 구현
// =================================================================

QString DatabaseManager::encryptJsonData(const QJsonObject& jsonData) const {
    if (!config_.enableEncryption) {
        // 암호화 비활성화 시 평문 JSON 반환
        QJsonDocument jsonDoc(jsonData);
        return QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Compact));
    }

    if (config_.encryptionKey.isEmpty()) {
        qWarning() << "[DatabaseManager] 암호화 키가 설정되지 않음 - 평문 저장";
        QJsonDocument jsonDoc(jsonData);
        return QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Compact));
    }

    // JSON을 문자열로 변환
    QJsonDocument jsonDoc(jsonData);
    QString plaintext = QString::fromUtf8(jsonDoc.toJson(QJsonDocument::Compact));

    // AES-256 암호화 수행
    QString encrypted = CryptoUtils::encryptToBase64(plaintext, config_.encryptionKey);

    if (encrypted.isEmpty()) {
        qCritical() << "[DatabaseManager] 암호화 실패 - 평문으로 저장";
        return plaintext;
    }

    qDebug() << "[DatabaseManager] JSON 데이터 암호화 완료";
    return encrypted;
}

QJsonObject DatabaseManager::decryptJsonData(const QString& encryptedData) const {
    if (encryptedData.isEmpty()) {
        qWarning() << "[DatabaseManager] 복호화할 데이터가 비어있음";
        return QJsonObject();
    }

    if (!config_.enableEncryption) {
        // 암호화 비활성화 시 평문 JSON 파싱
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(encryptedData.toUtf8(), &parseError);

        if (parseError.error == QJsonParseError::NoError) {
            return jsonDoc.object();
        } else {
            qWarning() << "[DatabaseManager] JSON 파싱 실패:" << parseError.errorString();
            return QJsonObject();
        }
    }

    if (config_.encryptionKey.isEmpty()) {
        qWarning() << "[DatabaseManager] 암호화 키가 설정되지 않음 - 평문 파싱 시도";
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(encryptedData.toUtf8(), &parseError);

        if (parseError.error == QJsonParseError::NoError) {
            return jsonDoc.object();
        } else {
            qWarning() << "[DatabaseManager] JSON 파싱 실패:" << parseError.errorString();
            return QJsonObject();
        }
    }

    // AES-256 복호화 수행
    QString decrypted = CryptoUtils::decryptFromBase64(encryptedData, config_.encryptionKey);

    if (decrypted.isEmpty()) {
        qCritical() << "[DatabaseManager] 복호화 실패 - 평문 파싱 시도";

        // 복호화 실패 시 평문 파싱 시도 (하위 호환성)
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(encryptedData.toUtf8(), &parseError);

        if (parseError.error == QJsonParseError::NoError) {
            return jsonDoc.object();
        } else {
            qWarning() << "[DatabaseManager] 평문 JSON 파싱도 실패";
            return QJsonObject();
        }
    }

    // 복호화된 문자열을 JSON으로 파싱
    QJsonParseError parseError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(decrypted.toUtf8(), &parseError);

    if (parseError.error == QJsonParseError::NoError) {
        qDebug() << "[DatabaseManager] JSON 데이터 복호화 완료";
        return jsonDoc.object();
    } else {
        qWarning() << "[DatabaseManager] 복호화 후 JSON 파싱 실패:" << parseError.errorString();
        return QJsonObject();
    }
}
