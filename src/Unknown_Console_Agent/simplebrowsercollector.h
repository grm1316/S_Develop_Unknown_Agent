// SimpleBrowserCollector.h - 프로덕션 레벨 브라우저 수집기

#ifndef SIMPLEBROWSERCOLLECTOR_H
#define SIMPLEBROWSERCOLLECTOR_H

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QUuid>
#include <QDebug>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QMutex>

#include <sqlite3.h>
#include <memory>
#include <vector>
#include <map>
#include <string>

class SimpleBrowserCollector : public QObject {
    Q_OBJECT

public:
    // 브라우저 프로필 정보
    struct BrowserProfile {
        QString browserName;
        QString profileName;
        QString basePath;
        bool exists;

        BrowserProfile() : exists(false) {}
    };

    // 수집된 파일 정보
    struct CollectedFile {
        QString fileName;
        QString filePath;
        QString tempPath;
        QString fileType;
        bool success;
        QString error;
        qint64 fileSize;
        QDateTime timestamp;

        // SQLite 전용 필드
        QStringList tableNames;
        int totalRecords;
        QJsonObject sqliteData; // 실제 데이터 내용

        // 바이너리 전용 필드
        QByteArray binaryData;
        QString fileSignature;

        CollectedFile() : success(false), fileSize(0), totalRecords(0), timestamp(QDateTime::currentDateTime()) {}
    };

    // 수집 결과 통계
    struct CollectionStats {
        int totalProfiles;
        int totalFiles;
        int successFiles;
        int failedFiles;
        int totalTables;
        int totalRecords;
        qint64 totalDataSize;
        QDateTime collectionTime;
        QString tempDirectory;
        QStringList errors;

        CollectionStats() : totalProfiles(0), totalFiles(0), successFiles(0),
            failedFiles(0), totalTables(0), totalRecords(0),
            totalDataSize(0), collectionTime(QDateTime::currentDateTime()) {}
    };

    // 수집 설정
    struct CollectionConfig {
        QStringList browserTypes = {"Chrome", "Edge"};
        QStringList fileTypes = {"History", "Login Data", "Cookies", "Web Data", "Sessions", "Cache"};
        bool autoDetectProfiles = true;
        bool includeCache = true;
        bool includeSessions = true;
        int maxRecordsPerTable = 50000; // 프로덕션: 50000개
        int maxCacheFiles = 10;
        qint64 maxCacheFileSize = 10 * 1024 * 1024; // 10MB
        bool extractFullData = true; // 실제 데이터 추출
        bool calculateHashes = true;
    };

private:
    CollectionConfig config_;
    QList<BrowserProfile> discoveredProfiles_;
    QList<CollectedFile> collectedFiles_;
    CollectionStats stats_;

    std::unique_ptr<QTemporaryDir> tempDir_;
    QString tempBasePath_;

    mutable QMutex logMutex_;

    // 브라우저 프로필 탐색
    bool discoverBrowserProfiles();
    QList<BrowserProfile> discoverProfilesForBrowser(const QString& browserName);
    QString getBrowserBasePath(const QString& browserName);

    // 파일 수집 및 복사
    bool collectProfile(const BrowserProfile& profile);
    CollectedFile collectSingleFile(const BrowserProfile& profile, const QString& fileName);
    QString copyFileToTemp(const QString& sourcePath, const QString& fileName);

    // SQLite 파일 처리 (실제 데이터 추출 포함)
    bool processSQLiteFile(const QString& filePath, CollectedFile& fileInfo);
    QStringList discoverSQLiteTables(const QString& filePath);
    std::vector<std::map<std::string, std::string>> extractSQLiteTableData(
        const QString& filePath, const QString& tableName, int maxRecords);
    QJsonObject extractAllSQLiteData(const QString& filePath, const QStringList& tableNames);

    // 바이너리 파일 처리 (향상된 분석)
    bool processBinaryFile(const QString& filePath, CollectedFile& fileInfo);
    QList<CollectedFile> collectSessionFiles(const BrowserProfile& profile);
    QList<CollectedFile> collectCacheFiles(const BrowserProfile& profile);
    QString analyzeFileSignature(const QByteArray& data);

    // 유틸리티
    QString determineFileType(const QString& fileName);
    QString constructFilePath(const BrowserProfile& profile, const QString& fileName);
    bool initializeTempDirectory();
    void updateCollectionStats();
    QString calculateFileHash(const QString& filePath);

    void logError(const QString& context, const QString& message);
    void logDebug(const QString& message);
    void logInfo(const QString& message);

public:
    explicit SimpleBrowserCollector(QObject *parent = nullptr);
    virtual ~SimpleBrowserCollector();

    // 메인 수집 인터페이스
    bool collectAllBrowserData();
    bool collectBrowser(const QString& browserName);
    bool collectSpecificProfile(const BrowserProfile& profile);

    // 설정 관리
    void setCollectionConfig(const CollectionConfig& config);
    CollectionConfig getCollectionConfig() const { return config_; }
    void setMaxRecordsPerTable(int maxRecords) { config_.maxRecordsPerTable = maxRecords; }
    void setExtractFullData(bool extract) { config_.extractFullData = extract; }

    // 결과 접근
    const QList<BrowserProfile>& getDiscoveredProfiles() const { return discoveredProfiles_; }
    const QList<CollectedFile>& getCollectedFiles() const { return collectedFiles_; }
    const CollectionStats& getCollectionStats() const { return stats_; }

    QList<CollectedFile> getFilesByBrowser(const QString& browserName) const;
    QList<CollectedFile> getFilesByType(const QString& fileType) const;
    QList<CollectedFile> getSuccessfulFiles() const;
    QList<CollectedFile> getFailedFiles() const;

    // JSON 변환
    QJsonObject toJsonObject() const;
    QJsonObject toDetailedJsonObject() const; // 실제 데이터 포함
    QJsonObject fileToJsonObject(const CollectedFile& file) const;

    bool saveToJsonFile(const QString& filePath) const;
    bool saveDetailedJsonFile(const QString& filePath) const;
    bool exportSQLiteDataToSeparateFiles(const QString& outputDirectory) const;

    // 출력 및 디버깅
    void printCollectionSummary() const;
    void printDetailedResults() const;
    void printDataSample(const QString& fileName, const QString& tableName, int maxRows = 10) const;
    void printDiscoveredProfiles() const;

    void cleanup();
};

#endif // SIMPLEBROWSERCOLLECTOR_H
