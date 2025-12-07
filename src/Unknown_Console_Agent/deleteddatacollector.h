#ifndef DELETEDDATACOLLECTOR_H
#define DELETEDDATACOLLECTOR_H

#include "pch.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

// Windows Recycle Bin 버전
enum class WindowsRecycleBinVersion {
    UNKNOWN = 0,
    VISTA_WIN8 = 1,
    WIN10_PLUS = 2
};

// 파싱 결과 상태
enum class ParseResult {
    SUCCESS = 0,
    FILE_NOT_FOUND,
    INCOMPLETE_DATA,
    MALFORMED_PATH,
    CORRUPTED_HEADER,
    INVALID_TIMESTAMP,
    UNSUPPORTED_VERSION
};

// MFT 속성 타입
enum class MFTAttributeType : quint32 {
    STANDARD_INFORMATION = 0x10,
    ATTRIBUTE_LIST = 0x20,
    FILE_NAME = 0x30,
    OBJECT_ID = 0x40,
    SECURITY_DESCRIPTOR = 0x50,
    VOLUME_NAME = 0x60,
    VOLUME_INFORMATION = 0x70,
    DATA = 0x80,
    INDEX_ROOT = 0x90,
    INDEX_ALLOCATION = 0xA0,
    BITMAP = 0xB0,
    REPARSE_POINT = 0xC0,
    EA_INFORMATION = 0xD0,
    EA = 0xE0,
    PROPERTY_SET = 0xF0,
    LOGGED_UTILITY_STREAM = 0x100,
    END_MARKER = 0xFFFFFFFF
};

// MFT 엔트리 플래그
enum class MFTEntryFlags : quint16 {
    FILE_RECORD_IN_USE = 0x0001,
    FILE_RECORD_IS_DIRECTORY = 0x0002
};

// Recycle Bin 파일 정보
struct RecycleBinFileInfo {
    QString currentIFilePath;           // 현재 $I 파일 경로
    QString currentRFilePath;           // 현재 $R 파일 경로
    QString originalPath;               // 원본 파일 경로
    QString originalFileName;           // 원본 파일명
    QString userSID;                    // 사용자 SID
    qint64 originalFileSize;            // 원본 파일 크기
    QDateTime deletedTime;              // 삭제 시간
    WindowsRecycleBinVersion version;   // Recycle Bin 버전
    ParseResult parseStatus;            // 파싱 상태
    QString parseErrorMessage;          // 파싱 에러 메시지

    RecycleBinFileInfo() : originalFileSize(0), version(WindowsRecycleBinVersion::UNKNOWN),
        parseStatus(ParseResult::SUCCESS) {}
};

// MFT 삭제 파일 정보
struct MFTDeletedFileInfo {
    quint32 mftRecordNumber;            // MFT 레코드 번호
    QString fileName;                   // 파일명
    QString fullPath;                   // 전체 경로
    qint64 fileSize;                    // 파일 크기
    qint64 allocatedSize;               // 할당된 크기
    QDateTime creationTime;             // 생성 시간
    QDateTime modificationTime;         // 수정 시간
    QDateTime deletionTime;             // 삭제 시간 (MFT 수정 시간)
    QDateTime accessTime;               // 접근 시간
    quint32 fileAttributes;             // 파일 속성
    quint32 parentMftRecord;            // 부모 디렉토리 MFT 레코드
    bool isDirectory;                   // 디렉토리 여부
    ParseResult parseStatus;            // 파싱 상태
    QString parseErrorMessage;          // 파싱 에러 메시지

    MFTDeletedFileInfo() : mftRecordNumber(0), fileSize(0), allocatedSize(0),
        fileAttributes(0), parentMftRecord(0), isDirectory(false),
        parseStatus(ParseResult::SUCCESS) {}
};

// Windows MFT 구조체들
#pragma pack(push, 1)
struct MFTEntryHeader {
    quint32 signature;                  // "FILE"
    quint16 updateSequenceOffset;
    quint16 updateSequenceSize;
    quint64 logSequenceNumber;
    quint16 sequenceNumber;
    quint16 hardLinkCount;
    quint16 firstAttributeOffset;
    quint16 flags;
    quint32 usedSize;
    quint32 totalSize;
    quint64 baseFileRecord;
    quint16 nextAttributeID;
    quint16 padding;
    quint32 mftRecordNumber;
};

struct MFTAttributeHeader {
    quint32 typeCode;
    quint32 length;
    quint8 nonResident;
    quint8 nameLength;
    quint16 nameOffset;
    quint16 flags;
    quint16 attributeID;
};

struct MFTResidentAttribute {
    MFTAttributeHeader header;
    quint32 valueLength;
    quint16 valueOffset;
    quint16 flags;
};

struct MFTStandardInformation {
    quint64 creationTime;
    quint64 modificationTime;
    quint64 mftModificationTime;
    quint64 accessTime;
    quint32 fileAttributes;
    quint32 maxVersions;
    quint32 versionNumber;
    quint32 classID;
    quint32 ownerID;
    quint32 securityID;
    quint64 quotaCharged;
    quint64 updateSequenceNumber;
};

struct MFTFileName {
    quint64 parentDirectoryMFT;
    quint64 creationTime;
    quint64 modificationTime;
    quint64 mftModificationTime;
    quint64 accessTime;
    quint64 allocatedSize;
    quint64 realSize;
    quint32 flags;
    quint32 reparseValue;
    quint8 fileNameLength;
    quint8 nameType;
    // 파일명 데이터가 이어짐 (UTF-16)
};
#pragma pack(pop)

class DeletedDataCollector : public QObject
{
    Q_OBJECT

public:
    explicit DeletedDataCollector(QObject *parent = nullptr);
    ~DeletedDataCollector();

    // 주요 수집 메서드
    bool collectDeletedFiles();                     // Recycle Bin만 수집
    bool collectFromMFT();                          // MFT만 수집
    bool collectAllDeletedFiles();                  // 통합 수집

    // 결과 조회 메서드
    const QList<RecycleBinFileInfo>& getDeletedFiles() const;
    const QList<MFTDeletedFileInfo>& getMFTDeletedFiles() const;
    int getDeletedFileCount() const;
    int getMFTDeletedFileCount() const;

    // JSON 변환 메서드
    QJsonObject toJsonObject() const;

    // 결과 출력 메서드
    void printDeviceSummary() const;                // Recycle Bin 결과만
    void printComprehensiveSummary() const;         // 통합 결과

    // 유틸리티 메서드
    void clearResults();

private:
    // 데이터 저장
    QList<RecycleBinFileInfo> deletedFiles;
    QList<MFTDeletedFileInfo> mftDeletedFiles;
    QHash<quint32, QString> mftRecordToPath;        // MFT 레코드 번호 → 경로 매핑

    // === JSON 헬퍼 함수들 ===
    QJsonObject recycleBinFileInfoToJson(const RecycleBinFileInfo& info) const;
    QJsonObject mftDeletedFileInfoToJson(const MFTDeletedFileInfo& info) const;
    QString parseResultToString(ParseResult result) const;
    QString windowsRecycleBinVersionToString(WindowsRecycleBinVersion version) const;

    // === Recycle Bin 관련 메서드 ===
    bool scanRecycleBinDirectory(const QString& recycleBinPath);
    bool scanUserSIDDirectory(const QString& sidPath, const QString& userSID);
    bool isValidSID(const QString& sidString);
    WindowsRecycleBinVersion detectFileVersion(const QString& iFilePath);
    RecycleBinFileInfo parseIFile(const QString& iFilePath, const QString& userSID);
    RecycleBinFileInfo parseVistaFormat(QDataStream& stream, const QString& iFilePath, const QString& userSID);
    RecycleBinFileInfo parseWindows10Format(QDataStream& stream, const QString& iFilePath, const QString& userSID);
    QString findCorrespondingRFile(const QString& iFilePath);
    QString extractFileNameFromPath(const QString& fullPath);
    bool validateParsedData(const RecycleBinFileInfo& info);

    // === MFT 관련 메서드 ===
    bool processDriveMFT(const QString& driveLetter);
    HANDLE openVolumeHandle(const QString& driveLetter);
    bool getMFTInfo(HANDLE volumeHandle, quint64& mftStartLCN, quint64& bytesPerCluster);
    QByteArray readMFTEntry(HANDLE volumeHandle, quint64 entryNumber, quint64 mftStartLCN, quint64 bytesPerCluster);
    bool isValidMFTEntry(const QByteArray& entryData);
    bool isDeletedMFTEntry(const MFTEntryHeader& header);
    bool isSuspiciousMFTEntry(const MFTEntryHeader& header, const QByteArray& entryData);
    bool parseMFTEntry(const QByteArray& entryData, quint64 entryNumber, MFTDeletedFileInfo& fileInfo);
    bool parseMFTEntryRelaxed(const QByteArray& entryData, quint64 entryNumber, MFTDeletedFileInfo& fileInfo, bool hasStdInfo, bool hasFileNameAttr);
    QList<QPair<MFTAttributeType, QByteArray>> extractAttributes(const QByteArray& entryData);
    bool parseStandardInformation(const QByteArray& data, MFTDeletedFileInfo& fileInfo);
    bool parseFileName(const QByteArray& data, MFTDeletedFileInfo& fileInfo);
    QString extractFileNameFromMFTData(const QByteArray& data, int offset, int length);
    QString reconstructFullPath(quint32 parentMftRecord, const QString& fileName);
    QString getParentDirectory(quint32 parentMftRecord);
    bool validateMFTDeletedData(const MFTDeletedFileInfo& info);

    // === 공통 유틸리티 메서드 ===
    QStringList getAllDrivePaths() const;
    QDateTime convertFiletimeToDateTime(quint64 filetime);

    QString sanitizeFileName(const QString& fileName) const;
};

#endif // DELETEDDATACOLLECTOR_H
