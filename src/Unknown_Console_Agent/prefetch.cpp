#include "prefetch.h"

PrefetchCollector::PrefetchCollector()
    : total_files_(0), processed_files_(0), success_count_(0), failed_count_(0) {
}

PrefetchCollector::~PrefetchCollector() {
    clearData();
}

bool PrefetchCollector::collectFromDirectory(const QString& directory) {
    clearData();

    qDebug() << "[PrefetchCollector] 수집 시작:" << directory;

    QDir dir(directory);
    if (!dir.exists()) {
        qDebug() << "[PrefetchCollector] 디렉토리 없음:" << directory;
        return false;
    }

    // .pf 파일들 찾기
    QStringList filters;
    filters << "*.pf";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::Readable);

    total_files_ = files.size();
    qDebug() << "[PrefetchCollector] 발견된 .pf 파일:" << total_files_ << "개";

    if (total_files_ == 0) {
        qDebug() << "[PrefetchCollector] 프리패치 파일이 없습니다.";
        return false;
    }

    // 모든 파일 처리 (동기식)
    for (const QFileInfo& fileInfo : files) {
        PrefetchFileData data;

        if (processFile(fileInfo.absoluteFilePath(), data)) {
            success_count_++;
            //    qDebug() << "[PrefetchCollector] 성공:" << data.application_name;
        } else {
            failed_count_++;
            //    qDebug() << "[PrefetchCollector] 실패:" << fileInfo.fileName() << "-" << data.error_message;
        }

        // 성공/실패 관계없이 모든 데이터 저장
        all_prefetch_data_.append(data);
        processed_files_++;

        // 진행 상황 출력 (50개마다)
        if (processed_files_ % 50 == 0 || processed_files_ == total_files_) {
            //qDebug() << QString("[PrefetchCollector] 진행: %1/%2 (%3%)")
            //                .arg(processed_files_).arg(total_files_)
            //                .arg(processed_files_ * 100 / total_files_);
        }
    }

    //qDebug() << "[PrefetchCollector] 수집 완료 - 성공:" << success_count_
    //         << "실패:" << failed_count_;

    return success_count_ > 0;
}

bool PrefetchCollector::processFile(const QString& file_path, PrefetchFileData& data) {
    data.file_path = file_path;

    try {
        // 파일 읽기
        QFile file(file_path);
        if (!file.open(QIODevice::ReadOnly)) {
            data.error_message = "파일 열기 실패";
            return false;
        }

        QByteArray file_data = file.readAll();
        file.close();

        if (file_data.size() < 256) {
            data.error_message = "파일 크기 부족";
            return false;
        }

        data.file_size = file_data.size();

        // 압축 확인
        data.is_compressed = isMAMCompressed(file_data);

        // 압축 해제 시도 (필요시)
        QByteArray parse_data = file_data;
        if (data.is_compressed) {
            QByteArray decompressed = decompressMAM(file_data);
            if (!decompressed.isEmpty() && validateDecompressedData(decompressed)) {
                parse_data = decompressed;
                //qDebug() << "[PrefetchCollector] 압축 해제 성공:" << file_data.size() << "->" << decompressed.size();
            } else {
                data.error_message = "압축 해제 실패 또는 데이터 검증 실패";
                return false;
            }
        }

        // Windows 10 구조 파싱
        return parseWin10File(parse_data, data);

    } catch (const std::exception& e) {
        data.error_message = QString("예외 발생: %1").arg(e.what());
        return false;
    } catch (...) {
        data.error_message = "알 수 없는 예외 발생";
        return false;
    }
}

bool PrefetchCollector::parseWin10File(const QByteArray& file_data, PrefetchFileData& data) {
    if (!validateDataSize(file_data.size(), sizeof(Win10PrefetchStructure))) {
        data.error_message = QString("파일 크기가 구조체보다 작음: %1 < %2")
                                 .arg(file_data.size()).arg(sizeof(Win10PrefetchStructure));
        return false;
    }

    try {
        const uint8_t* raw_data = reinterpret_cast<const uint8_t*>(file_data.constData());
        int data_size = file_data.size();

        // 구조체 초기화
        memset(&data.structure, 0, sizeof(Win10PrefetchStructure));

        // === 0x00~0x4F: Header 부분 개별 읽기 ===

        // 0x00: Format version
        if (!safeReadUint32(raw_data, data_size, 0x00, data.structure.format_version)) {
            data.error_message = "Format version 읽기 실패";
            return false;
        }

        // 버전 확인 (Windows 10/11: 0x1E, 0x1F)
        if (data.structure.format_version != 0x1E && data.structure.format_version != 0x1F && data.structure.format_version != 0x1A) {
            data.error_message = QString("지원되지 않는 버전: 0x%1").arg(data.structure.format_version, 0, 16);
            return false;
        }

        // 0x04: Signature "SCCA"
        if (!safeReadBytes(raw_data, data_size, 0x04, data.structure.signature, 4)) {
            data.error_message = "Signature 읽기 실패";
            return false;
        }

        // SCCA 시그니처 확인
        if (memcmp(data.structure.signature, "SCCA", 4) != 0) {
            data.error_message = "SCCA 시그니처 불일치";
            return false;
        }

        // 0x08: Unknown1
        safeReadUint32(raw_data, data_size, 0x08, data.structure.unknown1);

        // 0x0C: Executable File size
        safeReadUint32(raw_data, data_size, 0x0C, data.structure.executable_file_size);

        // 0x10~0x4B: Executable File name (UTF-16)
        if (!safeReadBytes(raw_data, data_size, 0x10, data.structure.executable_file_name, 60)) {
            data.error_message = "Executable File name 읽기 실패";
            return false;
        }

        // 0x4C: Prefetch Hash
        safeReadUint32(raw_data, data_size, 0x4C, data.structure.prefetch_hash);

        // === 0x50~0x7F: 메타데이터 부분 ===

        safeReadUint32(raw_data, data_size, 0x50, data.structure.file_metrics_offset);
        safeReadUint32(raw_data, data_size, 0x54, data.structure.num_file_metrics);
        safeReadUint32(raw_data, data_size, 0x58, data.structure.trace_chains_offset);
        safeReadUint32(raw_data, data_size, 0x5C, data.structure.num_trace_chains);
        safeReadUint32(raw_data, data_size, 0x60, data.structure.filename_strings_offset);
        safeReadUint32(raw_data, data_size, 0x64, data.structure.filename_strings_size);
        safeReadUint32(raw_data, data_size, 0x68, data.structure.volumes_info_offset);
        safeReadUint32(raw_data, data_size, 0x6C, data.structure.num_volumes);
        safeReadUint32(raw_data, data_size, 0x70, data.structure.volumes_info_size);

        // 0x74: Unknown (Empty values)
        safeReadBytes(raw_data, data_size, 0x74, data.structure.unknown_empty, 8);

        // === 0x80~0xBF: 실행 시간 부분 (8개 슬롯) ===
        for (int i = 0; i < 8; i++) {
            safeReadUint64(raw_data, data_size, 0x80 + i * 8, data.structure.last_run_times[i]);
        }

        // === 0xC0~0xFF: 나머지 정보 ===

        safeReadBytes(raw_data, data_size, 0xC0, data.structure.empty_values, 8);
        safeReadUint32(raw_data, data_size, 0xC8, data.structure.run_count);
        safeReadUint32(raw_data, data_size, 0xCC, data.structure.unknown2);
        safeReadUint32(raw_data, data_size, 0xD0, data.structure.unknown3);
        safeReadUint32(raw_data, data_size, 0xD4, data.structure.hash_string_offset);
        safeReadUint32(raw_data, data_size, 0xD8, data.structure.hash_string_size);
        safeReadBytes(raw_data, data_size, 0xDC, data.structure.empty_values2, 20);

        // === 후처리: 데이터 추출 ===

        // 애플리케이션 이름 추출
        data.application_name = extractApplicationName(data.structure.executable_file_name);

        // 실행 시간들 변환
        data.execution_times.clear();
        for (int i = 0; i < 8; i++) {
            if (data.structure.last_run_times[i] != 0) {
                QDateTime dt = convertFileTime(data.structure.last_run_times[i]);
                if (dt.isValid()) {
                    data.execution_times.append(dt);
                }
            }
        }

        data.parse_success = true;
        return true;

    } catch (const std::exception& e) {
        data.error_message = QString("구조체 파싱 예외: %1").arg(e.what());
        return false;
    } catch (...) {
        data.error_message = "구조체 파싱 중 알 수 없는 예외";
        return false;
    }
}

// === 안전한 메모리 접근 헬퍼 함수들 ===

bool PrefetchCollector::safeReadUint32(const uint8_t* data, int data_size, int offset, uint32_t& value) {
    if (offset < 0 || offset + 4 > data_size) {
        return false;
    }

    // Little-endian에서 읽어서 호스트 바이트 순서로 변환
    value = qFromLittleEndian(*reinterpret_cast<const uint32_t*>(data + offset));
    return true;
}

bool PrefetchCollector::safeReadUint64(const uint8_t* data, int data_size, int offset, uint64_t& value) {
    if (offset < 0 || offset + 8 > data_size) {
        return false;
    }

    // Little-endian에서 읽어서 호스트 바이트 순서로 변환
    value = qFromLittleEndian(*reinterpret_cast<const uint64_t*>(data + offset));
    return true;
}

bool PrefetchCollector::safeReadBytes(const uint8_t* data, int data_size, int offset, uint8_t* dest, int count) {
    if (offset < 0 || offset + count > data_size || !dest) {
        return false;
    }

    memcpy(dest, data + offset, count);
    return true;
}

bool PrefetchCollector::validateDataSize(int data_size, int required_size) {
    return data_size >= required_size;
}

bool PrefetchCollector::validateDecompressedData(const QByteArray& data) {
    if (data.size() < 256) {
        return false;
    }

    // SCCA 시그니처 확인
    if (data.size() >= 8) {
        const char* signature = data.constData() + 4;
        if (memcmp(signature, "SCCA", 4) == 0) {
            return true;
        }
    }

    return false;
}

// === MAM 압축 해제 ===

QByteArray PrefetchCollector::decompressMAM(const QByteArray& compressed_data) {
    if (compressed_data.size() < 8) {
        return QByteArray();
    }

    try {
        // NTDLL 함수 포인터
        typedef NTSTATUS (WINAPI *RtlDecompressBufferEx_t)(
            USHORT CompressionFormat,
            PUCHAR UncompressedBuffer,
            ULONG UncompressedBufferSize,
            PUCHAR CompressedBuffer,
            ULONG CompressedBufferSize,
            PULONG FinalUncompressedSize,
            PVOID WorkSpace
            );

        typedef NTSTATUS (WINAPI *RtlGetCompressionWorkSpaceSize_t)(
            USHORT CompressionFormat,
            PULONG CompressBufferWorkSpaceSize,
            PULONG CompressFragmentWorkSpaceSize
            );

        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        if (!ntdll) return QByteArray();

        auto RtlDecompressBufferEx = reinterpret_cast<RtlDecompressBufferEx_t>(
            GetProcAddress(ntdll, "RtlDecompressBufferEx"));
        auto RtlGetCompressionWorkSpaceSize = reinterpret_cast<RtlGetCompressionWorkSpaceSize_t>(
            GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize"));

        if (!RtlDecompressBufferEx || !RtlGetCompressionWorkSpaceSize) {
            return QByteArray();
        }

        // MAM 헤더 파싱
        const uint8_t* data = reinterpret_cast<const uint8_t*>(compressed_data.constData());
        uint32_t uncompressed_size = qFromLittleEndian(*reinterpret_cast<const uint32_t*>(data + 4));
        uint8_t algorithm = data[3];

        // 크기 검증 (최대 10MB)
        if (uncompressed_size == 0 || uncompressed_size > 10 * 1024 * 1024) {
            return QByteArray();
        }

        // 압축 형식 결정
        uint16_t compression_format = 0;
        switch (algorithm) {
        case 0x01: case 0x81: compression_format = 0x0003; break; // XPRESS
        case 0x04: case 0x84: compression_format = 0x0004; break; // XPRESS_HUFF
        case 0x02: case 0x82: compression_format = 0x0002; break; // LZX
        default: return QByteArray();
        }

        // 워크스페이스 크기 계산
        ULONG workspace_size = 0, fragment_size = 0;
        if (RtlGetCompressionWorkSpaceSize(compression_format, &workspace_size, &fragment_size) != 0) {
            return QByteArray();
        }

        // 메모리 할당
        std::vector<uint8_t> workspace(workspace_size);
        QByteArray output(uncompressed_size, Qt::Uninitialized);

        // 압축 해제
        size_t header_size = 8;
        if (algorithm & 0x80) header_size += 4; // 체크섬 건너뛰기

        if (compressed_data.size() <= static_cast<int>(header_size)) {
            return QByteArray();
        }

        ULONG final_size = 0;
        NTSTATUS status = RtlDecompressBufferEx(
            compression_format,
            reinterpret_cast<PUCHAR>(output.data()),
            uncompressed_size,
            const_cast<PUCHAR>(data + header_size),
            compressed_data.size() - header_size,
            &final_size,
            workspace.data()
            );

        if (status == 0) {
            output.resize(final_size);
            return output;
        }

    } catch (...) {
        // 예외 발생시 빈 배열 반환
    }

    return QByteArray();
}

QString PrefetchCollector::extractApplicationName(const uint8_t* name_data) {
    try {
        // UTF-16 문자열 변환 (60바이트 = 30 문자)
        QString name = QString::fromUtf16(reinterpret_cast<const ushort*>(name_data), 30);

        // null terminator 찾기 및 제거
        name.remove(QChar(0));

        return name.trimmed().isEmpty() ? "UNKNOWN" : name.trimmed();

    } catch (...) {
        return "UNKNOWN";
    }
}

QDateTime PrefetchCollector::convertFileTime(uint64_t filetime) const {  // const 추가 - 핵심 수정!
    if (filetime == 0) return QDateTime();

    try {
        // Windows FILETIME을 Unix timestamp로 변환
        const qint64 FILETIME_EPOCH_DIFF = 11644473600LL;
        const qint64 FILETIME_TICKS_PER_SECOND = 10000000LL;

        qint64 unix_seconds = (filetime / FILETIME_TICKS_PER_SECOND) - FILETIME_EPOCH_DIFF;
        return QDateTime::fromSecsSinceEpoch(unix_seconds, Qt::UTC);

    } catch (...) {
        return QDateTime();
    }
}

bool PrefetchCollector::isMAMCompressed(const QByteArray& data) {
    if (data.size() < 4) return false;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.constData());
    return (bytes[0] == 0x4D && bytes[1] == 0x41 && bytes[2] == 0x4D);
}

void PrefetchCollector::clearData() {
    all_prefetch_data_.clear();
    total_files_ = 0;
    processed_files_ = 0;
    success_count_ = 0;
    failed_count_ = 0;
}

void PrefetchCollector::printAllCollectedData() const {
    qDebug() << "\n╔══════════════════════════════════════════════════════════════╗";
    qDebug() << "║                  모든 수집된 프리패치 데이터                     ║";
    qDebug() << "╚══════════════════════════════════════════════════════════════╝";

    qDebug() << "총 수집된 파일:" << all_prefetch_data_.size() << "개\n";

    for (int i = 0; i < all_prefetch_data_.size(); i++) {
        const PrefetchFileData& data = all_prefetch_data_.at(i);

        //    qDebug() << QString("[%1] %2").arg(i + 1, 3).arg(data.application_name);
        //    qDebug() << "  파일:" << QFileInfo(data.file_path).fileName();
        //    qDebug() << "  상태:" << (data.parse_success ? "성공" : "실패");

        if (data.parse_success) {
            qDebug() << "  버전:" << QString("0x%1").arg(data.structure.format_version, 0, 16);
            qDebug() << "  해시:" << QString("0x%1").arg(data.structure.prefetch_hash, 8, 16, QChar('0'));
            qDebug() << "  실행횟수:" << data.structure.run_count;
            qDebug() << "  실행시간개수:" << data.execution_times.size();
            qDebug() << "  파일메트릭오프셋:" << QString("0x%1").arg(data.structure.file_metrics_offset, 0, 16);
            qDebug() << "  파일메트릭개수:" << data.structure.num_file_metrics;
            qDebug() << "  파일명오프셋:" << QString("0x%1").arg(data.structure.filename_strings_offset, 0, 16);
            qDebug() << "  파일명크기:" << data.structure.filename_strings_size;
            qDebug() << "  볼륨오프셋:" << QString("0x%1").arg(data.structure.volumes_info_offset, 0, 16);
            qDebug() << "  볼륨개수:" << data.structure.num_volumes;
            qDebug() << "  압축:" << (data.is_compressed ? "압축됨" : "비압축");

            // 모든 실행 시간들 출력
            if (!data.execution_times.isEmpty()) {
                qDebug() << "  실행시간들:";
                for (int j = 0; j < data.execution_times.size(); j++) {
                    QString timeLabel = (j == 0) ? " (최근)" : "";
                    qDebug() << QString("    [%1] %2%3")
                                    .arg(j + 1)
                                    .arg(data.execution_times[j].toString("yyyy-MM-dd hh:mm:ss"))
                                    .arg(timeLabel);
                }
            } else {
                qDebug() << "  실행시간들: (없음)";
            }
        } else {
            qDebug() << "  에러:" << data.error_message;
        }

        qDebug() << "  크기:" << data.file_size << "bytes";
        qDebug() << "";
    }
}

void PrefetchCollector::printCollectionSummary() const {
    qDebug() << "\n=== 수집 요약 ===";
    qDebug() << "총 파일:" << total_files_;
    qDebug() << "처리완료:" << processed_files_;
    qDebug() << "성공:" << success_count_;
    qDebug() << "실패:" << failed_count_;

    int compressed_count = 0;
    for (const auto& data : all_prefetch_data_) {
        if (data.is_compressed) compressed_count++;
    }
    qDebug() << "압축파일:" << compressed_count;

    if (total_files_ > 0) {
        qDebug() << "성공률:" << QString("%1%").arg(success_count_ * 100 / total_files_);
    }
}

// =============================================================================
// JSON 변환 함수들 구현 (에러 수정 완료)
// =============================================================================

QJsonObject PrefetchCollector::toJsonObject() const {
    QJsonObject result;
    /*
    // 메타데이터
    result["collection_info"] = QJsonObject({
        {"module_name", "Prefetch"},
        {"collection_time", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"total_files", total_files_},
        {"processed_files", processed_files_},
        {"success_count", success_count_},
        {"failed_count", failed_count_},
        {"version", "1.0"}
    });
    */
    // 모든 프리패치 파일 데이터
    QJsonArray prefetchArray;
    for (int i = 0; i < all_prefetch_data_.size(); i++) {
        const PrefetchFileData& data = all_prefetch_data_[i];

        QJsonObject prefetchObj = prefetchFileDataToJson(data);
        prefetchObj["file_index"] = i + 1;

        prefetchArray.append(prefetchObj);
    }

    result["prefetch_files"] = prefetchArray;

    return result;
}

QJsonObject PrefetchCollector::prefetchFileDataToJson(const PrefetchFileData& data) const {
    QJsonObject obj;

    // 기본 파일 정보
    obj["file_path"] = data.file_path;
    obj["application_name"] = data.application_name;
    obj["file_size"] = static_cast<qint64>(data.file_size);
    obj["is_compressed"] = data.is_compressed;
    obj["parse_success"] = data.parse_success;

    // 에러 정보 (실패시)
    if (!data.parse_success) {
        obj["error_message"] = data.error_message;
    }

    // 실행 시간들
    obj["execution_times"] = executionTimesToJson(data.execution_times);
    obj["execution_count"] = data.execution_times.size();

    // 원본 구조체 데이터 (성공시)
    if (data.parse_success) {
        obj["structure"] = win10PrefetchStructureToJson(data.structure);
    }

    return obj;
}

QJsonObject PrefetchCollector::win10PrefetchStructureToJson(const Win10PrefetchStructure& structure) const {
    QJsonObject obj;

    // Header 부분 (0x00~0x4F)
    obj["format_version"] = QString::number(structure.format_version, 16);
    obj["signature"] = QString::fromLatin1(reinterpret_cast<const char*>(structure.signature), 4);
    obj["executable_file_size"] = static_cast<qint64>(structure.executable_file_size);

    // 🔥 핵심 수정: null byte가 포함된 UTF-16 문자열을 안전하게 변환
    QString cleanName = QString::fromUtf16(
        reinterpret_cast<const char16_t*>(structure.executable_file_name), 30);
    // null terminator와 제어 문자 제거
    cleanName.remove(QChar(0));
    cleanName = cleanName.trimmed();

    // 빈 문자열이면 "UNKNOWN"으로 설정
    obj["executable_file_name"] = cleanName.isEmpty() ? "UNKNOWN" : cleanName;

    obj["prefetch_hash"] = QString::number(structure.prefetch_hash, 16);

    // 메타데이터 부분 (0x50~0x7F)
    obj["file_metrics_offset"] = static_cast<qint64>(structure.file_metrics_offset);
    obj["num_file_metrics"] = static_cast<qint64>(structure.num_file_metrics);
    obj["trace_chains_offset"] = static_cast<qint64>(structure.trace_chains_offset);
    obj["num_trace_chains"] = static_cast<qint64>(structure.num_trace_chains);
    obj["filename_strings_offset"] = static_cast<qint64>(structure.filename_strings_offset);
    obj["filename_strings_size"] = static_cast<qint64>(structure.filename_strings_size);
    obj["volumes_info_offset"] = static_cast<qint64>(structure.volumes_info_offset);
    obj["num_volumes"] = static_cast<qint64>(structure.num_volumes);
    obj["volumes_info_size"] = static_cast<qint64>(structure.volumes_info_size);

    // 실행 시간들 (8개 슬롯) - JSON 배열로 변환
    QJsonArray runTimesArray;
    for (int i = 0; i < 8; i++) {
        if (structure.last_run_times[i] != 0) {
            QDateTime dt = convertFileTime(structure.last_run_times[i]);
            if (dt.isValid()) {
                runTimesArray.append(dt.toString(Qt::ISODate));
            }
        }
    }
    obj["last_run_times"] = runTimesArray;

    // 기타 정보
    obj["run_count"] = static_cast<qint64>(structure.run_count);

    return obj;
}

QJsonArray PrefetchCollector::executionTimesToJson(const QList<QDateTime>& times) const {
    QJsonArray array;
    for (const QDateTime& time : times) {
        if (time.isValid()) {
            array.append(time.toString(Qt::ISODate));  // 단순한 문자열만
        }
    }
    return array;
}
