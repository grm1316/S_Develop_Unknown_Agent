// SimpleBrowserCollector.cpp - 프로덕션 레벨 브라우저 수집기 구현

#include "SimpleBrowserCollector.h"
#include <QFile>
#include <QDirIterator>
#include <QCoreApplication>
#include <QMutexLocker>
#include <QThread>

#include <windows.h>
#include <algorithm>

using namespace std;

SimpleBrowserCollector::SimpleBrowserCollector(QObject *parent)
    : QObject{parent} {

    logInfo("초기화 시작...");

    // 프로덕션 레벨 기본 설정
    config_.includeCache = true;
    config_.includeSessions = true;
    config_.extractFullData = true;
    config_.calculateHashes = true;
    config_.maxRecordsPerTable = 50000; // 대용량 데이터 처리

    if (!initializeTempDirectory()) {
        throw runtime_error("Failed to initialize temporary directory");
    }

    logInfo(QString("초기화 완료 - 임시 디렉토리: %1").arg(tempBasePath_));
}

SimpleBrowserCollector::~SimpleBrowserCollector() {
    cleanup();
    logInfo("정리 완료");
}

// =============================================================================
// 메인 수집 로직
// =============================================================================

bool SimpleBrowserCollector::collectAllBrowserData() {
    logInfo("===== Starting Browser Data Collection =====");

    // Initialization
    discoveredProfiles_.clear();
    collectedFiles_.clear();
    stats_ = CollectionStats();
    stats_.collectionTime = QDateTime::currentDateTime();
    stats_.tempDirectory = tempBasePath_;

    // Step 1: Discover browser profiles
    if (!discoverBrowserProfiles()) {
        logError("collectAllBrowserData", "Browser profile discovery failed");
        return false;
    }

    if (discoveredProfiles_.isEmpty()) {
        logError("collectAllBrowserData", "No browser profiles found");
        return false;
    }

    logInfo(QString("Found profiles: %1").arg(discoveredProfiles_.size()));

    // Step 2: Collect data from each profile
    bool overallSuccess = true;
    int profileCount = 0;

    for (const auto& profile : discoveredProfiles_) {
        if (profile.exists) {
            profileCount++;
            logInfo(QString("[%1/%2] Collecting: %3/%4")
                        .arg(profileCount).arg(discoveredProfiles_.size())
                        .arg(profile.browserName).arg(profile.profileName));

            if (!collectProfile(profile)) {
                logError("collectAllBrowserData",
                         QString("Profile collection partial failure: %1/%2").arg(profile.browserName, profile.profileName));
                // FIXED: Individual profile failures don't cause overall failure
                // overallSuccess = false;  // <-- REMOVED THIS LINE
            } else {
                logInfo(QString("Profile collection completed: %1/%2").arg(profile.browserName, profile.profileName));
            }
        }
    }

    // Step 3: Update statistics
    updateCollectionStats();

    logInfo("===== Collection Completed =====");
    logInfo(QString("Total results: success %1, failed %2, data %3MB")
                .arg(stats_.successFiles)
                .arg(stats_.failedFiles)
                .arg(stats_.totalDataSize / 1024 / 1024));

    // FIXED: Return true if any files were successfully collected
    return (stats_.successFiles > 0);
}

bool SimpleBrowserCollector::discoverBrowserProfiles() {
    discoveredProfiles_.clear();

    for (const QString& browserName : config_.browserTypes) {
        auto profiles = discoverProfilesForBrowser(browserName);
        discoveredProfiles_.append(profiles);
    }

    stats_.totalProfiles = discoveredProfiles_.size();
    return !discoveredProfiles_.isEmpty();
}

QList<SimpleBrowserCollector::BrowserProfile> SimpleBrowserCollector::discoverProfilesForBrowser(const QString& browserName) {
    QList<BrowserProfile> profiles;

    QString basePath = getBrowserBasePath(browserName);
    if (basePath.isEmpty()) {
        logError("discoverProfilesForBrowser", QString("%1 브라우저 기본 경로를 찾을 수 없음").arg(browserName));
        return profiles;
    }

    QDir baseDir(basePath);
    if (!baseDir.exists()) {
        logError("discoverProfilesForBrowser", QString("%1 브라우저가 설치되지 않음: %2").arg(browserName, basePath));
        return profiles;
    }

    // 프로필 탐색 (확장된 목록)
    QStringList profileNames = {"Default", "Profile 1", "Profile 2", "Profile 3", "Profile 4", "Profile 5"};

    for (const QString& profileName : profileNames) {
        QString profilePath = basePath + "/" + profileName;
        QDir profileDir(profilePath);

        if (profileDir.exists()) {
            // 프로필 유효성 검사 (주요 파일 존재 확인)
            QString historyPath = profilePath + "/History";
            if (QFile::exists(historyPath)) {
                BrowserProfile profile;
                profile.browserName = browserName;
                profile.profileName = profileName;
                profile.basePath = profilePath;
                profile.exists = true;

                profiles.append(profile);
                logInfo(QString("프로필 발견: %1/%2").arg(browserName, profileName));
            }
        }
    }

    return profiles;
}

QString SimpleBrowserCollector::getBrowserBasePath(const QString& browserName) {
    // Qt 방식으로 LOCALAPPDATA 경로 가져오기
    QString localAppData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);

    // AppLocalDataLocation은 앱별 경로이므로, 상위 폴더로 이동
    QDir dir(localAppData);
    if (dir.cdUp()) {
        localAppData = dir.absolutePath();
    }

    // 환경변수 백업
    if (localAppData.isEmpty()) {
        localAppData = qgetenv("LOCALAPPDATA");
    }

    if (localAppData.isEmpty()) {
        logError("getBrowserBasePath", "LOCALAPPDATA 경로를 가져올 수 없음");
        return QString();
    }

    if (browserName == "Chrome") {
        return localAppData + "/Google/Chrome/User Data";
    } else if (browserName == "Edge") {
        return localAppData + "/Microsoft/Edge/User Data";
    }

    logError("getBrowserBasePath", QString("지원하지 않는 브라우저: %1").arg(browserName));
    return QString();
}

// =============================================================================
// 파일 수집 및 복사
// =============================================================================

bool SimpleBrowserCollector::collectProfile(const BrowserProfile& profile) {
    bool success = true;
    int fileCount = 0;

    for (const QString& fileName : config_.fileTypes) {
        fileCount++;

        // 세션은 폴더이므로 별도 처리
        if (fileName == "Sessions") {
            if (config_.includeSessions) {
                logInfo(QString("[%1] Sessions 수집 시작...").arg(fileCount));
                auto sessionFiles = collectSessionFiles(profile);
                collectedFiles_.append(sessionFiles);
                logInfo(QString("Sessions: %1개 파일 수집됨").arg(sessionFiles.size()));
            }
            continue;
        }

        // 캐시도 폴더이므로 별도 처리
        if (fileName == "Cache") {
            if (config_.includeCache) {
                logInfo(QString("[%1] Cache 수집 시작...").arg(fileCount));
                auto cacheFiles = collectCacheFiles(profile);
                collectedFiles_.append(cacheFiles);
                logInfo(QString("Cache: %1개 파일 수집됨").arg(cacheFiles.size()));
            }
            continue;
        }

        // 일반 파일 수집
        logInfo(QString("[%1] %2 수집 시작...").arg(fileCount).arg(fileName));
        CollectedFile fileInfo = collectSingleFile(profile, fileName);
        collectedFiles_.append(fileInfo);

        if (!fileInfo.success) {
            success = false;
            logError("collectProfile", QString("%1 수집 실패: %2").arg(fileName, fileInfo.error));
        } else {
            logInfo(QString("%1 수집 완료: %2 bytes").arg(fileName).arg(fileInfo.fileSize));
        }
    }

    return success;
}

SimpleBrowserCollector::CollectedFile SimpleBrowserCollector::collectSingleFile(const BrowserProfile& profile, const QString& fileName) {
    CollectedFile fileInfo;
    fileInfo.fileName = fileName;
    fileInfo.timestamp = QDateTime::currentDateTime();

    // 1. 원본 파일 경로 구성
    fileInfo.filePath = constructFilePath(profile, fileName);

    QFileInfo originalFile(fileInfo.filePath);
    if (!originalFile.exists()) {
        fileInfo.error = QString("파일이 존재하지 않음: %1").arg(fileInfo.filePath);
        return fileInfo;
    }

    fileInfo.fileSize = originalFile.size();
    fileInfo.fileType = determineFileType(fileName);

    logDebug(QString("수집 중: %1 (%2, %3 bytes)").arg(fileName, fileInfo.fileType).arg(fileInfo.fileSize));

    // 2. 파일 복사
    fileInfo.tempPath = copyFileToTemp(fileInfo.filePath, fileName);
    if (fileInfo.tempPath.isEmpty()) {
        fileInfo.error = "파일 복사 실패";
        return fileInfo;
    }

    // 3. 해시 계산
    if (config_.calculateHashes) {
        fileInfo.fileSignature = calculateFileHash(fileInfo.tempPath);
    }

    // 4. 파일 타입별 처리
    if (fileInfo.fileType == "sqlite") {
        if (!processSQLiteFile(fileInfo.tempPath, fileInfo)) {
            logError("collectSingleFile", QString("SQLite 처리 실패: %1").arg(fileName));
        }
    } else if (fileInfo.fileType == "binary") {
        if (!processBinaryFile(fileInfo.tempPath, fileInfo)) {
            logError("collectSingleFile", QString("바이너리 파일 처리 실패: %1").arg(fileName));
        }
    }

    fileInfo.success = true;
    return fileInfo;
}

QString SimpleBrowserCollector::copyFileToTemp(const QString& sourcePath, const QString& fileName) {
    QString tempFilePath = tempBasePath_ + "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "_" + fileName;

    QFileInfo sourceInfo(sourcePath);
    qint64 originalSize = sourceInfo.size();

    if (QFile::copy(sourcePath, tempFilePath)) {
        // 복사 후 크기 검증
        QFileInfo copiedInfo(tempFilePath);
        qint64 copiedSize = copiedInfo.size();

        if (originalSize != copiedSize) {
            logError("copyFileToTemp", QString("복사 후 크기 불일치: %1 (원본:%2, 복사본:%3)")
                                           .arg(fileName).arg(originalSize).arg(copiedSize));
        }

        return tempFilePath;
    }

    logError("copyFileToTemp", QString("복사 실패: %1").arg(sourcePath));
    return QString();
}

// =============================================================================
// SQLite 파일 처리 (실제 데이터 추출)
// =============================================================================

bool SimpleBrowserCollector::processSQLiteFile(const QString& filePath, CollectedFile& fileInfo) {
    try {
        // 테이블 목록 탐색
        fileInfo.tableNames = discoverSQLiteTables(filePath);

        if (fileInfo.tableNames.isEmpty()) {
            fileInfo.error = "SQLite 테이블을 찾을 수 없음";
            return false;
        }

        logDebug(QString("발견된 테이블: %1개").arg(fileInfo.tableNames.size()));

        // 실제 데이터 추출
        if (config_.extractFullData) {
            fileInfo.sqliteData = extractAllSQLiteData(filePath, fileInfo.tableNames);

            // 총 레코드 수 계산
            fileInfo.totalRecords = 0;
            for (auto it = fileInfo.sqliteData.begin(); it != fileInfo.sqliteData.end(); ++it) {
                if (it.value().isArray()) {
                    fileInfo.totalRecords += it.value().toArray().size();
                }
            }
        } else {
            // 통계용 샘플링만
            fileInfo.totalRecords = 0;
            for (const QString& tableName : fileInfo.tableNames) {
                auto tableData = extractSQLiteTableData(filePath, tableName, 10);
                fileInfo.totalRecords += tableData.size();
            }
        }

        logDebug(QString("총 레코드: %1개").arg(fileInfo.totalRecords));
        return true;

    } catch (const exception& e) {
        fileInfo.error = QString("SQLite 처리 예외: %1").arg(e.what());
        return false;
    }
}

QStringList SimpleBrowserCollector::discoverSQLiteTables(const QString& filePath) {
    QStringList tables;

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(filePath.toStdString().c_str(), &db, SQLITE_OPEN_READONLY, nullptr);

    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return tables;
    }

    const char* query = "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' ORDER BY name";
    sqlite3_stmt* stmt = nullptr;

    rc = sqlite3_prepare_v2(db, query, -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* tableName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (tableName) {
                tables.append(QString::fromUtf8(tableName));
            }
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return tables;
}

std::vector<std::map<std::string, std::string>> SimpleBrowserCollector::extractSQLiteTableData(
    const QString& filePath, const QString& tableName, int maxRecords) {

    vector<map<string, string>> data;

    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(filePath.toStdString().c_str(), &db, SQLITE_OPEN_READONLY, nullptr);

    if (rc != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return data;
    }

    // 안전한 쿼리 생성 (LIMIT 추가)
    string query = QString("SELECT * FROM `%1` LIMIT %2").arg(tableName).arg(maxRecords).toStdString();
    sqlite3_stmt* stmt = nullptr;

    rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW && static_cast<int>(data.size()) < maxRecords) {
            map<string, string> row;

            int columnCount = sqlite3_column_count(stmt);
            for (int i = 0; i < columnCount; i++) {
                string columnName = sqlite3_column_name(stmt, i);

                // 다양한 데이터 타입 처리
                int columnType = sqlite3_column_type(stmt, i);
                string columnValue;

                switch (columnType) {
                case SQLITE_INTEGER:
                    columnValue = to_string(sqlite3_column_int64(stmt, i));
                    break;
                case SQLITE_FLOAT:
                    columnValue = to_string(sqlite3_column_double(stmt, i));
                    break;
                case SQLITE_TEXT:
                case SQLITE_BLOB: {
                    const char* textValue = reinterpret_cast<const char*>(sqlite3_column_text(stmt, i));
                    columnValue = textValue ? textValue : "NULL";
                    break;
                }
                case SQLITE_NULL:
                default:
                    columnValue = "NULL";
                    break;
                }

                row[columnName] = columnValue;
            }

            data.push_back(row);
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return data;
}

QJsonObject SimpleBrowserCollector::extractAllSQLiteData(const QString& filePath, const QStringList& tableNames) {
    QJsonObject allData;

    for (const QString& tableName : tableNames) {
        auto tableData = extractSQLiteTableData(filePath, tableName, config_.maxRecordsPerTable);

        QJsonArray rowsArray;
        for (const auto& row : tableData) {
            QJsonObject rowObject;
            for (const auto& pair : row) {
                rowObject[QString::fromStdString(pair.first)] = QString::fromStdString(pair.second);
            }
            rowsArray.append(rowObject);
        }

        allData[tableName] = rowsArray;

        if (!tableData.empty()) {
            logDebug(QString("테이블 %1: %2개 레코드 추출").arg(tableName).arg(tableData.size()));
        }
    }

    return allData;
}

// =============================================================================
// 바이너리 파일 처리 (향상된 분석)
// =============================================================================

bool SimpleBrowserCollector::processBinaryFile(const QString& filePath, CollectedFile& fileInfo) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        fileInfo.error = "바이너리 파일을 열 수 없음";
        return false;
    }

    // 파일 크기 재확인
    qint64 actualSize = file.size();
    if (fileInfo.fileSize != actualSize) {
        fileInfo.fileSize = actualSize;
    }

    // 작은 파일만 메모리에 로드 (5MB 미만)
    if (actualSize > 0 && actualSize < 5 * 1024 * 1024) {
        fileInfo.binaryData = file.readAll();
        fileInfo.fileSignature = analyzeFileSignature(fileInfo.binaryData);
    } else if (actualSize >= 16) {
        // 큰 파일은 헤더만 분석
        QByteArray header = file.read(64);
        fileInfo.fileSignature = analyzeFileSignature(header);
    }

    file.close();
    return true;
}

QList<SimpleBrowserCollector::CollectedFile> SimpleBrowserCollector::collectSessionFiles(const BrowserProfile& profile) {
    QList<CollectedFile> sessionFiles;

    QString sessionDirPath = profile.basePath + "/Sessions";
    QDir sessionDir(sessionDirPath);

    if (!sessionDir.exists()) {
        logDebug(QString("Sessions 폴더 없음: %1").arg(sessionDirPath));
        return sessionFiles;
    }

    logDebug(QString("Sessions 폴더 발견: %1").arg(sessionDirPath));

    // 세션 파일들 수집 (시간순 정렬)
    sessionDir.setFilter(QDir::Files);
    sessionDir.setSorting(QDir::Time | QDir::Reversed);
    QFileInfoList fileList = sessionDir.entryInfoList();

    logDebug(QString("세션 파일 %1개 발견").arg(fileList.size()));

    int count = 0;
    const int MAX_SESSION_FILES = 15;

    for (const QFileInfo& fileInfo : fileList) {
        if (count >= MAX_SESSION_FILES) break;

        QString fullPath = fileInfo.absoluteFilePath();
        CollectedFile sessionFile;
        sessionFile.fileName = QString("Session_%1").arg(fileInfo.fileName());
        sessionFile.filePath = fullPath;
        sessionFile.fileType = "binary";
        sessionFile.fileSize = fileInfo.size();
        sessionFile.timestamp = QDateTime::currentDateTime();

        logDebug(QString("세션 파일 처리: %1 (%2 bytes)").arg(fileInfo.fileName()).arg(sessionFile.fileSize));

        // 파일 복사
        sessionFile.tempPath = copyFileToTemp(fullPath, sessionFile.fileName);
        if (!sessionFile.tempPath.isEmpty()) {
            if (processBinaryFile(sessionFile.tempPath, sessionFile)) {
                sessionFile.success = true;
            } else {
                sessionFile.success = false;
                sessionFile.error = "세션 파일 처리 실패";
            }
        } else {
            sessionFile.success = false;
            sessionFile.error = "파일 복사 실패";
        }

        sessionFiles.append(sessionFile);
        count++;
    }

    return sessionFiles;
}

QList<SimpleBrowserCollector::CollectedFile> SimpleBrowserCollector::collectCacheFiles(const BrowserProfile& profile) {
    QList<CollectedFile> cacheFiles;

    // Chrome과 Edge의 여러 캐시 경로들 시도
    QStringList cachePaths = {
        profile.basePath + "/Cache/Cache_Data",
        profile.basePath + "/Cache",
        profile.basePath + "/Code Cache",
        profile.basePath + "/GPUCache"
    };

    logDebug("Cache 경로 탐색 시작...");

    for (const QString& cachePath : cachePaths) {
        QDir cacheDir(cachePath);

        if (!cacheDir.exists()) {
            continue;
        }

        logDebug(QString("Cache 경로 발견: %1").arg(cachePath));

        // 캐시 파일 목록 가져오기 (크기순 정렬)
        cacheDir.setFilter(QDir::Files);
        cacheDir.setSorting(QDir::Size | QDir::Reversed);
        QFileInfoList fileList = cacheDir.entryInfoList();

        logDebug(QString("Cache 파일 총 %1개 발견").arg(fileList.size()));

        int count = 0;

        for (const QFileInfo& fileInfo : fileList) {
            if (count >= config_.maxCacheFiles) break;

            // 크기 제한 확인
            if (fileInfo.size() > config_.maxCacheFileSize || fileInfo.size() == 0) {
                continue;
            }

            QString fullPath = fileInfo.absoluteFilePath();
            CollectedFile cacheFile;
            cacheFile.fileName = QString("Cache_%1").arg(fileInfo.fileName());
            cacheFile.filePath = fullPath;
            cacheFile.fileType = "binary";
            cacheFile.fileSize = fileInfo.size();
            cacheFile.timestamp = QDateTime::currentDateTime();

            // 파일 복사 및 처리
            cacheFile.tempPath = copyFileToTemp(fullPath, cacheFile.fileName);
            if (!cacheFile.tempPath.isEmpty()) {
                if (processBinaryFile(cacheFile.tempPath, cacheFile)) {
                    cacheFile.success = true;
                    cacheFiles.append(cacheFile);
                    count++;
                }
            }
        }
    }

    return cacheFiles;
}

QString SimpleBrowserCollector::analyzeFileSignature(const QByteArray& data) {
    if (data.isEmpty()) return "EMPTY";

    QString hex = data.left(16).toHex().toLower();

    // 알려진 시그니처 확인
    if (hex.startsWith("534e5353")) return "SNSS";           // Chrome Session
    if (hex.startsWith("ffd8ff")) return "JPEG";             // JPEG 이미지
    if (hex.startsWith("89504e47")) return "PNG";            // PNG 이미지
    if (hex.startsWith("47494638")) return "GIF";            // GIF 이미지
    if (hex.startsWith("504b0304")) return "ZIP";            // ZIP/압축
    if (hex.startsWith("25504446")) return "PDF";            // PDF

    return QString("BINARY_%1").arg(hex.left(8));
}

// =============================================================================
// 유틸리티 함수들
// =============================================================================

QString SimpleBrowserCollector::determineFileType(const QString& fileName) {
    QStringList sqliteFiles = {"History", "Login Data", "Cookies", "Web Data"};
    QStringList binaryFiles = {"Sessions", "Cache"};

    if (sqliteFiles.contains(fileName)) {
        return "sqlite";
    } else if (binaryFiles.contains(fileName) || fileName.startsWith("Session_") || fileName.startsWith("Cache_")) {
        return "binary";
    }

    return "unknown";
}

QString SimpleBrowserCollector::constructFilePath(const BrowserProfile& profile, const QString& fileName) {
    if (fileName == "Login Data") {
        return profile.basePath + "/Login Data";
    } else if (fileName == "Web Data") {
        return profile.basePath + "/Web Data";
    } else if (fileName == "Cookies") {
        return profile.basePath + "/Network/Cookies";
    } else {
        return profile.basePath + "/" + fileName;
    }
}

bool SimpleBrowserCollector::initializeTempDirectory() {
    tempDir_ = std::make_unique<QTemporaryDir>();

    if (!tempDir_->isValid()) {
        return false;
    }

    tempBasePath_ = tempDir_->path();
    return true;
}

void SimpleBrowserCollector::updateCollectionStats() {
    stats_.totalFiles = collectedFiles_.size();
    stats_.successFiles = 0;
    stats_.failedFiles = 0;
    stats_.totalTables = 0;
    stats_.totalRecords = 0;
    stats_.totalDataSize = 0;
    stats_.errors.clear();

    for (const auto& file : collectedFiles_) {
        if (file.success) {
            stats_.successFiles++;
            stats_.totalTables += file.tableNames.size();
            stats_.totalRecords += file.totalRecords;
            stats_.totalDataSize += file.fileSize;
        } else {
            stats_.failedFiles++;
            if (!file.error.isEmpty()) {
                stats_.errors.append(QString("%1: %2").arg(file.fileName, file.error));
            }
        }
    }
}

QString SimpleBrowserCollector::calculateFileHash(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    return hash.result().toHex();
}

// =============================================================================
// JSON 변환 및 출력
// =============================================================================

QJsonObject SimpleBrowserCollector::toJsonObject() const {
    QJsonObject root;

    // 기본 정보
    root["collection_time"] = stats_.collectionTime.toString(Qt::ISODate);
    root["temp_directory"] = stats_.tempDirectory;

    // 통계
    QJsonObject statsObj;
    statsObj["total_profiles"] = stats_.totalProfiles;
    statsObj["total_files"] = stats_.totalFiles;
    statsObj["success_files"] = stats_.successFiles;
    statsObj["failed_files"] = stats_.failedFiles;
    statsObj["total_tables"] = stats_.totalTables;
    statsObj["total_records"] = stats_.totalRecords;
    statsObj["total_data_size"] = static_cast<qint64>(stats_.totalDataSize);

    if (!stats_.errors.isEmpty()) {
        QJsonArray errorsArray;
        for (const QString& error : stats_.errors) {
            errorsArray.append(error);
        }
        statsObj["errors"] = errorsArray;
    }

    root["statistics"] = statsObj;

    // 발견된 프로필들
    QJsonArray profilesArray;
    for (const auto& profile : discoveredProfiles_) {
        QJsonObject profileObj;
        profileObj["browser_name"] = profile.browserName;
        profileObj["profile_name"] = profile.profileName;
        profileObj["base_path"] = profile.basePath;
        profileObj["exists"] = profile.exists;
        profilesArray.append(profileObj);
    }
    root["discovered_profiles"] = profilesArray;

    // 수집된 파일들
    QJsonArray filesArray;
    for (const auto& file : collectedFiles_) {
        filesArray.append(fileToJsonObject(file));
    }
    root["collected_files"] = filesArray;

    return root;
}

QJsonObject SimpleBrowserCollector::toDetailedJsonObject() const {
    QJsonObject root = toJsonObject();

    // 실제 데이터 포함된 상세 버전
    QJsonArray detailedFilesArray;
    for (const auto& file : collectedFiles_) {
        QJsonObject fileObj = fileToJsonObject(file);

        // SQLite 실제 데이터 추가
        if (file.fileType == "sqlite" && !file.sqliteData.isEmpty()) {
            fileObj["sqlite_data"] = file.sqliteData;
        }

        detailedFilesArray.append(fileObj);
    }

    root["detailed_files"] = detailedFilesArray;
    return root;
}

QJsonObject SimpleBrowserCollector::fileToJsonObject(const CollectedFile& file) const {
    QJsonObject fileObj;
    fileObj["file_name"] = file.fileName;
    fileObj["file_path"] = file.filePath;
    fileObj["temp_path"] = file.tempPath;
    fileObj["file_type"] = file.fileType;
    fileObj["success"] = file.success;
    fileObj["error"] = file.error;
    fileObj["file_size"] = static_cast<qint64>(file.fileSize);
    fileObj["timestamp"] = file.timestamp.toString(Qt::ISODate);

    if (!file.fileSignature.isEmpty()) {
        fileObj["file_signature"] = file.fileSignature;
    }

    if (file.fileType == "sqlite") {
        QJsonArray tablesArray;
        for (const QString& tableName : file.tableNames) {
            tablesArray.append(tableName);
        }
        fileObj["table_names"] = tablesArray;
        fileObj["total_records"] = file.totalRecords;
    }

    return fileObj;
}

bool SimpleBrowserCollector::saveToJsonFile(const QString& filePath) const {
    QJsonDocument doc(toJsonObject());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
}

bool SimpleBrowserCollector::saveDetailedJsonFile(const QString& filePath) const {
    QJsonDocument doc(toDetailedJsonObject());

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
}

// =============================================================================
// 출력 및 디버깅
// =============================================================================

void SimpleBrowserCollector::printCollectionSummary() const {
    QMutexLocker locker(&logMutex_);

    qDebug() << "==========================================";
    qDebug() << "      브라우저 데이터 수집 결과 요약";
    qDebug() << "==========================================";
    qDebug() << "수집 시간:" << stats_.collectionTime.toString("yyyy-MM-dd hh:mm:ss");
    qDebug() << "임시 디렉토리:" << stats_.tempDirectory;
    qDebug() << "";
    qDebug() << "통계:";
    qDebug() << "  발견된 프로필:" << stats_.totalProfiles << "개";
    qDebug() << "  처리된 파일:" << stats_.totalFiles << "개";
    qDebug() << "  성공:" << stats_.successFiles << "개";
    qDebug() << "  실패:" << stats_.failedFiles << "개";
    qDebug() << "  총 테이블:" << stats_.totalTables << "개";
    qDebug() << "  총 레코드:" << stats_.totalRecords << "개";
    qDebug() << "  총 데이터 크기:" << (stats_.totalDataSize / 1024 / 1024) << "MB";

    if (!stats_.errors.isEmpty()) {
        qDebug() << "";
        qDebug() << "오류 목록:";
        for (const QString& error : stats_.errors) {
            qDebug() << "  -" << error;
        }
    }

    qDebug() << "==========================================";
}

void SimpleBrowserCollector::printDataSample(const QString& fileName, const QString& tableName, int maxRows) const {
    for (const auto& file : collectedFiles_) {
        if (file.fileName == fileName && file.success && !file.sqliteData.isEmpty()) {
            if (file.sqliteData.contains(tableName)) {
                QJsonArray tableData = file.sqliteData[tableName].toArray();

                qDebug() << QString("=== %1.%2 데이터 샘플 (최대 %3개) ===").arg(fileName, tableName).arg(maxRows);

                int count = 0;
                for (const auto& value : tableData) {
                    if (count >= maxRows) break;

                    QJsonObject row = value.toObject();
                    QStringList rowData;
                    for (auto it = row.begin(); it != row.end(); ++it) {
                        rowData.append(QString("%1=%2").arg(it.key(), it.value().toString()));
                    }

                    qDebug() << QString("[%1] %2").arg(count + 1).arg(rowData.join(", "));
                    count++;
                }

                qDebug() << QString("총 %1개 레코드 중 %2개 표시").arg(tableData.size()).arg(count);
                break;
            }
        }
    }
}

void SimpleBrowserCollector::cleanup() {
    tempDir_.reset();
}

// =============================================================================
// 설정 및 접근 메서드들
// =============================================================================

void SimpleBrowserCollector::setCollectionConfig(const CollectionConfig& config) {
    config_ = config;
}

QList<SimpleBrowserCollector::CollectedFile> SimpleBrowserCollector::getFilesByBrowser(const QString& browserName) const {
    QList<CollectedFile> result;

    for (const auto& file : collectedFiles_) {
        if (file.filePath.contains(browserName, Qt::CaseInsensitive)) {
            result.append(file);
        }
    }

    return result;
}

QList<SimpleBrowserCollector::CollectedFile> SimpleBrowserCollector::getFilesByType(const QString& fileType) const {
    QList<CollectedFile> result;

    for (const auto& file : collectedFiles_) {
        if (file.fileType == fileType) {
            result.append(file);
        }
    }

    return result;
}

QList<SimpleBrowserCollector::CollectedFile> SimpleBrowserCollector::getSuccessfulFiles() const {
    QList<CollectedFile> result;

    for (const auto& file : collectedFiles_) {
        if (file.success) {
            result.append(file);
        }
    }

    return result;
}

// =============================================================================
// 로깅 함수들
// =============================================================================

void SimpleBrowserCollector::logError(const QString& context, const QString& message) {
    QMutexLocker locker(&logMutex_);
    qWarning() << QString("[SimpleBrowserCollector] ERROR [%1]: %2").arg(context, message);
}

void SimpleBrowserCollector::logDebug(const QString& message) {
    QMutexLocker locker(&logMutex_);
    qDebug() << QString("[SimpleBrowserCollector] DEBUG: %1").arg(message);
}

void SimpleBrowserCollector::logInfo(const QString& message) {
    QMutexLocker locker(&logMutex_);
    qDebug() << QString("[SimpleBrowserCollector] INFO: %1").arg(message);
}
