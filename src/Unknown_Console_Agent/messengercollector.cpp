#include "messengercollector.h"

// MessengerFileInfo 구현
QString MessengerFileInfo::getMessengerName() const {
    return messengerTypeToString(messenger);
}

QString MessengerFileInfo::getFileTypeName() const {
    return fileTypeToString(fileType);
}

bool MessengerFileInfo::isValidFile() const {
    return !fileName.isEmpty() && !filePath.isEmpty() && fileSize >= 0;
}

// MessengerCollector 구현
MessengerCollector::MessengerCollector(QObject *parent)
    : QObject(parent)
{
    // 기본 옵션 설정
    m_options.includeCache = true;
    m_options.includeDownloads = true;
    m_options.includeLogs = false;
    m_options.calculateHash = false;
    m_options.deepScan = true;
    m_options.maxFileSize = -1;
}

MessengerCollector::~MessengerCollector() {
    m_fileList.clear();
}

bool MessengerCollector::collectMessengerForensicsData() {
    qDebug() << "메신저 포렌식 데이터 수집 시작...";

    m_fileList.clear();
    m_stats = CollectionStats();
    m_stats.scanStartTime = QDateTime::currentDateTime();

    try {
        // 지원하는 모든 메신저 수집
        collectKakaoTalk();
        collectLine();
        collectDiscord();
        collectTelegram();
        collectWhatsApp();

        m_stats.scanEndTime = QDateTime::currentDateTime();
        updateStats();

        qDebug() << QString("메신저 데이터 수집 완료: %1개 파일").arg(m_fileList.size());
        return true;

    } catch (const std::exception& e) {
        logError(QString("수집 중 오류 발생: %1").arg(e.what()));
        return false;
    }
}

bool MessengerCollector::collectMessenger(MessengerType type) {
    qDebug() << QString("%1 데이터 수집 중...").arg(messengerTypeToString(type));

    int initialCount = m_fileList.size();

    try {
        switch (type) {
        case MessengerType::KakaoTalk:
            collectKakaoTalk();
            break;
        case MessengerType::Line:
            collectLine();
            break;
        case MessengerType::Discord:
            collectDiscord();
            break;
        case MessengerType::Telegram:
            collectTelegram();
            break;
        case MessengerType::WhatsApp:
            collectWhatsApp();
            break;
        default:
            logError("지원하지 않는 메신저 타입");
            return false;
        }

        int foundFiles = m_fileList.size() - initialCount;
        qDebug() << QString("%1 파일 %2개 발견").arg(messengerTypeToString(type)).arg(foundFiles);
        return true;

    } catch (const std::exception& e) {
        logError(QString("%1 수집 오류: %2").arg(messengerTypeToString(type)).arg(e.what()));
        return false;
    }
}

void MessengerCollector::collectKakaoTalk() {
    QStringList paths;
    QString userProfile = getUserProfile();

    // 기본 설치 경로
    paths << userProfile + "/AppData/Local/Kakao/KakaoTalk";

    // 다운로드 폴더
    if (m_options.includeDownloads) {
        paths << getDownloads() + "/KakaoTalk";
    }

    // 캐시 경로
    if (m_options.includeCache) {
        paths << userProfile + "/AppData/Local/Kakao/KakaoTalk/Cache";
    }

    // 로그 파일
    if (m_options.includeLogs) {
        paths << userProfile + "/AppData/Local/Kakao/KakaoTalk/Logs";
    }

    // 채팅 데이터베이스 경로 (사용자별)
    QString chatDbBase = userProfile + "/AppData/Roaming/Kakao/KakaoTalk/Users";
    QDir usersDir(chatDbBase);
    if (usersDir.exists()) {
        QStringList userList = usersDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& userId : userList) {
            paths << chatDbBase + "/" + userId + "/Chats";
            paths << chatDbBase + "/" + userId;
        }
    }

    // 커스텀 경로 추가
    paths << getMessengerPaths(MessengerType::KakaoTalk);

    // 각 경로 스캔
    for (const QString& path : paths) {
        if (QDir(path).exists()) {
            scanDirectory(path, MessengerType::KakaoTalk);
        }
    }
}

void MessengerCollector::collectLine() {
    QStringList paths;
    QString userProfile = getUserProfile();

    // 기본 설치 경로
    paths << userProfile + "/AppData/Local/LINE";

    // 다운로드 폴더
    if (m_options.includeDownloads) {
        paths << getDownloads() + "/Line";
    }

    // 캐시 경로
    if (m_options.includeCache) {
        paths << userProfile + "/AppData/Local/LINE/Cache";
    }

    // 로그 파일
    if (m_options.includeLogs) {
        paths << userProfile + "/AppData/Local/LINE/Logs";
    }

    // 채팅 데이터베이스
    paths << getDocuments() + "/LINE";
    paths << userProfile + "/AppData/Roaming/LINE";

    // 커스텀 경로 추가
    paths << getMessengerPaths(MessengerType::Line);

    for (const QString& path : paths) {
        if (QDir(path).exists()) {
            scanDirectory(path, MessengerType::Line);
        }
    }
}

void MessengerCollector::collectDiscord() {
    QStringList paths;
    QString userProfile = getUserProfile();

    // 기본 설치 경로
    paths << userProfile + "/AppData/Local/Discord";
    paths << userProfile + "/AppData/Roaming/discord";

    // 다운로드 폴더
    if (m_options.includeDownloads) {
        paths << getDownloads() + "/Discord";
    }

    // 캐시 경로
    if (m_options.includeCache) {
        paths << userProfile + "/AppData/Roaming/discord/Cache";
        paths << userProfile + "/AppData/Local/Discord/Cache";
    }

    // 로그 파일
    if (m_options.includeLogs) {
        paths << userProfile + "/AppData/Roaming/discord/logs";
        paths << userProfile + "/AppData/Local/Discord/logs";
    }

    // 커스텀 경로 추가
    paths << getMessengerPaths(MessengerType::Discord);

    for (const QString& path : paths) {
        if (QDir(path).exists()) {
            scanDirectory(path, MessengerType::Discord);
        }
    }
}

void MessengerCollector::collectTelegram() {
    QStringList paths;
    QString userProfile = getUserProfile();

    // 기본 설치 경로
    paths << userProfile + "/AppData/Roaming/Telegram Desktop";
    paths << userProfile + "/AppData/Local/Telegram Desktop";

    // 다운로드 폴더
    if (m_options.includeDownloads) {
        paths << getDownloads() + "/Telegram Desktop";
    }

    // 로그 파일
    if (m_options.includeLogs) {
        paths << userProfile + "/AppData/Roaming/Telegram Desktop/log.txt";
    }

    // 커스텀 경로 추가
    paths << getMessengerPaths(MessengerType::Telegram);

    for (const QString& path : paths) {
        if (QDir(path).exists() || QFile(path).exists()) {
            if (QFileInfo(path).isFile()) {
                // 단일 파일 처리
                QFileInfo fileInfo(path);
                if (isValidFile(fileInfo)) {
                    MessengerFileInfo info;
                    info.messenger = MessengerType::Telegram;
                    info.fileType = determineFileType(path, MessengerType::Telegram);
                    info.fileName = fileInfo.fileName();
                    info.filePath = fileInfo.absoluteFilePath();
                    info.relativePath = fileInfo.fileName();
                    info.lastModified = fileInfo.lastModified();
                    info.created = fileInfo.birthTime();
                    info.fileSize = fileInfo.size();

                    if (m_options.calculateHash) {
                        info.fileHash = calculateFileHash(path);
                    }

                    m_fileList.append(info);
                }
            } else {
                scanDirectory(path, MessengerType::Telegram);
            }
        }
    }
}

void MessengerCollector::collectWhatsApp() {
    QStringList paths;
    QString userProfile = getUserProfile();

    // 기본 설치 경로
    paths << userProfile + "/AppData/Roaming/WhatsApp";
    paths << userProfile + "/AppData/Local/WhatsApp";

    // 다운로드 폴더
    if (m_options.includeDownloads) {
        paths << getDownloads() + "/WhatsApp";
    }

    // 로그 파일
    if (m_options.includeLogs) {
        paths << userProfile + "/AppData/Roaming/WhatsApp/logs";
    }

    // 커스텀 경로 추가
    paths << getMessengerPaths(MessengerType::WhatsApp);

    for (const QString& path : paths) {
        if (QDir(path).exists()) {
            scanDirectory(path, MessengerType::WhatsApp);
        }
    }
}

void MessengerCollector::scanDirectory(const QString& dirPath, MessengerType messenger) {
    if (!hasPermission(dirPath)) {
        logError(QString("접근 권한 없음: %1").arg(dirPath));
        return;
    }

    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (m_options.deepScan) {
        flags = QDirIterator::Subdirectories;
    }

    QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot, flags);
    QString basePath = QDir(dirPath).absolutePath();

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);

        if (!isValidFile(fileInfo)) {
            continue;
        }

        MessengerFileInfo info;
        info.messenger = messenger;
        info.fileType = determineFileType(filePath, messenger);
        info.fileName = fileInfo.fileName();
        info.filePath = fileInfo.absoluteFilePath();
        info.relativePath = QDir(basePath).relativeFilePath(filePath);
        info.lastModified = fileInfo.lastModified();
        info.created = fileInfo.birthTime();
        info.fileSize = fileInfo.size();

        if (m_options.calculateHash) {
            info.fileHash = calculateFileHash(filePath);
        }

        m_fileList.append(info);
    }
}

bool MessengerCollector::isValidFile(const QFileInfo& fileInfo) const {
    // 파일 크기 체크
    if (m_options.maxFileSize > 0 && fileInfo.size() > m_options.maxFileSize) {
        return false;
    }

    // 날짜 범위 체크
    if (m_options.fromDate.isValid() && fileInfo.lastModified() < m_options.fromDate) {
        return false;
    }

    if (m_options.toDate.isValid() && fileInfo.lastModified() > m_options.toDate) {
        return false;
    }

    // 확장자 체크
    if (!m_options.fileExtensions.isEmpty()) {
        QString ext = fileInfo.suffix().toLower();
        if (!m_options.fileExtensions.contains(ext, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return fileInfo.isFile() && fileInfo.isReadable();
}

FileType MessengerCollector::determineFileType(const QString& filePath, MessengerType messenger) const {
    QFileInfo fileInfo(filePath);
    QString ext = fileInfo.suffix().toLower();
    QString fileName = fileInfo.fileName().toLower();
    QString dirName = fileInfo.dir().dirName().toLower();

    // 데이터베이스 파일
    if (ext == "db" || ext == "sqlite" || ext == "sqlite3") {
        return FileType::ChatDatabase;
    }

    // 미디어 파일
    QStringList mediaExts = {"jpg", "jpeg", "png", "gif", "bmp", "webp", "tiff",
                             "mp4", "avi", "mov", "wmv", "mkv", "flv",
                             "mp3", "wav", "m4a", "aac", "flac", "ogg"};
    if (mediaExts.contains(ext)) {
        return FileType::MediaFile;
    }

    // 캐시 파일
    if (dirName.contains("cache") || fileName.contains("cache") || ext == "cache") {
        return FileType::CacheFile;
    }

    // 설정 파일
    QStringList configExts = {"json", "xml", "ini", "cfg", "conf", "plist", "config"};
    if (configExts.contains(ext) || fileName.contains("config") || fileName.contains("setting")) {
        return FileType::ConfigFile;
    }

    // 로그 파일
    if (ext == "log" || ext == "txt" || fileName.contains("log") || dirName.contains("log")) {
        return FileType::LogFile;
    }

    // 다운로드 폴더의 파일
    if (filePath.contains("/Downloads/", Qt::CaseInsensitive) ||
        filePath.contains("/다운로드/", Qt::CaseInsensitive)) {
        return FileType::DownloadFile;
    }

    return FileType::Unknown;
}

QString MessengerCollector::calculateFileHash(const QString& filePath) const {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);

    // 큰 파일의 경우 청크 단위로 읽기
    const qint64 chunkSize = 8192; // 8KB
    while (!file.atEnd()) {
        QByteArray chunk = file.read(chunkSize);
        hash.addData(chunk);
    }

    return hash.result().toHex();
}

// 결과 조회 함수들
const QList<MessengerFileInfo>& MessengerCollector::getFileList() const {
    return m_fileList;
}

QList<MessengerFileInfo> MessengerCollector::getFilesByMessenger(MessengerType type) const {
    QList<MessengerFileInfo> filtered;
    for (const auto& file : m_fileList) {
        if (file.messenger == type) {
            filtered.append(file);
        }
    }
    return filtered;
}

QList<MessengerFileInfo> MessengerCollector::getFilesByType(FileType type) const {
    QList<MessengerFileInfo> filtered;
    for (const auto& file : m_fileList) {
        if (file.fileType == type) {
            filtered.append(file);
        }
    }
    return filtered;
}

CollectionStats MessengerCollector::getStats() const {
    return m_stats;
}

// 출력 함수들
void MessengerCollector::printCollectionSummary() const {
    qDebug() << "\n========== 메신저 포렌식 수집 결과 ==========";
    qDebug() << QString("총 파일 수: %1개").arg(m_stats.totalFiles);
    qDebug() << QString("총 파일 크기: %1").arg(formatFileSize(m_stats.totalSize));
    qDebug() << QString("스캔 시간: %1").arg(formatDuration(m_stats.scanStartTime, m_stats.scanEndTime));
    qDebug() << QString("오류 수: %1개").arg(m_stats.errorCount);

    if (!m_stats.errors.isEmpty()) {
        qDebug() << "\n오류 목록:";
        for (const QString& error : m_stats.errors) {
            qDebug() << "  -" << error;
        }
    }

    qDebug() << "\n메신저별 파일 수:";
    for (auto it = m_stats.messengerCounts.begin(); it != m_stats.messengerCounts.end(); ++it) {
        if (it.value() > 0) {
            qDebug() << QString("  - %1: %2개").arg(messengerTypeToString(it.key())).arg(it.value());
        }
    }

    qDebug() << "\n파일 타입별 분류:";
    for (auto it = m_stats.fileTypeCounts.begin(); it != m_stats.fileTypeCounts.end(); ++it) {
        if (it.value() > 0) {
            qDebug() << QString("  - %1: %2개").arg(fileTypeToString(it.key())).arg(it.value());
        }
    }
    qDebug() << "==========================================\n";
}

void MessengerCollector::printDetailedResults() const {
    qDebug() << "\n========== 상세 파일 목록 ==========";

    for (const auto& file : m_fileList) {
        qDebug() << QString("[%1] %2").arg(file.getMessengerName()).arg(file.fileName);
        qDebug() << QString("  경로: %1").arg(file.filePath);
        qDebug() << QString("  타입: %1").arg(file.getFileTypeName());
        qDebug() << QString("  크기: %1").arg(formatFileSize(file.fileSize));
        qDebug() << QString("  수정일: %1").arg(file.lastModified.toString("yyyy-MM-dd hh:mm:ss"));

        if (!file.fileHash.isEmpty()) {
            qDebug() << QString("  해시: %1").arg(file.fileHash);
        }
        qDebug() << "";
    }
    qDebug() << "==================================\n";
}

void MessengerCollector::printMessengerSummary(MessengerType messenger) const {
    auto files = getFilesByMessenger(messenger);
    QString messengerName = messengerTypeToString(messenger);

    qDebug() << QString("\n========== %1 파일 요약 ==========").arg(messengerName);
    qDebug() << QString("총 파일 수: %1개").arg(files.size());

    qint64 totalSize = 0;
    QMap<FileType, int> typeCounts;

    for (const auto& file : files) {
        totalSize += file.fileSize;
        typeCounts[file.fileType]++;
    }

    qDebug() << QString("총 크기: %1").arg(formatFileSize(totalSize));

    qDebug() << "\n파일 타입별 분류:";
    for (auto it = typeCounts.begin(); it != typeCounts.end(); ++it) {
        qDebug() << QString("  - %1: %2개").arg(fileTypeToString(it.key())).arg(it.value());
    }
    qDebug() << "===============================\n";
}

// 설정 함수들
void MessengerCollector::setOptions(const CollectionOptions& options) {
    m_options = options;
}

void MessengerCollector::addCustomPath(MessengerType messenger, const QString& path) {
    m_customPaths[messenger].append(path);
}

// 유틸리티 함수들
QStringList MessengerCollector::getMessengerPaths(MessengerType messenger) const {
    return m_customPaths.value(messenger, QStringList());
}

QString MessengerCollector::getUserProfile() const {
    return QDir::homePath();
}

QString MessengerCollector::getAppDataLocal() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}

QString MessengerCollector::getAppDataRoaming() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString MessengerCollector::getDocuments() const {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

QString MessengerCollector::getDownloads() const {
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void MessengerCollector::logError(const QString& error) {
    m_stats.errors.append(error);
    m_stats.errorCount++;
    qWarning() << "MessengerCollector Error:" << error;
}

bool MessengerCollector::hasPermission(const QString& path) const {
    QFileInfo info(path);
    return info.exists() && info.isReadable();
}

void MessengerCollector::updateStats() {
    m_stats.totalFiles = m_fileList.size();
    m_stats.totalSize = 0;
    m_stats.messengerCounts.clear();
    m_stats.fileTypeCounts.clear();

    for (const auto& file : m_fileList) {
        m_stats.totalSize += file.fileSize;
        m_stats.messengerCounts[file.messenger]++;
        m_stats.fileTypeCounts[file.fileType]++;
    }
}

QString MessengerCollector::formatFileSize(qint64 bytes) const {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 1);
}

QString MessengerCollector::formatDuration(const QDateTime& start, const QDateTime& end) const {
    qint64 seconds = start.secsTo(end);
    if (seconds < 60) return QString("%1초").arg(seconds);
    if (seconds < 3600) return QString("%1분 %2초").arg(seconds / 60).arg(seconds % 60);
    return QString("%1시간 %2분").arg(seconds / 3600).arg((seconds % 3600) / 60);
}

// 전역 헬퍼 함수들
QString messengerTypeToString(MessengerType type) {
    switch (type) {
    case MessengerType::KakaoTalk: return "KakaoTalk";
    case MessengerType::Line: return "Line";
    case MessengerType::Discord: return "Discord";
    case MessengerType::Telegram: return "Telegram";
    case MessengerType::WhatsApp: return "WhatsApp";
    default: return "Unknown";
    }
}

MessengerType stringToMessengerType(const QString& str) {
    if (str == "KakaoTalk") return MessengerType::KakaoTalk;
    if (str == "Line") return MessengerType::Line;
    if (str == "Discord") return MessengerType::Discord;
    if (str == "Telegram") return MessengerType::Telegram;
    if (str == "WhatsApp") return MessengerType::WhatsApp;
    return MessengerType::Unknown;
}

QString fileTypeToString(FileType type) {
    switch (type) {
    case FileType::ChatDatabase: return "채팅DB";
    case FileType::MediaFile: return "미디어파일";
    case FileType::CacheFile: return "캐시파일";
    case FileType::ConfigFile: return "설정파일";
    case FileType::LogFile: return "로그파일";
    case FileType::DownloadFile: return "다운로드파일";
    default: return "알수없음";
    }
}

FileType stringToFileType(const QString& str) {
    if (str == "채팅DB" || str == "ChatDatabase") return FileType::ChatDatabase;
    if (str == "미디어파일" || str == "MediaFile") return FileType::MediaFile;
    if (str == "캐시파일" || str == "CacheFile") return FileType::CacheFile;
    if (str == "설정파일" || str == "ConfigFile") return FileType::ConfigFile;
    if (str == "로그파일" || str == "LogFile") return FileType::LogFile;
    if (str == "다운로드파일" || str == "DownloadFile") return FileType::DownloadFile;
    return FileType::Unknown;
}

// =============================================================================
// JSON 변환 함수들 구현 (기존 messengercollector.cpp 파일 끝에 추가)
// =============================================================================

QJsonObject MessengerCollector::toJsonObject() const {
    QJsonObject result;
    /*
    // 메타데이터
    result["collection_info"] = QJsonObject({
        {"module_name", "Messenger_Data"},
        {"collection_time", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"total_files", static_cast<int>(m_fileList.size())},
        {"version", "1.0"}
    });

    // 수집 옵션 정보
    result["collection_options"] = collectionOptionsToJson(m_options);

    // 통계 정보
    result["statistics"] = collectionStatsToJson(m_stats);
    */
    // 메신저별 파일 분류
    QJsonObject messengerGroups;

    // 각 메신저 타입별로 파일들을 그룹화
    QMap<MessengerType, QList<MessengerFileInfo>> messengerFiles;
    for (const auto& file : m_fileList) {
        messengerFiles[file.messenger].append(file);
    }

    // 각 메신저별 JSON 생성
    for (auto it = messengerFiles.begin(); it != messengerFiles.end(); ++it) {
        MessengerType type = it.key();
        const QList<MessengerFileInfo>& files = it.value();

        QJsonObject messengerObj;
        messengerObj["messenger_name"] = messengerTypeToString(type);
        messengerObj["file_count"] = files.size();

        // 해당 메신저의 파일 크기 합계
        qint64 totalSize = 0;
        for (const auto& file : files) {
            totalSize += file.fileSize;
        }
        messengerObj["total_size_bytes"] = static_cast<qint64>(totalSize);

        // 파일 타입별 통계
        QMap<FileType, int> typeCounts;
        for (const auto& file : files) {
            typeCounts[file.fileType]++;
        }

        QJsonObject typeStats;
        for (auto typeIt = typeCounts.begin(); typeIt != typeCounts.end(); ++typeIt) {
            typeStats[fileTypeToString(typeIt.key())] = typeIt.value();
        }
        messengerObj["file_type_statistics"] = typeStats;

        // 파일 목록
        QJsonArray fileArray;
        for (int i = 0; i < files.size(); i++) {
            QJsonObject fileObj = messengerFileInfoToJson(files[i]);
            fileObj["file_index"] = i + 1;
            fileArray.append(fileObj);
        }
        messengerObj["files"] = fileArray;

        messengerGroups[messengerTypeToString(type)] = messengerObj;
    }

    result["messenger_data"] = messengerGroups;
    /*
    // 전체 파일 목록 (선택적)
    if (!m_fileList.isEmpty()) {
        QJsonArray allFilesArray;
        for (int i = 0; i < m_fileList.size(); i++) {
            QJsonObject fileObj = messengerFileInfoToJson(m_fileList[i]);
            fileObj["global_file_index"] = i + 1;
            allFilesArray.append(fileObj);
        }
        result["all_files"] = allFilesArray;
    }
    */
    return result;
}

QJsonObject MessengerCollector::messengerFileInfoToJson(const MessengerFileInfo& info) const {
    QJsonObject result;

    // 기본 파일 정보
    result["messenger_type"] = messengerTypeToString(info.messenger);
    result["file_type"] = fileTypeToString(info.fileType);
    result["file_name"] = info.fileName;
    result["file_path"] = info.filePath;
    result["relative_path"] = info.relativePath;
    result["file_size"] = static_cast<qint64>(info.fileSize);

    // 시간 정보
    if (info.lastModified.isValid()) {
        result["last_modified"] = info.lastModified.toString(Qt::ISODate);
        result["last_modified_timestamp"] = info.lastModified.toMSecsSinceEpoch();
    } else {
        result["last_modified"] = QString();
        result["last_modified_timestamp"] = 0;
    }

    if (info.created.isValid()) {
        result["created"] = info.created.toString(Qt::ISODate);
        result["created_timestamp"] = info.created.toMSecsSinceEpoch();
    } else {
        result["created"] = QString();
        result["created_timestamp"] = 0;
    }

    // 해시 정보 (있는 경우)
    if (!info.fileHash.isEmpty()) {
        result["file_hash"] = info.fileHash;
    }

    // 유효성 정보
    result["is_valid_file"] = info.isValidFile();

    return result;
}

QJsonObject MessengerCollector::collectionStatsToJson(const CollectionStats& stats) const {
    QJsonObject result;

    // 기본 통계
    result["total_files"] = stats.totalFiles;
    result["total_size_bytes"] = static_cast<qint64>(stats.totalSize);
    result["error_count"] = stats.errorCount;

    // 시간 정보
    if (stats.scanStartTime.isValid()) {
        result["scan_start_time"] = stats.scanStartTime.toString(Qt::ISODate);
        result["scan_start_timestamp"] = stats.scanStartTime.toMSecsSinceEpoch();
    }

    if (stats.scanEndTime.isValid()) {
        result["scan_end_time"] = stats.scanEndTime.toString(Qt::ISODate);
        result["scan_end_timestamp"] = stats.scanEndTime.toMSecsSinceEpoch();

        if (stats.scanStartTime.isValid()) {
            qint64 durationMs = stats.scanStartTime.msecsTo(stats.scanEndTime);
            result["scan_duration_ms"] = durationMs;
        }
    }

    // 에러 목록
    if (!stats.errors.isEmpty()) {
        QJsonArray errorArray;
        for (const QString& error : stats.errors) {
            errorArray.append(error);
        }
        result["errors"] = errorArray;
    }

    // 메신저별 통계
    QJsonObject messengerStats;
    for (auto it = stats.messengerCounts.begin(); it != stats.messengerCounts.end(); ++it) {
        if (it.value() > 0) {
            messengerStats[messengerTypeToString(it.key())] = it.value();
        }
    }
    result["messenger_counts"] = messengerStats;

    // 파일 타입별 통계
    QJsonObject fileTypeStats;
    for (auto it = stats.fileTypeCounts.begin(); it != stats.fileTypeCounts.end(); ++it) {
        if (it.value() > 0) {
            fileTypeStats[fileTypeToString(it.key())] = it.value();
        }
    }
    result["file_type_counts"] = fileTypeStats;

    return result;
}

QJsonObject MessengerCollector::collectionOptionsToJson(const CollectionOptions& options) const {
    QJsonObject result;

    // 수집 옵션들
    result["include_cache"] = options.includeCache;
    result["include_downloads"] = options.includeDownloads;
    result["include_logs"] = options.includeLogs;
    result["calculate_hash"] = options.calculateHash;
    result["deep_scan"] = options.deepScan;
    result["max_file_size"] = static_cast<qint64>(options.maxFileSize);

    // 파일 확장자 필터
    if (!options.fileExtensions.isEmpty()) {
        QJsonArray extArray;
        for (const QString& ext : options.fileExtensions) {
            extArray.append(ext);
        }
        result["file_extensions"] = extArray;
    }

    // 날짜 범위
    if (options.fromDate.isValid()) {
        result["from_date"] = options.fromDate.toString(Qt::ISODate);
        result["from_date_timestamp"] = options.fromDate.toMSecsSinceEpoch();
    }

    if (options.toDate.isValid()) {
        result["to_date"] = options.toDate.toString(Qt::ISODate);
        result["to_date_timestamp"] = options.toDate.toMSecsSinceEpoch();
    }

    return result;
}

// 기존 saveToJson 함수 구현 (toJsonObject 사용)
bool MessengerCollector::saveToJson(const QString& filePath) const {
    QJsonObject jsonObj = toJsonObject();
    QJsonDocument doc(jsonObj);

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        //logError(QString("JSON 파일 생성 실패: %1").arg(filePath));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
