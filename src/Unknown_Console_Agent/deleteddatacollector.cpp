#include "deleteddatacollector.h"

DeletedDataCollector::DeletedDataCollector(QObject *parent)
    : QObject(parent)
{
}

DeletedDataCollector::~DeletedDataCollector()
{
    clearResults();
}

bool DeletedDataCollector::collectDeletedFiles()
{
    clearResults();

    QStringList drivePaths = getAllDrivePaths();
    bool hasResults = false;

    for (const QString& drive : drivePaths) {
        QString recycleBinPath = drive + "$Recycle.Bin";

        QDir recycleBinDir(recycleBinPath);
        if (recycleBinDir.exists()) {
            if (scanRecycleBinDirectory(recycleBinPath)) {
                hasResults = true;
            }
        }
    }

    return hasResults;
}

bool DeletedDataCollector::scanRecycleBinDirectory(const QString& recycleBinPath)
{
    QDir recycleBinDir(recycleBinPath);
    if (!recycleBinDir.exists()) {
        return false;
    }

    QStringList sidDirs = recycleBinDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    bool foundFiles = false;

    for (const QString& dirName : sidDirs) {
        if (isValidSID(dirName)) {
            QString sidPath = recycleBinPath + "/" + dirName;
            if (scanUserSIDDirectory(sidPath, dirName)) {
                foundFiles = true;
            }
        }
    }

    return foundFiles;
}

bool DeletedDataCollector::scanUserSIDDirectory(const QString& sidPath, const QString& userSID)
{
    QDir sidDir(sidPath);
    if (!sidDir.exists()) {
        return false;
    }

    QStringList nameFilters;
    nameFilters << "$I*";

    QFileInfoList iFiles = sidDir.entryInfoList(nameFilters, QDir::Files | QDir::Hidden | QDir::System);

    if (iFiles.isEmpty()) {
        return false;
    }

    int processedCount = 0;
    for (const QFileInfo& fileInfo : iFiles) {
        RecycleBinFileInfo deletedFile = parseIFile(fileInfo.absoluteFilePath(), userSID);

        if (deletedFile.parseStatus == ParseResult::SUCCESS) {
            deletedFiles.append(deletedFile);
            processedCount++;
        }
    }

    return processedCount > 0;
}

bool DeletedDataCollector::isValidSID(const QString& sidString)
{
    QRegularExpression sidPattern("^S-1-5-((18|19|20)|21-\\d+-\\d+-\\d+-\\d+)$");
    return sidPattern.match(sidString).hasMatch();
}

WindowsRecycleBinVersion DeletedDataCollector::detectFileVersion(const QString& iFilePath)
{
    QFile file(iFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return WindowsRecycleBinVersion::UNKNOWN;
    }

    qint64 fileSize = file.size();
    if (fileSize < 28) {
        return WindowsRecycleBinVersion::UNKNOWN;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint64 version;
    stream >> version;

    if (version == 1 && fileSize == 544) {
        return WindowsRecycleBinVersion::VISTA_WIN8;
    } else if (version == 2 && fileSize >= 28) {
        return WindowsRecycleBinVersion::WIN10_PLUS;
    }

    return WindowsRecycleBinVersion::UNKNOWN;
}

RecycleBinFileInfo DeletedDataCollector::parseIFile(const QString& iFilePath, const QString& userSID)
{
    RecycleBinFileInfo info;
    info.currentIFilePath = iFilePath;
    info.userSID = userSID;

    info.version = detectFileVersion(iFilePath);
    if (info.version == WindowsRecycleBinVersion::UNKNOWN) {
        info.parseStatus = ParseResult::UNSUPPORTED_VERSION;
        info.parseErrorMessage = "지원되지 않는 파일 버전";
        return info;
    }

    QFile file(iFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        info.parseStatus = ParseResult::FILE_NOT_FOUND;
        info.parseErrorMessage = "파일을 열 수 없음";
        return info;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    if (info.version == WindowsRecycleBinVersion::VISTA_WIN8) {
        info = parseVistaFormat(stream, iFilePath, userSID);
    } else if (info.version == WindowsRecycleBinVersion::WIN10_PLUS) {
        info = parseWindows10Format(stream, iFilePath, userSID);
    }

    if (info.parseStatus == ParseResult::SUCCESS) {
        info.currentRFilePath = findCorrespondingRFile(iFilePath);
        info.originalFileName = extractFileNameFromPath(info.originalPath);

        if (!validateParsedData(info)) {
            info.parseStatus = ParseResult::CORRUPTED_HEADER;
            info.parseErrorMessage = "데이터 유효성 검증 실패";
        }
    }

    return info;
}

RecycleBinFileInfo DeletedDataCollector::parseVistaFormat(QDataStream& stream, const QString& iFilePath, const QString& userSID)
{
    RecycleBinFileInfo info;
    info.currentIFilePath = iFilePath;
    info.userSID = userSID;
    info.version = WindowsRecycleBinVersion::VISTA_WIN8;

    quint64 version, fileSize, filetime;

    stream >> version;
    stream >> fileSize;
    stream >> filetime;

    if (stream.status() != QDataStream::Ok) {
        info.parseStatus = ParseResult::INCOMPLETE_DATA;
        info.parseErrorMessage = "헤더 읽기 실패";
        return info;
    }

    QByteArray pathBytes(520, 0);
    if (stream.readRawData(pathBytes.data(), 520) != 520) {
        info.parseStatus = ParseResult::MALFORMED_PATH;
        info.parseErrorMessage = "경로 읽기 실패";
        return info;
    }

    QString originalPath = QString::fromUtf16(
        reinterpret_cast<const ushort*>(pathBytes.constData()),
        260
        );

    int nullPos = originalPath.indexOf(QChar(0));
    if (nullPos >= 0) {
        originalPath = originalPath.left(nullPos);
    }

    info.originalPath = originalPath;
    info.originalFileSize = static_cast<qint64>(fileSize);
    info.deletedTime = convertFiletimeToDateTime(filetime);
    info.parseStatus = ParseResult::SUCCESS;

    return info;
}

RecycleBinFileInfo DeletedDataCollector::parseWindows10Format(QDataStream& stream, const QString& iFilePath, const QString& userSID)
{
    RecycleBinFileInfo info;
    info.currentIFilePath = iFilePath;
    info.userSID = userSID;
    info.version = WindowsRecycleBinVersion::WIN10_PLUS;

    quint64 version, fileSize, filetime;
    quint32 pathLength;

    stream >> version;
    stream >> fileSize;
    stream >> filetime;
    stream >> pathLength;

    if (stream.status() != QDataStream::Ok) {
        info.parseStatus = ParseResult::INCOMPLETE_DATA;
        info.parseErrorMessage = "헤더 읽기 실패";
        return info;
    }

    if (pathLength == 0) {
        info.parseStatus = ParseResult::MALFORMED_PATH;
        info.parseErrorMessage = "경로 길이가 0";
        return info;
    }

    qint64 remainingBytes = stream.device()->size() - stream.device()->pos();
    qint64 actualReadLength = qMin((qint64)pathLength, remainingBytes);

    QByteArray allRemainingBytes(remainingBytes, 0);
    stream.readRawData(allRemainingBytes.data(), remainingBytes);

    QString originalPath;
    if (remainingBytes >= 2) {
        int charCount = remainingBytes / 2;
        originalPath = QString::fromUtf16(
            reinterpret_cast<const ushort*>(allRemainingBytes.constData()),
            charCount
            );

        int nullPos = originalPath.indexOf(QChar(0));
        if (nullPos >= 0) {
            originalPath = originalPath.left(nullPos);
        }
    }

    info.originalPath = originalPath;
    info.originalFileSize = static_cast<qint64>(fileSize);
    info.deletedTime = convertFiletimeToDateTime(filetime);
    info.parseStatus = ParseResult::SUCCESS;

    return info;
}

QString DeletedDataCollector::findCorrespondingRFile(const QString& iFilePath)
{
    QString rFilePath = iFilePath;
    rFilePath.replace("$I", "$R");

    QFile rFile(rFilePath);
    if (rFile.exists()) {
        return rFilePath;
    }

    return QString();
}

QDateTime DeletedDataCollector::convertFiletimeToDateTime(quint64 filetime)
{
    if (filetime == 0) {
        return QDateTime();
    }

    const quint64 FILETIME_EPOCH_OFFSET = 11644473600ULL;
    const quint64 FILETIME_TICKS_PER_SECOND = 10000000ULL;

    const quint64 MIN_VALID_FILETIME = 119600064000000000ULL; // 1980년
    const quint64 MAX_VALID_FILETIME = 159287424000000000ULL; // 2099년

    if (filetime < MIN_VALID_FILETIME || filetime > MAX_VALID_FILETIME) {
        return QDateTime();
    }

    quint64 unixSeconds = (filetime / FILETIME_TICKS_PER_SECOND) - FILETIME_EPOCH_OFFSET;
    quint64 nanoseconds = ((filetime % FILETIME_TICKS_PER_SECOND) * 100);

    QDateTime result = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(unixSeconds), Qt::UTC);

    if (!result.isValid()) {
        return QDateTime();
    }

    result = result.addMSecs(static_cast<qint64>(nanoseconds / 1000000));

    QDateTime minTime = QDateTime::fromString("1980-01-01T00:00:00", Qt::ISODate);
    QDateTime maxTime = QDateTime::fromString("2099-12-31T23:59:59", Qt::ISODate);

    if (result < minTime || result > maxTime) {
        return QDateTime();
    }

    return result;
}

QString DeletedDataCollector::extractFileNameFromPath(const QString& fullPath)
{
    QFileInfo fileInfo(fullPath);
    return fileInfo.fileName();
}

bool DeletedDataCollector::validateParsedData(const RecycleBinFileInfo& info)
{
    if (info.originalPath.isEmpty()) {
        return false;
    }

    if (info.originalFileSize < 0) {
        return false;
    }

    return true;
}

bool DeletedDataCollector::validateMFTDeletedData(const MFTDeletedFileInfo& info)
{
    if (info.fileName.isEmpty()) {
        return false;
    }

    if (info.fileSize < 0) {
        return false;
    }

    return true;
}

QStringList DeletedDataCollector::getAllDrivePaths() const
{
    QStringList drivePaths;

#ifdef Q_OS_WIN
    foreach (const QStorageInfo &storage, QStorageInfo::mountedVolumes()) {
        if (storage.isValid() && storage.isReady()) {
            QString drivePath = storage.rootPath();
            if (!drivePath.endsWith('/') && !drivePath.endsWith('\\')) {
                drivePath += '/';
            }
            drivePaths.append(drivePath);
        }
    }
#else
    drivePaths.append("/mnt/");
    drivePaths.append("/media/");
#endif

    if (drivePaths.isEmpty()) {
        drivePaths.append("C:/");
    }

    return drivePaths;
}

const QList<RecycleBinFileInfo>& DeletedDataCollector::getDeletedFiles() const
{
    return deletedFiles;
}

int DeletedDataCollector::getDeletedFileCount() const
{
    return deletedFiles.size();
}

void DeletedDataCollector::clearResults()
{
    deletedFiles.clear();
    mftDeletedFiles.clear();
    mftRecordToPath.clear();
}

void DeletedDataCollector::printDeviceSummary() const
{
    qDebug() << "=== Recycle Bin 분석 결과 ===";
    qDebug() << "총 삭제된 파일 수:" << deletedFiles.size() << "개";

    if (deletedFiles.isEmpty()) {
        qDebug() << "삭제된 파일이 없습니다.";
        return;
    }

    int vistaCount = 0, win10Count = 0;
    qint64 totalSize = 0;

    for (const auto& file : deletedFiles) {
        if (file.version == WindowsRecycleBinVersion::VISTA_WIN8) {
            vistaCount++;
        } else if (file.version == WindowsRecycleBinVersion::WIN10_PLUS) {
            win10Count++;
        }
        totalSize += file.originalFileSize;
    }

    if (win10Count > 0) qDebug() << "Windows 10+ 형식:" << win10Count << "개";
    if (vistaCount > 0) qDebug() << "Vista~8.1 형식:" << vistaCount << "개";
    qDebug() << "총 파일 크기:" << QString("%L1").arg(totalSize) << "바이트";

    qDebug() << "\n=== 삭제된 파일 목록 ===";
    int index = 1;
    for (const auto& file : deletedFiles) {
        qDebug() << QString("[%1] %2").arg(index++, 2).arg(file.originalFileName);
        qDebug() << QString("    경로: %1").arg(file.originalPath);
        qDebug() << QString("    시간: %1").arg(file.deletedTime.toString("yyyy-MM-dd hh:mm:ss"));
        qDebug() << QString("    크기: %L1 바이트").arg(file.originalFileSize);
        qDebug() << "";
    }
}

// === MFT 관련 구현 ===

bool DeletedDataCollector::collectAllDeletedFiles()
{
    clearResults();
    bool hasRecycleBinResults = collectDeletedFiles();
    bool hasMFTResults = collectFromMFT();

    return hasRecycleBinResults || hasMFTResults;
}

bool DeletedDataCollector::collectFromMFT()
{
    mftDeletedFiles.clear();

    QStringList drivePaths = getAllDrivePaths();
    bool hasResults = false;

    for (const QString& drive : drivePaths) {
        QString driveLetter = drive.left(1);
        if (processDriveMFT(driveLetter)) {
            hasResults = true;
        }
    }

    return hasResults;
}

bool DeletedDataCollector::processDriveMFT(const QString& driveLetter)
{
    HANDLE volumeHandle = openVolumeHandle(driveLetter);
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    quint64 mftStartLCN = 0;
    quint64 bytesPerCluster = 0;

    if (!getMFTInfo(volumeHandle, mftStartLCN, bytesPerCluster)) {
        CloseHandle(volumeHandle);
        return false;
    }

    bool foundFiles = false;
    const quint64 MAX_ENTRIES_TO_SCAN = 50000;

    for (quint64 entryNumber = 0; entryNumber < MAX_ENTRIES_TO_SCAN; ++entryNumber) {
        QByteArray entryData = readMFTEntry(volumeHandle, entryNumber, mftStartLCN, bytesPerCluster);

        if (entryData.isEmpty()) {
            continue;
        }

        if (!isValidMFTEntry(entryData)) {
            continue;
        }

        const MFTEntryHeader* header = reinterpret_cast<const MFTEntryHeader*>(entryData.constData());

        bool isDeleted = isDeletedMFTEntry(*header);
        bool isSuspicious = isSuspiciousMFTEntry(*header, entryData);

        if (isDeleted || isSuspicious) {
            QList<QPair<MFTAttributeType, QByteArray>> attributes = extractAttributes(entryData);

            bool hasStdInfo = false;
            bool hasFileNameAttr = false;

            for (const auto& attr : attributes) {
                if (attr.first == MFTAttributeType::STANDARD_INFORMATION) {
                    hasStdInfo = true;
                }
                if (attr.first == MFTAttributeType::FILE_NAME) {
                    hasFileNameAttr = true;
                }
            }

            MFTDeletedFileInfo fileInfo;

            if (parseMFTEntryRelaxed(entryData, entryNumber, fileInfo, hasStdInfo, hasFileNameAttr)) {
                mftDeletedFiles.append(fileInfo);
                foundFiles = true;
            }
        }
    }

    CloseHandle(volumeHandle);
    return foundFiles;
}

bool DeletedDataCollector::parseMFTEntryRelaxed(const QByteArray& entryData, quint64 entryNumber,
                                                MFTDeletedFileInfo& fileInfo, bool hasStdInfo, bool hasFileNameAttr)
{
    fileInfo.mftRecordNumber = static_cast<quint32>(entryNumber);
    const MFTEntryHeader* header = reinterpret_cast<const MFTEntryHeader*>(entryData.constData());
    fileInfo.isDirectory = (header->flags & static_cast<quint16>(MFTEntryFlags::FILE_RECORD_IS_DIRECTORY)) != 0;

    QList<QPair<MFTAttributeType, QByteArray>> attributes = extractAttributes(entryData);

    bool parsedSomething = false;

    for (const auto& attr : attributes) {
        switch (attr.first) {
        case MFTAttributeType::STANDARD_INFORMATION:
            if (parseStandardInformation(attr.second, fileInfo)) {
                parsedSomething = true;
            }
            break;

        case MFTAttributeType::FILE_NAME:
            if (parseFileName(attr.second, fileInfo)) {
                parsedSomething = true;
            }
            break;

        default:
            break;
        }
    }

    if (fileInfo.fileName.isEmpty()) {
        if (fileInfo.isDirectory) {
            fileInfo.fileName = QString("Unknown_Dir_%1").arg(entryNumber);
        } else {
            fileInfo.fileName = QString("Unknown_File_%1").arg(entryNumber);
        }
        parsedSomething = true;
    }

    if (parsedSomething) {
        if (validateMFTDeletedData(fileInfo)) {
            fileInfo.parseStatus = ParseResult::SUCCESS;
            return true;
        } else {
            fileInfo.parseStatus = ParseResult::CORRUPTED_HEADER;
            fileInfo.parseErrorMessage = "유효성 검증 실패";
        }
    } else {
        fileInfo.parseStatus = ParseResult::INCOMPLETE_DATA;
        fileInfo.parseErrorMessage = "최소 정보 부족";
    }

    return false;
}

HANDLE DeletedDataCollector::openVolumeHandle(const QString& driveLetter)
{
    QString volumePath = QString("\\\\.\\%1:").arg(driveLetter);

    HANDLE handle = CreateFileA(
        volumePath.toLocal8Bit().constData(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING,
        nullptr
        );

    return handle;
}

bool DeletedDataCollector::getMFTInfo(HANDLE volumeHandle, quint64& mftStartLCN, quint64& bytesPerCluster)
{
    QByteArray bootSector(512, 0);
    DWORD bytesRead = 0;

    if (!ReadFile(volumeHandle, bootSector.data(), 512, &bytesRead, nullptr) || bytesRead != 512) {
        return false;
    }

    if (bootSector.mid(3, 8) != QByteArray("NTFS    ", 8)) {
        return false;
    }

    const quint8* data = reinterpret_cast<const quint8*>(bootSector.constData());

    quint16 bytesPerSector = *reinterpret_cast<const quint16*>(data + 0x0B);
    quint8 sectorsPerCluster = data[0x0D];
    quint64 mftClusterNumber = *reinterpret_cast<const quint64*>(data + 0x30);

    bytesPerCluster = bytesPerSector * sectorsPerCluster;
    mftStartLCN = mftClusterNumber;

    return true;
}

QByteArray DeletedDataCollector::readMFTEntry(HANDLE volumeHandle, quint64 entryNumber,
                                              quint64 mftStartLCN, quint64 bytesPerCluster)
{
    const quint32 MFT_ENTRY_SIZE = 1024;

    quint64 entryOffset = (mftStartLCN * bytesPerCluster) + (entryNumber * MFT_ENTRY_SIZE);

    LARGE_INTEGER filePointer;
    filePointer.QuadPart = entryOffset;

    if (!SetFilePointerEx(volumeHandle, filePointer, nullptr, FILE_BEGIN)) {
        return QByteArray();
    }

    QByteArray entryData(MFT_ENTRY_SIZE, 0);
    DWORD bytesRead = 0;

    if (!ReadFile(volumeHandle, entryData.data(), MFT_ENTRY_SIZE, &bytesRead, nullptr) ||
        bytesRead != MFT_ENTRY_SIZE) {
        return QByteArray();
    }

    return entryData;
}

bool DeletedDataCollector::isValidMFTEntry(const QByteArray& entryData)
{
    if (entryData.size() < sizeof(MFTEntryHeader)) {
        return false;
    }

    const MFTEntryHeader* header = reinterpret_cast<const MFTEntryHeader*>(entryData.constData());
    return (header->signature == 0x454C4946);  // "FILE"
}

bool DeletedDataCollector::isDeletedMFTEntry(const MFTEntryHeader& header)
{
    if (!(header.flags & static_cast<quint16>(MFTEntryFlags::FILE_RECORD_IN_USE))) {
        return true;
    }

    if (header.hardLinkCount == 0) {
        return true;
    }

    if (header.sequenceNumber > 100) {
        return true;
    }

    return false;
}

bool DeletedDataCollector::isSuspiciousMFTEntry(const MFTEntryHeader& header, const QByteArray& entryData)
{
    if (isDeletedMFTEntry(header)) {
        return true;
    }

    if (header.firstAttributeOffset == 0 || header.firstAttributeOffset > 1000) {
        return true;
    }

    if (header.usedSize == 0 || header.usedSize > header.totalSize) {
        return true;
    }

    if (header.totalSize != 1024) {
        return true;
    }

    return false;
}

bool DeletedDataCollector::parseMFTEntry(const QByteArray& entryData, quint64 entryNumber, MFTDeletedFileInfo& fileInfo)
{
    const MFTEntryHeader* header = reinterpret_cast<const MFTEntryHeader*>(entryData.constData());

    fileInfo.mftRecordNumber = static_cast<quint32>(entryNumber);
    fileInfo.isDirectory = (header->flags & static_cast<quint16>(MFTEntryFlags::FILE_RECORD_IS_DIRECTORY)) != 0;

    QList<QPair<MFTAttributeType, QByteArray>> attributes = extractAttributes(entryData);

    bool hasStandardInfo = false;
    bool hasFileName = false;

    for (const auto& attr : attributes) {
        MFTAttributeType type = attr.first;
        const QByteArray& data = attr.second;

        switch (type) {
        case MFTAttributeType::STANDARD_INFORMATION:
            if (parseStandardInformation(data, fileInfo)) {
                hasStandardInfo = true;
            }
            break;

        case MFTAttributeType::FILE_NAME:
            if (parseFileName(data, fileInfo)) {
                hasFileName = true;
            }
            break;

        default:
            break;
        }
    }

    if (hasStandardInfo && hasFileName && !fileInfo.fileName.isEmpty()) {
        fileInfo.parseStatus = ParseResult::SUCCESS;
        return true;
    }

    fileInfo.parseStatus = ParseResult::INCOMPLETE_DATA;
    fileInfo.parseErrorMessage = "필수 속성 누락";
    return false;
}

QList<QPair<MFTAttributeType, QByteArray>> DeletedDataCollector::extractAttributes(const QByteArray& entryData)
{
    QList<QPair<MFTAttributeType, QByteArray>> attributes;

    const MFTEntryHeader* header = reinterpret_cast<const MFTEntryHeader*>(entryData.constData());
    int offset = header->firstAttributeOffset;

    while (offset < entryData.size()) {
        if (offset + sizeof(MFTAttributeHeader) > entryData.size()) {
            break;
        }

        const MFTAttributeHeader* attrHeader =
            reinterpret_cast<const MFTAttributeHeader*>(entryData.constData() + offset);

        if (attrHeader->typeCode == 0xFFFFFFFF) {
            break;
        }

        if (attrHeader->length == 0 || offset + attrHeader->length > entryData.size()) {
            break;
        }

        if (attrHeader->nonResident == 0) {
            const MFTResidentAttribute* resAttr =
                reinterpret_cast<const MFTResidentAttribute*>(attrHeader);

            int valueOffset = offset + resAttr->valueOffset;
            int valueLength = resAttr->valueLength;

            if (valueOffset + valueLength <= entryData.size()) {
                QByteArray attrData = entryData.mid(valueOffset, valueLength);
                MFTAttributeType type = static_cast<MFTAttributeType>(attrHeader->typeCode);
                attributes.append(qMakePair(type, attrData));
            }
        }

        offset += attrHeader->length;
    }

    return attributes;
}

bool DeletedDataCollector::parseStandardInformation(const QByteArray& data, MFTDeletedFileInfo& fileInfo)
{
    if (data.size() < sizeof(MFTStandardInformation)) {
        return false;
    }

    const MFTStandardInformation* stdInfo =
        reinterpret_cast<const MFTStandardInformation*>(data.constData());

    fileInfo.creationTime = convertFiletimeToDateTime(stdInfo->creationTime);
    fileInfo.modificationTime = convertFiletimeToDateTime(stdInfo->modificationTime);
    fileInfo.deletionTime = convertFiletimeToDateTime(stdInfo->mftModificationTime);
    fileInfo.accessTime = convertFiletimeToDateTime(stdInfo->accessTime);
    fileInfo.fileAttributes = stdInfo->fileAttributes;

    return true;
}

bool DeletedDataCollector::parseFileName(const QByteArray& data, MFTDeletedFileInfo& fileInfo)
{
    if (data.size() < sizeof(MFTFileName)) {
        return false;
    }

    const MFTFileName* fnInfo = reinterpret_cast<const MFTFileName*>(data.constData());

    fileInfo.parentMftRecord = static_cast<quint32>(fnInfo->parentDirectoryMFT & 0xFFFFFFFF);
    fileInfo.fileSize = static_cast<qint64>(fnInfo->realSize);
    fileInfo.allocatedSize = static_cast<qint64>(fnInfo->allocatedSize);

    int nameOffset = sizeof(MFTFileName);
    int nameLength = fnInfo->fileNameLength * 2;

    if (nameOffset + nameLength <= data.size()) {
        fileInfo.fileName = extractFileNameFromMFTData(data, nameOffset, nameLength);
        fileInfo.fullPath = reconstructFullPath(fileInfo.parentMftRecord, fileInfo.fileName);
        return true;
    }

    return false;
}

QString DeletedDataCollector::extractFileNameFromMFTData(const QByteArray& data, int offset, int length)
{
    if (offset + length > data.size()) {
        return QString();
    }

    const ushort* nameData = reinterpret_cast<const ushort*>(data.constData() + offset);
    int charCount = length / 2;

    // ✅ UTF-16 raw data에서 invalid surrogate 정제
    QString rawFileName = QString::fromUtf16(nameData, charCount);
    return sanitizeFileName(rawFileName);
}

QString DeletedDataCollector::reconstructFullPath(quint32 parentMftRecord, const QString& fileName)
{
    // 1. 부모 디렉토리 경로 찾기
    QString parentPath = getParentDirectory(parentMftRecord);

    // 2. 전체 경로 조합
    if (!parentPath.isEmpty()) {
        return parentPath + "\\" + fileName;
    }

    // 3. 부모를 찾을 수 없는 경우 기본 처리
    return QString("C:\\[UNKNOWN_PATH]\\%1").arg(fileName);
}

QString DeletedDataCollector::getParentDirectory(quint32 parentMftRecord)
{
    // 간단한 방식: MFT 레코드 번호만 표시
    return QString("[MFT_%1]").arg(parentMftRecord);
}

const QList<MFTDeletedFileInfo>& DeletedDataCollector::getMFTDeletedFiles() const
{
    return mftDeletedFiles;
}

int DeletedDataCollector::getMFTDeletedFileCount() const
{
    return mftDeletedFiles.size();
}

void DeletedDataCollector::printComprehensiveSummary() const
{
    int totalRecycleBin = deletedFiles.size();
    int totalMFT = mftDeletedFiles.size();
    int totalFiles = totalRecycleBin + totalMFT;

    qDebug() << "=== 통합 삭제 파일 분석 결과 ===";
    qDebug() << "총 발견된 삭제 파일:" << totalFiles << "개";
    qDebug() << "  - Recycle Bin:" << totalRecycleBin << "개";
    qDebug() << "  - MFT 잔여 데이터:" << totalMFT << "개";

    if (totalRecycleBin > 0) {
        qDebug() << "\n=== Recycle Bin 파일 목록 ===";
        int index = 1;
        for (const auto& file : deletedFiles) {
            qDebug() << QString("[RB-%1] %2").arg(index++, 2).arg(file.originalFileName);
            qDebug() << QString("    경로: %1").arg(file.originalPath);
            qDebug() << QString("    삭제된 시간: %1").arg(file.deletedTime.toString("yyyy-MM-dd hh:mm:ss"));
            qDebug() << QString("    삭제한 사용자 SID: %1").arg(file.userSID);
            qDebug() << QString("    크기: %L1 바이트").arg(file.originalFileSize);
            qDebug() << "";
        }
    }

    if (totalMFT > 0) {
        qDebug() << "=== MFT 삭제 파일 목록 ===";
        int index = 1;
        for (const auto& file : mftDeletedFiles) {
            QString typePrefix = file.isDirectory ? "[DIR]" : "[FILE]";
            qDebug() << QString("[MFT-%1] %2 %3").arg(index++, 2).arg(typePrefix).arg(file.fileName);

            // 기본 정보
            if (!file.fullPath.isEmpty() && file.fullPath != file.fileName) {
                qDebug() << QString("    경로: %1").arg(file.fullPath);
            }

            // 모든 시간 정보
            if (file.creationTime.isValid()) {
                qDebug() << QString("    생성 시간: %1").arg(file.creationTime.toString("yyyy-MM-dd hh:mm:ss"));
            }
            if (file.modificationTime.isValid()) {
                qDebug() << QString("    수정 시간: %1").arg(file.modificationTime.toString("yyyy-MM-dd hh:mm:ss"));
            }
            if (file.deletionTime.isValid()) {
                qDebug() << QString("    MFT 수정 시간: %1").arg(file.deletionTime.toString("yyyy-MM-dd hh:mm:ss"));
            }
            if (file.accessTime.isValid()) {
                qDebug() << QString("    접근 시간: %1").arg(file.accessTime.toString("yyyy-MM-dd hh:mm:ss"));
            }

            // 크기 정보
            qDebug() << QString("    파일 크기: %L1 바이트").arg(file.fileSize);
            qDebug() << QString("    할당 크기: %L1 바이트").arg(file.allocatedSize);

            // MFT 관련 정보
            qDebug() << QString("    MFT 레코드 번호: %1").arg(file.mftRecordNumber);
            qDebug() << QString("    부모 MFT 레코드: %1").arg(file.parentMftRecord);

            // 파일 속성 (16진수로 표시)
            qDebug() << QString("    파일 속성: 0x%1").arg(file.fileAttributes, 8, 16, QChar('0'));

            qDebug() << "";  // 파일 간 구분용 빈 줄
        }
    }

    if (totalFiles == 0) {
        qDebug() << "삭제된 파일을 찾을 수 없습니다.";
    }
}

// =============================================================================
// JSON 변환 함수들 구현 (기존 deleteddatacollector.cpp 파일 끝에 추가)
// =============================================================================

QJsonObject DeletedDataCollector::toJsonObject() const {
    QJsonObject result;
    /*
    // 메타데이터
    result["collection_info"] = QJsonObject({
        {"module_name", "Deleted_Files"},
        {"collection_time", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"total_recycle_bin_files", static_cast<int>(deletedFiles.size())},
        {"total_mft_deleted_files", static_cast<int>(mftDeletedFiles.size())},
        {"total_files", static_cast<int>(deletedFiles.size() + mftDeletedFiles.size())},
        {"version", "1.0"}
    });
    */
    // 데이터 소스 플래그들
    QJsonObject sourceFlags;
    sourceFlags["has_recycle_bin_data"] = !deletedFiles.isEmpty();
    sourceFlags["has_mft_data"] = !mftDeletedFiles.isEmpty();
    result["data_sources"] = sourceFlags;

    // Recycle Bin 삭제 파일들
    if (!deletedFiles.isEmpty()) {
        QJsonArray recycleBinArray;
        for (int i = 0; i < deletedFiles.size(); i++) {
            const RecycleBinFileInfo& file = deletedFiles[i];

            QJsonObject fileObj = recycleBinFileInfoToJson(file);
            fileObj["file_index"] = i + 1;
            recycleBinArray.append(fileObj);
        }
        result["recycle_bin_files"] = recycleBinArray;
    }

    // MFT 삭제 파일들
    if (!mftDeletedFiles.isEmpty()) {
        QJsonArray mftArray;
        for (int i = 0; i < mftDeletedFiles.size(); i++) {
            const MFTDeletedFileInfo& file = mftDeletedFiles[i];

            QJsonObject fileObj = mftDeletedFileInfoToJson(file);
            fileObj["file_index"] = i + 1;
            mftArray.append(fileObj);
        }
        result["mft_deleted_files"] = mftArray;
    }

    // 통계 정보
    QJsonObject statistics;
    statistics["recycle_bin_count"] = static_cast<int>(deletedFiles.size());
    statistics["mft_deleted_count"] = static_cast<int>(mftDeletedFiles.size());
    statistics["total_deleted_files"] = static_cast<int>(deletedFiles.size() + mftDeletedFiles.size());

    // Recycle Bin 파일 크기 통계
    qint64 totalRecycleBinSize = 0;
    for (const auto& file : deletedFiles) {
        totalRecycleBinSize += file.originalFileSize;
    }
    statistics["total_recycle_bin_size_bytes"] = static_cast<qint64>(totalRecycleBinSize);

    // MFT 파일 크기 통계
    qint64 totalMftSize = 0;
    for (const auto& file : mftDeletedFiles) {
        totalMftSize += file.fileSize;
    }
    statistics["total_mft_size_bytes"] = static_cast<qint64>(totalMftSize);
    statistics["total_deleted_size_bytes"] = static_cast<qint64>(totalRecycleBinSize + totalMftSize);

    result["statistics"] = statistics;

    return result;
}

QJsonObject DeletedDataCollector::recycleBinFileInfoToJson(const RecycleBinFileInfo& info) const {
    QJsonObject result;

    // 기본 파일 정보
    result["current_i_file_path"] = sanitizeFileName(info.currentIFilePath);  // ✅ 정제
    result["current_r_file_path"] = sanitizeFileName(info.currentRFilePath);  // ✅ 정제
    result["original_path"] = sanitizeFileName(info.originalPath);  // ✅ 정제
    result["original_file_name"] = sanitizeFileName(info.originalFileName);  // ✅ 정제
    result["user_sid"] = info.userSID;
    result["original_file_size"] = static_cast<qint64>(info.originalFileSize);

    // 시간 정보
    if (info.deletedTime.isValid()) {
        result["deleted_time"] = info.deletedTime.toString(Qt::ISODate);
        result["deleted_time_timestamp"] = info.deletedTime.toMSecsSinceEpoch();
    } else {
        result["deleted_time"] = QString();
        result["deleted_time_timestamp"] = 0;
    }

    // 버전 및 상태 정보
    result["recycle_bin_version"] = windowsRecycleBinVersionToString(info.version);
    result["parse_status"] = parseResultToString(info.parseStatus);

    if (!info.parseErrorMessage.isEmpty()) {
        result["parse_error_message"] = info.parseErrorMessage;
    }

    return result;
}

QJsonObject DeletedDataCollector::mftDeletedFileInfoToJson(const MFTDeletedFileInfo& info) const {
    QJsonObject result;

    // 기본 파일 정보
    result["mft_record_number"] = static_cast<int>(info.mftRecordNumber);
    result["file_name"] = sanitizeFileName(info.fileName);  // ✅ 정제
    result["full_path"] = sanitizeFileName(info.fullPath);  // ✅ 정제
    result["file_size"] = static_cast<qint64>(info.fileSize);
    result["allocated_size"] = static_cast<qint64>(info.allocatedSize);
    result["is_directory"] = info.isDirectory;

    // 시간 정보들
    if (info.creationTime.isValid()) {
        result["creation_time"] = info.creationTime.toString(Qt::ISODate);
        result["creation_time_timestamp"] = info.creationTime.toMSecsSinceEpoch();
    } else {
        result["creation_time"] = QString();
        result["creation_time_timestamp"] = 0;
    }

    if (info.modificationTime.isValid()) {
        result["modification_time"] = info.modificationTime.toString(Qt::ISODate);
        result["modification_time_timestamp"] = info.modificationTime.toMSecsSinceEpoch();
    } else {
        result["modification_time"] = QString();
        result["modification_time_timestamp"] = 0;
    }

    if (info.deletionTime.isValid()) {
        result["deletion_time"] = info.deletionTime.toString(Qt::ISODate);
        result["deletion_time_timestamp"] = info.deletionTime.toMSecsSinceEpoch();
    } else {
        result["deletion_time"] = QString();
        result["deletion_time_timestamp"] = 0;
    }

    if (info.accessTime.isValid()) {
        result["access_time"] = info.accessTime.toString(Qt::ISODate);
        result["access_time_timestamp"] = info.accessTime.toMSecsSinceEpoch();
    } else {
        result["access_time"] = QString();
        result["access_time_timestamp"] = 0;
    }

    // MFT 특화 정보
    result["file_attributes"] = QString("0x%1").arg(info.fileAttributes, 8, 16, QChar('0'));
    result["parent_mft_record"] = static_cast<int>(info.parentMftRecord);

    // 파싱 상태
    result["parse_status"] = parseResultToString(info.parseStatus);
    if (!info.parseErrorMessage.isEmpty()) {
        result["parse_error_message"] = info.parseErrorMessage;
    }

    return result;
}

QString DeletedDataCollector::parseResultToString(ParseResult result) const {
    switch (result) {
    case ParseResult::SUCCESS:
        return "SUCCESS";
    case ParseResult::FILE_NOT_FOUND:
        return "FILE_NOT_FOUND";
    case ParseResult::INCOMPLETE_DATA:
        return "INCOMPLETE_DATA";
    case ParseResult::MALFORMED_PATH:
        return "MALFORMED_PATH";
    case ParseResult::CORRUPTED_HEADER:
        return "CORRUPTED_HEADER";
    case ParseResult::INVALID_TIMESTAMP:
        return "INVALID_TIMESTAMP";
    case ParseResult::UNSUPPORTED_VERSION:
        return "UNSUPPORTED_VERSION";
    default:
        return "UNKNOWN";
    }
}

QString DeletedDataCollector::windowsRecycleBinVersionToString(WindowsRecycleBinVersion version) const {
    switch (version) {
    case WindowsRecycleBinVersion::VISTA_WIN8:
        return "VISTA_WIN8";
    case WindowsRecycleBinVersion::WIN10_PLUS:
        return "WIN10_PLUS";
    case WindowsRecycleBinVersion::UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

// ========================================================================
// UTF-16 Invalid Surrogate 정제 함수 (deleteddatacollector.cpp 끝에 추가)
// ========================================================================

QString DeletedDataCollector::sanitizeFileName(const QString& fileName) const
{
    if (fileName.isEmpty()) {
        return fileName;
    }

    QString sanitized;
    for (int i = 0; i < fileName.length(); ++i) {
        QChar ch = fileName[i];

        // Orphaned surrogate 확인
        if (ch.isSurrogate()) {
            // High surrogate (D800-DBFF)
            if (ch.isHighSurrogate()) {
                // 다음 문자가 Low surrogate인지 확인
                if (i + 1 < fileName.length() && fileName[i + 1].isLowSurrogate()) {
                    // 유효한 surrogate pair
                    sanitized += ch;
                    continue;
                }
            }

            // Low surrogate (DC00-DFFF) 또는 orphaned high surrogate
            // Replacement character (U+FFFD)로 교체
            qDebug() << "[DeletedDataCollector] Found invalid surrogate in:" << fileName
                     << "at position" << i << "code:" << QString::number(ch.unicode(), 16);
            sanitized += QChar(0xFFFD);
        } else {
            sanitized += ch;
        }
    }

    return sanitized;
}
