#ifndef LNKCOLLECTOR_H
#define LNKCOLLECTOR_H

#include "pch.h"

class LnkCollector : public QObject
{
    Q_OBJECT

public:
    // LNK 파일 헤더 구조체 (76 bytes)
    struct LnkHeader {
        quint32 header_size;           // 0x0000004C (76)
        quint8  link_clsid[16];        // Link CLSID
        quint32 link_flags;            // Link flags
        quint32 file_attributes;       // File attributes
        quint64 creation_time;         // Creation time (FILETIME)
        quint64 access_time;           // Access time (FILETIME)
        quint64 write_time;            // Write time (FILETIME)
        quint32 file_size;             // File size
        quint32 icon_index;            // Icon index
        quint32 show_command;          // Show command
        quint16 hot_key;               // Hot key
        quint16 reserved1;             // Reserved
        quint32 reserved2;             // Reserved
        quint32 reserved3;             // Reserved
    };

    // 볼륨 정보 구조체
    struct VolumeInfo {
        quint32 volume_id_size;        // Volume ID size
        quint32 drive_type;            // Drive type
        quint32 drive_serial_number;   // Drive serial number
        quint32 volume_label_offset;   // Volume label offset
        QString volume_label;          // Volume label
    };

    // LinkInfo 구조체
    struct LinkInfo {
        quint32 link_info_size;        // LinkInfo size
        quint32 link_info_header_size; // LinkInfo header size
        quint32 link_info_flags;       // LinkInfo flags
        quint32 volume_id_offset;      // Volume ID offset
        quint32 local_base_path_offset; // Local base path offset
        quint32 common_network_relative_link_offset; // Common network relative link offset
        quint32 common_path_suffix_offset; // Common path suffix offset
        VolumeInfo volume_info;        // Volume information
        QString local_base_path;       // Local base path
        QString common_path_suffix;    // Common path suffix
    };

    // 완전한 LNK 파일 정보 구조체
    struct LnkFileInfo {
        QString file_path;             // LNK 파일 경로
        QString file_name;             // LNK 파일 이름
        qint64 file_size;              // LNK 파일 크기
        QDateTime creation_time;       // LNK 파일 생성 시간
        QDateTime modification_time;   // LNK 파일 수정 시간
        QDateTime access_time;         // LNK 파일 접근 시간

        // LNK 내부 데이터
        LnkHeader header;              // LNK 헤더
        LinkInfo link_info;            // Link 정보
        QString target_path;           // 대상 파일 경로
        QString working_directory;     // 작업 디렉토리
        QString command_line_args;     // 명령행 인수
        QString icon_location;         // 아이콘 위치
        QString description;           // 설명
        QString relative_path;         // 상대 경로

        // 타겟 파일 정보 (원본 파일)
        QDateTime target_creation_time;   // 타겟 생성 시간
        QDateTime target_access_time;     // 타겟 접근 시간
        QDateTime target_write_time;      // 타겟 쓰기 시간
        quint32 target_file_size;         // 타겟 파일 크기
        quint32 target_file_attributes;   // 타겟 파일 속성

        // 추가 메타데이터
        bool is_file_link;             // 파일 링크 여부
        bool is_directory_link;        // 디렉토리 링크 여부
        bool has_target_id_list;       // Target ID List 존재 여부
        bool has_link_info;            // Link Info 존재 여부
        bool has_name;                 // Name 존재 여부
        bool has_relative_path;        // Relative Path 존재 여부
        bool has_working_dir;          // Working Directory 존재 여부
        bool has_arguments;            // Arguments 존재 여부
        bool has_icon_location;        // Icon Location 존재 여부
        bool target_exists;            // 타겟 파일 존재 여부

        // 포렌식 관련
        QString drive_serial;          // 드라이브 시리얼 번호
        QString volume_label;          // 볼륨 레이블
        QString machine_id;            // 머신 ID (NetBIOS name)
    };

    explicit LnkCollector(QObject *parent = nullptr);
    ~LnkCollector();

    // 주요 수집 메서드
    bool collectLnkFiles();                                       // 모든 LNK 파일 수집
    bool collectFromDirectory(const QString& directory_path);     // 특정 디렉토리에서 수집
    bool parseLnkFile(const QString& lnk_file_path);              // 개별 LNK 파일 파싱

    // 데이터 접근
    const QList<LnkFileInfo>& getAllLnkData() const { return collected_lnk_files_; }
    int getCollectedCount() const { return collected_lnk_files_.size(); }

    // 상태 확인
    qint64 getTotalProcessedFiles() const { return total_processed_files_; }
    qint64 getTotalSuccessfulParses() const { return total_successful_parses_; }

    // 출력 메서드
    void printLnkFiles() const;                                   // 수집된 LNK 파일들 출력

    // JSON 변환 함수 (새로 추가)
    QJsonObject toJsonObject() const;

private:
    // 내부 파싱 메서드
    bool parseShellLinkHeader(QDataStream& stream, LnkFileInfo& lnk_info);
    bool parseLinkInfo(QDataStream& stream, LnkFileInfo& lnk_info);
    bool parseStringData(QDataStream& stream, LnkFileInfo& lnk_info);
    bool parseExtraData(QDataStream& stream, LnkFileInfo& lnk_info);
    bool parseVolumeInfo(QDataStream& stream, VolumeInfo& volume_info);

    // 유틸리티 메서드
    QString readUnicodeString(QDataStream& stream, quint16 length);
    QString readAnsiString(QDataStream& stream, quint16 length);
    QDateTime filetimeToDateTime(quint64 filetime) const;
    QString formatFileAttributes(quint32 attributes) const;
    QString formatLinkFlags(quint32 flags) const;
    QString formatShowCommand(quint32 show_command) const;
    QString formatDriveType(quint32 drive_type) const;
    QString extractMachineId(const QByteArray& tracker_data);

    // 검증 메서드
    bool isValidLnkFile(const QString& file_path);
    bool checkTargetExists(const QString& target_path);
    void validateAndEnrichData(LnkFileInfo& lnk_info);

    // JSON 헬퍼 함수들 (새로 추가)
    QJsonObject lnkFileInfoToJson(const LnkFileInfo& info) const;
    QJsonObject lnkHeaderToJson(const LnkHeader& header) const;
    QJsonObject linkInfoToJson(const LinkInfo& linkInfo) const;
    QJsonObject volumeInfoToJson(const VolumeInfo& volumeInfo) const;

    // 멤버 변수
    QList<LnkFileInfo> collected_lnk_files_;                     // 수집된 LNK 파일들
    QStringList search_directories_;                             // 검색할 디렉토리 목록
    qint64 total_processed_files_;                               // 처리된 총 파일 수
    qint64 total_successful_parses_;                             // 성공한 파싱 수
    QDateTime collection_start_time_;                            // 수집 시작 시간
    QDateTime collection_end_time_;                              // 수집 종료 시간

    // 상수
    static const quint32 LNK_SIGNATURE = 0x0000004C;            // LNK 파일 시그니처
    static const QByteArray LNK_CLSID;                          // LNK CLSID
    static const int MAX_PATH_LENGTH = 32767;                   // 최대 경로 길이

    // 플래그 상수들
    enum LinkFlags {
        HasTargetIDList = 0x00000001,
        HasLinkInfo = 0x00000002,
        HasName = 0x00000004,
        HasRelativePath = 0x00000008,
        HasWorkingDir = 0x00000010,
        HasArguments = 0x00000020,
        HasIconLocation = 0x00000040,
        IsUnicode = 0x00000080,
        ForceNoLinkInfo = 0x00000100,
        HasExpString = 0x00000200,
        RunInSeparateProcess = 0x00000400,
        HasDarwinID = 0x00001000,
        RunAsUser = 0x00002000,
        HasExpIcon = 0x00004000,
        NoPidlAlias = 0x00008000,
        RunWithShimLayer = 0x00020000,
        ForceNoLinkTrack = 0x00040000,
        EnableTargetMetadata = 0x00080000,
        DisableLinkPathTracking = 0x00100000,
        DisableKnownFolderRelativeTracking = 0x00200000,
        NoKFAlias = 0x00400000,
        AllowLinkToLink = 0x00800000,
        UnaliasOnSave = 0x01000000,
        PreferEnvironmentPath = 0x02000000,
        KeepLocalIDListForUNCTarget = 0x04000000
    };

    enum ShowCommands {
        HSW_HIDE = 0,
        HSW_NORMAL = 1,
        HSW_SHOWMINIMIZED = 2,
        HSW_SHOWMAXIMIZED = 3,
        HSW_SHOWNOACTIVATE = 4,
        HSW_SHOW = 5,
        HSW_MINIMIZE = 6,
        HSW_SHOWMINNOACTIVE = 7,
        HSW_SHOWNA = 8,
        HSW_RESTORE = 9,
        HSW_SHOWDEFAULT = 10
    };
};

#endif // LNKCOLLECTOR_H
