#ifndef PREFETCH_H
#define PREFETCH_H

#include "pch.h"

/**
 * @brief 간단한 프리패치 수집기 (포렌식 에이전트용)
 * - 순수 C++ 클래스 (Qt MOC 없음)
 * - 동기식 처리로 안정성 확보
 * - 구조체 기반 데이터 수집
 */
class PrefetchCollector {
public:
// Windows 10 프리패치 구조체 (표 구조 정확 반영)
#pragma pack(push, 1)
    struct Win10PrefetchStructure {
        // 0x00~0x4F: Header 부분
        uint32_t format_version;            // 0x00: Format version (0x1E)
        uint8_t signature[4];               // 0x04: Signature "SCCA"
        uint32_t unknown1;                  // 0x08: Unknown
        uint32_t executable_file_size;      // 0x0C: Executable File size
        uint8_t executable_file_name[60];   // 0x10~0x4B: Executable File name (UTF-16)
        uint32_t prefetch_hash;             // 0x4C: Prefetch Hash

        // 0x50~0x7F: 메타데이터 부분
        uint32_t file_metrics_offset;       // 0x50: File metrics array offset
        uint32_t num_file_metrics;          // 0x54: Num of file metrics entries
        uint32_t trace_chains_offset;       // 0x58: Trace chains array offset
        uint32_t num_trace_chains;          // 0x5C: Num of trace chains entries
        uint32_t filename_strings_offset;   // 0x60: Filename strings offset
        uint32_t filename_strings_size;     // 0x64: Filename strings size
        uint32_t volumes_info_offset;       // 0x68: Volumes information offset
        uint32_t num_volumes;               // 0x6C: Number of volumes
        uint32_t volumes_info_size;         // 0x70: Volumes information size
        uint8_t unknown_empty[8];           // 0x74: Unknown (Empty values)

        // 0x80~0xBF: 실행 시간 부분 (8개 슬롯)
        uint64_t last_run_times[8];         // 0x80: Last run times

        // 0xC0~0xFF: 나머지 정보
        uint8_t empty_values[8];            // 0xC0: Empty values
        uint32_t run_count;                 // 0xC8: Run count
        uint32_t unknown2;                  // 0xCC: Unknown
        uint32_t unknown3;                  // 0xD0: Unknown
        uint32_t hash_string_offset;        // 0xD4: Hash string offset
        uint32_t hash_string_size;          // 0xD8: Hash string size
        uint8_t empty_values2[20];          // 0xDC~0xEF: Empty values
    };
#pragma pack(pop)

    // 수집된 프리패치 데이터 (구조체 호출용)
    struct PrefetchFileData {
        QString file_path;                  // 원본 파일 경로
        QString application_name;           // 추출된 실행파일명
        Win10PrefetchStructure structure;   // 원본 구조체 데이터
        QList<QDateTime> execution_times;   // 변환된 실행 시간들
        qint64 file_size;                   // 파일 크기
        bool is_compressed;                 // 압축 파일 여부
        bool parse_success;                 // 파싱 성공 여부
        QString error_message;              // 에러 메시지 (실패시)

        PrefetchFileData() {
            memset(&structure, 0, sizeof(Win10PrefetchStructure));
            file_size = 0;
            is_compressed = false;
            parse_success = false;
        }
    };

private:
    QList<PrefetchFileData> all_prefetch_data_;  // 모든 수집된 데이터
    int total_files_;                            // 총 파일 수
    int processed_files_;                        // 처리된 파일 수
    int success_count_;                          // 성공 개수
    int failed_count_;                           // 실패 개수

    // 파일 처리 함수들
    bool processFile(const QString& file_path, PrefetchFileData& data);
    bool parseWin10File(const QByteArray& file_data, PrefetchFileData& data);
    QByteArray decompressMAM(const QByteArray& compressed_data);

    // 안전한 메모리 접근 헬퍼 함수들
    bool safeReadUint32(const uint8_t* data, int data_size, int offset, uint32_t& value);
    bool safeReadUint64(const uint8_t* data, int data_size, int offset, uint64_t& value);
    bool safeReadBytes(const uint8_t* data, int data_size, int offset, uint8_t* dest, int count);
    bool validateDataSize(int data_size, int required_size);
    bool validateDecompressedData(const QByteArray& data);

    // 데이터 추출 함수들
    QString extractApplicationName(const uint8_t* name_data);
    QDateTime convertFileTime(uint64_t filetime) const;  // const 함수로 변경
    bool isMAMCompressed(const QByteArray& data);

    // JSON 헬퍼 함수들 (const 함수들)
    QJsonObject prefetchFileDataToJson(const PrefetchFileData& data) const;
    QJsonObject win10PrefetchStructureToJson(const Win10PrefetchStructure& structure) const;
    QJsonArray executionTimesToJson(const QList<QDateTime>& times) const;

public:
    PrefetchCollector();
    ~PrefetchCollector();

    // 메인 수집 함수 (동기식 - 안정적)
    bool collectFromDirectory(const QString& directory = "C:\\Windows\\Prefetch");

    // 구조체 데이터 접근 (서버 전송용)
    const QList<PrefetchFileData>& getAllPrefetchData() const { return all_prefetch_data_; }
    const PrefetchFileData& getPrefetchData(int index) const { return all_prefetch_data_.at(index); }
    int getDataCount() const { return all_prefetch_data_.size(); }

    // 상태 확인
    int getTotalFiles() const { return total_files_; }
    int getProcessedFiles() const { return processed_files_; }
    int getSuccessCount() const { return success_count_; }
    int getFailedCount() const { return failed_count_; }

    // 데이터 초기화
    void clearData();

    // 검증용 출력 (모든 데이터)
    void printAllCollectedData() const;
    void printCollectionSummary() const;

    // JSON 변환 함수 (const 함수)
    QJsonObject toJsonObject() const;
};

#endif // PREFETCH_H
