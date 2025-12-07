#ifndef MESSENGERCOLLECTOR_H
#define MESSENGERCOLLECTOR_H

#include "pch.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

// 메신저 타입 열거형
enum class MessengerType {
    KakaoTalk,
    Line,
    Discord,
    Telegram,
    WhatsApp,
    Unknown
};

// 파일 타입 분류
enum class FileType {
    ChatDatabase,     // 채팅 DB 파일
    MediaFile,        // 이미지, 동영상 등
    CacheFile,        // 캐시 파일
    ConfigFile,       // 설정 파일
    LogFile,          // 로그 파일
    DownloadFile,     // 다운로드 파일
    Unknown
};

// 메신저 파일 정보 구조체
struct MessengerFileInfo {
    MessengerType messenger;
    FileType fileType;
    QString fileName;
    QString filePath;
    QString relativePath;
    QDateTime lastModified;
    QDateTime created;
    qint64 fileSize;
    QString fileHash;

    QString getMessengerName() const;
    QString getFileTypeName() const;
    bool isValidFile() const;
};

// 수집 옵션 구조체
struct CollectionOptions {
    bool includeCache = true;
    bool includeDownloads = true;
    bool includeLogs = false;
    bool calculateHash = false;
    bool deepScan = true;
    QStringList fileExtensions;
    qint64 maxFileSize = -1;
    QDateTime fromDate;
    QDateTime toDate;
};

// 수집 결과 통계
struct CollectionStats {
    int totalFiles = 0;
    qint64 totalSize = 0;
    int errorCount = 0;
    QDateTime scanStartTime;
    QDateTime scanEndTime;
    QStringList errors;
    QMap<MessengerType, int> messengerCounts;
    QMap<FileType, int> fileTypeCounts;
};

class MessengerCollector : public QObject {
    Q_OBJECT

public:
    explicit MessengerCollector(QObject *parent = nullptr);
    ~MessengerCollector();

    // 메인 수집 함수
    bool collectMessengerForensicsData();
    bool collectMessenger(MessengerType type);

    // 결과 조회
    const QList<MessengerFileInfo>& getFileList() const;
    QList<MessengerFileInfo> getFilesByMessenger(MessengerType type) const;
    QList<MessengerFileInfo> getFilesByType(FileType type) const;
    CollectionStats getStats() const;

    // JSON 변환 메서드
    QJsonObject toJsonObject() const;

    // 결과 출력
    void printCollectionSummary() const;
    void printDetailedResults() const;
    void printMessengerSummary(MessengerType messenger) const;

    // 결과 저장
    bool saveToJson(const QString& filePath) const;
    bool saveToCsv(const QString& filePath) const;

    // 설정
    void setOptions(const CollectionOptions& options);
    void addCustomPath(MessengerType messenger, const QString& path);

private:
    // 개별 메신저 수집 함수들
    void collectKakaoTalk();
    void collectLine();
    void collectDiscord();
    void collectTelegram();
    void collectWhatsApp();

    // === JSON 헬퍼 함수들 ===
    QJsonObject messengerFileInfoToJson(const MessengerFileInfo& info) const;
    QJsonObject collectionStatsToJson(const CollectionStats& stats) const;
    QJsonObject collectionOptionsToJson(const CollectionOptions& options) const;

    // 도우미 함수들
    void scanDirectory(const QString& dirPath, MessengerType messenger);
    bool isValidFile(const QFileInfo& fileInfo) const;
    FileType determineFileType(const QString& filePath, MessengerType messenger) const;
    QString calculateFileHash(const QString& filePath) const;

    // 경로 관리
    QStringList getMessengerPaths(MessengerType messenger) const;
    QString getUserProfile() const;
    QString getAppDataLocal() const;
    QString getAppDataRoaming() const;
    QString getDocuments() const;
    QString getDownloads() const;

    // 유틸리티
    void logError(const QString& error);
    bool hasPermission(const QString& path) const;
    void updateStats();
    QString formatFileSize(qint64 bytes) const;
    QString formatDuration(const QDateTime& start, const QDateTime& end) const;

private:
    QList<MessengerFileInfo> m_fileList;
    CollectionStats m_stats;
    CollectionOptions m_options;
    QMap<MessengerType, QStringList> m_customPaths;
};

// 전역 헬퍼 함수들
QString messengerTypeToString(MessengerType type);
MessengerType stringToMessengerType(const QString& str);
QString fileTypeToString(FileType type);
FileType stringToFileType(const QString& str);

#endif // MESSENGERCOLLECTOR_H
