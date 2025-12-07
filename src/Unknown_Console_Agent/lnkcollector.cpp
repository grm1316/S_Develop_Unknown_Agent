#include "lnkcollector.h"

// LNK CLSID 상수 정의
const QByteArray LnkCollector::LNK_CLSID = QByteArray::fromHex("01140200000000000000000046000000");

LnkCollector::LnkCollector(QObject *parent)
    : QObject(parent)
    , total_processed_files_(0)
    , total_successful_parses_(0)
{
    // 기본 검색 디렉토리 설정
    search_directories_ << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    search_directories_ << QDir::homePath() + "/AppData/Roaming/Microsoft/Windows/Recent";
    search_directories_ << QDir::homePath() + "/AppData/Roaming/Microsoft/Windows/Start Menu";
    search_directories_ << "C:/ProgramData/Microsoft/Windows/Start Menu";
}

LnkCollector::~LnkCollector()
{
    collected_lnk_files_.clear();
}

bool LnkCollector::collectLnkFiles()
{
    collection_start_time_ = QDateTime::currentDateTime();
    collected_lnk_files_.clear();
    total_processed_files_ = 0;
    total_successful_parses_ = 0;

    bool overall_success = false;

    // 각 검색 디렉토리에서 수집
    for (const QString& directory : search_directories_) {
        if (collectFromDirectory(directory)) {
            overall_success = true;
        }
    }

    collection_end_time_ = QDateTime::currentDateTime();
    return overall_success;
}

bool LnkCollector::collectFromDirectory(const QString& directory_path)
{
    QDir directory(directory_path);

    if (!directory.exists()) {
        return false;
    }

    // .lnk 파일만 필터링
    QStringList name_filters;
    name_filters << "*.lnk";

    // 재귀적으로 하위 디렉토리도 검색
    QDirIterator iterator(directory_path, name_filters, QDir::Files, QDirIterator::Subdirectories);

    bool found_any = false;

    while (iterator.hasNext()) {
        QString lnk_file_path = iterator.next();
        total_processed_files_++;

        if (parseLnkFile(lnk_file_path)) {
            total_successful_parses_++;
            found_any = true;
        }
    }

    return found_any;
}

bool LnkCollector::parseLnkFile(const QString& lnk_file_path)
{
    if (!isValidLnkFile(lnk_file_path)) {
        return false;
    }

    QFile file(lnk_file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);

    LnkFileInfo lnk_info;

    // 기본 파일 정보 설정
    QFileInfo file_info(lnk_file_path);
    lnk_info.file_path = lnk_file_path;
    lnk_info.file_name = file_info.fileName();
    lnk_info.file_size = file_info.size();
    lnk_info.creation_time = file_info.birthTime();
    lnk_info.modification_time = file_info.lastModified();
    lnk_info.access_time = file_info.lastRead();

    try {
        // 1. Shell Link Header 파싱 (76 bytes)
        if (!parseShellLinkHeader(stream, lnk_info)) {
            return false;
        }

        // 2. LinkTargetIDList 건너뛰기 (HasTargetIDList 플래그가 있을 때)
        if (lnk_info.has_target_id_list) {
            quint16 id_list_size;
            stream >> id_list_size;
            stream.skipRawData(id_list_size);
        }

        // 3. LinkInfo 파싱 (HasLinkInfo 플래그가 있을 때)
        if (lnk_info.has_link_info) {
            parseLinkInfo(stream, lnk_info);
        }

        // 4. String Data 파싱
        parseStringData(stream, lnk_info);

        // 5. Extra Data 파싱
        parseExtraData(stream, lnk_info);

        // 6. 데이터 검증 및 보강
        validateAndEnrichData(lnk_info);

        // 수집된 데이터에 추가
        collected_lnk_files_.append(lnk_info);

        return true;

    } catch (...) {
        return false;
    }
}

bool LnkCollector::parseShellLinkHeader(QDataStream& stream, LnkFileInfo& lnk_info)
{
    LnkHeader& header = lnk_info.header;

    // Header Size (4 bytes) - 항상 0x0000004C (76)
    stream >> header.header_size;
    if (header.header_size != LNK_SIGNATURE) {
        return false;
    }

    // Link CLSID (16 bytes)
    stream.readRawData(reinterpret_cast<char*>(header.link_clsid), 16);

    // Link Flags (4 bytes)
    stream >> header.link_flags;

    // 플래그에 따른 boolean 설정
    lnk_info.has_target_id_list = (header.link_flags & HasTargetIDList) != 0;
    lnk_info.has_link_info = (header.link_flags & HasLinkInfo) != 0;
    lnk_info.has_name = (header.link_flags & HasName) != 0;
    lnk_info.has_relative_path = (header.link_flags & HasRelativePath) != 0;
    lnk_info.has_working_dir = (header.link_flags & HasWorkingDir) != 0;
    lnk_info.has_arguments = (header.link_flags & HasArguments) != 0;
    lnk_info.has_icon_location = (header.link_flags & HasIconLocation) != 0;

    // File Attributes (4 bytes)
    stream >> header.file_attributes;
    lnk_info.target_file_attributes = header.file_attributes;

    // File Times (각 8 bytes)
    stream >> header.creation_time;
    stream >> header.access_time;
    stream >> header.write_time;

    lnk_info.target_creation_time = filetimeToDateTime(header.creation_time);
    lnk_info.target_access_time = filetimeToDateTime(header.access_time);
    lnk_info.target_write_time = filetimeToDateTime(header.write_time);

    // File Size (4 bytes)
    stream >> header.file_size;
    lnk_info.target_file_size = header.file_size;

    // Icon Index (4 bytes)
    stream >> header.icon_index;

    // Show Command (4 bytes)
    stream >> header.show_command;

    // Hot Key (2 bytes)
    stream >> header.hot_key;

    // Reserved fields (10 bytes)
    stream >> header.reserved1;
    stream >> header.reserved2;
    stream >> header.reserved3;

    return true;
}

bool LnkCollector::parseLinkInfo(QDataStream& stream, LnkFileInfo& lnk_info)
{
    LinkInfo& link_info = lnk_info.link_info;

    // LinkInfo Size (4 bytes)
    stream >> link_info.link_info_size;

    qint64 link_info_start = stream.device()->pos() - 4;

    // LinkInfo Header Size (4 bytes)
    stream >> link_info.link_info_header_size;

    // LinkInfo Flags (4 bytes)
    stream >> link_info.link_info_flags;

    // Volume ID Offset (4 bytes)
    stream >> link_info.volume_id_offset;

    // Local Base Path Offset (4 bytes)
    stream >> link_info.local_base_path_offset;

    // Common Network Relative Link Offset (4 bytes)
    stream >> link_info.common_network_relative_link_offset;

    // Common Path Suffix Offset (4 bytes)
    stream >> link_info.common_path_suffix_offset;

    // Volume Info 파싱
    if (link_info.volume_id_offset > 0) {
        qint64 current_pos = stream.device()->pos();
        stream.device()->seek(link_info_start + link_info.volume_id_offset);

        parseVolumeInfo(stream, link_info.volume_info);
        lnk_info.drive_serial = QString("0x%1").arg(link_info.volume_info.drive_serial_number, 8, 16, QChar('0'));
        lnk_info.volume_label = link_info.volume_info.volume_label;

        stream.device()->seek(current_pos);
    }

    // Local Base Path 읽기
    if (link_info.local_base_path_offset > 0) {
        qint64 current_pos = stream.device()->pos();
        stream.device()->seek(link_info_start + link_info.local_base_path_offset);

        QByteArray path_data;
        char ch;
        while (stream.readRawData(&ch, 1) == 1 && ch != '\0') {
            path_data.append(ch);
        }
        link_info.local_base_path = QString::fromLocal8Bit(path_data);
        lnk_info.target_path = link_info.local_base_path;

        stream.device()->seek(current_pos);
    }

    // Common Path Suffix 읽기
    if (link_info.common_path_suffix_offset > 0) {
        qint64 current_pos = stream.device()->pos();
        stream.device()->seek(link_info_start + link_info.common_path_suffix_offset);

        QByteArray suffix_data;
        char ch;
        while (stream.readRawData(&ch, 1) == 1 && ch != '\0') {
            suffix_data.append(ch);
        }
        link_info.common_path_suffix = QString::fromLocal8Bit(suffix_data);

        if (!link_info.local_base_path.isEmpty() && !link_info.common_path_suffix.isEmpty()) {
            lnk_info.target_path = link_info.local_base_path + "\\" + link_info.common_path_suffix;
        }

        stream.device()->seek(current_pos);
    }

    // LinkInfo 끝으로 이동
    stream.device()->seek(link_info_start + link_info.link_info_size);

    return true;
}

bool LnkCollector::parseStringData(QDataStream& stream, LnkFileInfo& lnk_info)
{
    bool is_unicode = (lnk_info.header.link_flags & IsUnicode) != 0;

    // Name String
    if (lnk_info.has_name) {
        quint16 name_length;
        stream >> name_length;
        if (is_unicode) {
            lnk_info.description = readUnicodeString(stream, name_length);
        } else {
            lnk_info.description = readAnsiString(stream, name_length);
        }
    }

    // Relative Path String
    if (lnk_info.has_relative_path) {
        quint16 relative_path_length;
        stream >> relative_path_length;
        if (is_unicode) {
            lnk_info.relative_path = readUnicodeString(stream, relative_path_length);
        } else {
            lnk_info.relative_path = readAnsiString(stream, relative_path_length);
        }
    }

    // Working Directory String
    if (lnk_info.has_working_dir) {
        quint16 working_dir_length;
        stream >> working_dir_length;
        if (is_unicode) {
            lnk_info.working_directory = readUnicodeString(stream, working_dir_length);
        } else {
            lnk_info.working_directory = readAnsiString(stream, working_dir_length);
        }
    }

    // Command Line Arguments String
    if (lnk_info.has_arguments) {
        quint16 arguments_length;
        stream >> arguments_length;
        if (is_unicode) {
            lnk_info.command_line_args = readUnicodeString(stream, arguments_length);
        } else {
            lnk_info.command_line_args = readAnsiString(stream, arguments_length);
        }
    }

    // Icon Location String
    if (lnk_info.has_icon_location) {
        quint16 icon_location_length;
        stream >> icon_location_length;
        if (is_unicode) {
            lnk_info.icon_location = readUnicodeString(stream, icon_location_length);
        } else {
            lnk_info.icon_location = readAnsiString(stream, icon_location_length);
        }
    }

    return true;
}

bool LnkCollector::parseExtraData(QDataStream& stream, LnkFileInfo& lnk_info)
{
    while (!stream.atEnd()) {
        quint32 block_size;
        stream >> block_size;

        if (block_size < 4) {
            break; // Terminal block
        }

        quint32 block_signature;
        stream >> block_signature;

        // 블록 타입에 따른 처리
        switch (block_signature) {
        case 0xA0000003: // TrackerDataBlock
        {
            QByteArray tracker_data(block_size - 8, 0);
            stream.readRawData(tracker_data.data(), block_size - 8);

            lnk_info.machine_id = extractMachineId(tracker_data);
            break;
        }
        default:
            // 알려지지 않은 블록은 건너뛰기
            stream.skipRawData(block_size - 8);
            break;
        }
    }

    return true;
}

bool LnkCollector::parseVolumeInfo(QDataStream& stream, VolumeInfo& volume_info)
{
    // Volume ID Size (4 bytes)
    stream >> volume_info.volume_id_size;

    qint64 volume_start = stream.device()->pos() - 4;

    // Drive Type (4 bytes)
    stream >> volume_info.drive_type;

    // Drive Serial Number (4 bytes)
    stream >> volume_info.drive_serial_number;

    // Volume Label Offset (4 bytes)
    stream >> volume_info.volume_label_offset;

    // Volume Label 읽기
    if (volume_info.volume_label_offset > 0 && volume_info.volume_label_offset < volume_info.volume_id_size) {
        qint64 current_pos = stream.device()->pos();
        stream.device()->seek(volume_start + volume_info.volume_label_offset);

        QByteArray label_data;
        char ch;
        while (stream.readRawData(&ch, 1) == 1 && ch != '\0') {
            label_data.append(ch);
        }
        volume_info.volume_label = QString::fromLocal8Bit(label_data);

        stream.device()->seek(current_pos);
    }

    // Volume Info 끝으로 이동
    stream.device()->seek(volume_start + volume_info.volume_id_size);

    return true;
}

QString LnkCollector::readUnicodeString(QDataStream& stream, quint16 length)
{
    if (length == 0) return QString();

    QByteArray data(length * 2, 0);
    stream.readRawData(data.data(), length * 2);

    return QString::fromUtf16(reinterpret_cast<const char16_t*>(data.data()), length);
}

QString LnkCollector::readAnsiString(QDataStream& stream, quint16 length)
{
    if (length == 0) return QString();

    QByteArray data(length, 0);
    stream.readRawData(data.data(), length);

    return QString::fromLocal8Bit(data);
}

QDateTime LnkCollector::filetimeToDateTime(quint64 filetime) const
{
    if (filetime == 0) return QDateTime();

    // FILETIME은 1601년 1월 1일부터의 100나노초 단위
    // QDateTime::fromMSecsSinceEpoch는 1970년 1월 1일부터의 밀리초 단위
    static const qint64 EPOCH_DIFF = 11644473600000LL; // 1601년 -> 1970년 (밀리초)

    qint64 milliseconds = static_cast<qint64>(filetime / 10000) - EPOCH_DIFF;
    return QDateTime::fromMSecsSinceEpoch(milliseconds, Qt::UTC);
}

QString LnkCollector::formatFileAttributes(quint32 attributes) const
{
    QStringList attr_list;

    if (attributes & 0x00000001) attr_list << "READONLY";
    if (attributes & 0x00000002) attr_list << "HIDDEN";
    if (attributes & 0x00000004) attr_list << "SYSTEM";
    if (attributes & 0x00000010) attr_list << "DIRECTORY";
    if (attributes & 0x00000020) attr_list << "ARCHIVE";
    if (attributes & 0x00000080) attr_list << "NORMAL";

    return attr_list.join(" | ");
}

QString LnkCollector::formatLinkFlags(quint32 flags) const
{
    QStringList flag_list;

    if (flags & HasTargetIDList) flag_list << "HasTargetIDList";
    if (flags & HasLinkInfo) flag_list << "HasLinkInfo";
    if (flags & HasName) flag_list << "HasName";
    if (flags & HasRelativePath) flag_list << "HasRelativePath";
    if (flags & HasWorkingDir) flag_list << "HasWorkingDir";
    if (flags & HasArguments) flag_list << "HasArguments";
    if (flags & HasIconLocation) flag_list << "HasIconLocation";
    if (flags & IsUnicode) flag_list << "IsUnicode";

    return flag_list.join(" | ");
}

QString LnkCollector::formatShowCommand(quint32 show_command) const
{
    switch (show_command) {
    case HSW_HIDE: return "숨김";
    case HSW_NORMAL: return "일반";
    case HSW_SHOWMINIMIZED: return "최소화";
    case HSW_SHOWMAXIMIZED: return "최대화";
    case HSW_SHOW: return "표시";
    case HSW_MINIMIZE: return "최소화";
    case HSW_RESTORE: return "복원";
    case HSW_SHOWDEFAULT: return "기본값";
    default: return QString("알수없음(%1)").arg(show_command);
    }
}

QString LnkCollector::formatDriveType(quint32 drive_type) const
{
    switch (drive_type) {
    case 0: return "UNKNOWN";
    case 1: return "NO_ROOT_DIR";
    case 2: return "REMOVABLE";
    case 3: return "FIXED";
    case 4: return "REMOTE";
    case 5: return "CDROM";
    case 6: return "RAMDISK";
    default: return QString("알수없음(%1)").arg(drive_type);
    }
}

QString LnkCollector::extractMachineId(const QByteArray& tracker_data)
{
    // TrackerDataBlock structure: Length(4), Version(4), MachineID(16)
    // MachineID starts at offset 8.
    if (tracker_data.size() >= 24) { // 8 (header) + 16 (machine_id)
        QByteArray machine_id_data = tracker_data.mid(8, 16);
        // MachineID is a null-terminated ANSI string.
        return QString::fromLocal8Bit(machine_id_data.constData());
    }
    return QString();
}

bool LnkCollector::isValidLnkFile(const QString& file_path)
{
    QFileInfo file_info(file_path);

    // 파일이 존재하고 확장자가 .lnk인지 확인
    if (!file_info.exists() || file_info.suffix().toLower() != "lnk") {
        return false;
    }

    // 최소 크기 확인 (76바이트 헤더)
    if (file_info.size() < 76) {
        return false;
    }

    return true;
}

bool LnkCollector::checkTargetExists(const QString& target_path)
{
    if (target_path.isEmpty()) return false;

    QFileInfo target_info(target_path);
    return target_info.exists();
}

void LnkCollector::validateAndEnrichData(LnkFileInfo& lnk_info)
{
    // 타겟 파일 존재 여부 확인
    lnk_info.target_exists = checkTargetExists(lnk_info.target_path);

    // 파일/디렉토리 링크 여부 판단
    if (!lnk_info.target_path.isEmpty()) {
        QFileInfo target_info(lnk_info.target_path);
        lnk_info.is_file_link = target_info.exists() && target_info.isFile();
        lnk_info.is_directory_link = target_info.exists() && target_info.isDir();
    }

    // 추가 메타데이터 보강
    if (lnk_info.target_path.isEmpty() && !lnk_info.relative_path.isEmpty()) {
        // 상대 경로만 있는 경우
        QDir base_dir(QFileInfo(lnk_info.file_path).absolutePath());
        lnk_info.target_path = base_dir.absoluteFilePath(lnk_info.relative_path);
        lnk_info.target_exists = checkTargetExists(lnk_info.target_path);
    }
}

void LnkCollector::printLnkFiles() const
{
    if (collected_lnk_files_.isEmpty()) {
        qDebug() << "수집된 LNK 파일이 없습니다.";
        return;
    }

    qDebug() << "수집 통계:" << collected_lnk_files_.size() << "개 파일";
    qDebug() << "============================================================";

    for (int i = 0; i < collected_lnk_files_.size(); ++i) {
        const LnkFileInfo& lnk = collected_lnk_files_[i];

        qDebug() << QString("[%1] %2").arg(i + 1).arg(lnk.file_name);
        qDebug() << QString("  타겟: %1").arg(lnk.target_path.isEmpty() ? "N/A" : lnk.target_path);
        qDebug() << QString("  존재: %1").arg(lnk.target_exists ? "예" : "아니오");
        qDebug() << QString("  크기: %1 bytes").arg(lnk.target_file_size);

        if (lnk.target_creation_time.isValid()) {
            qDebug() << QString("  생성: %1").arg(lnk.target_creation_time.toString("yyyy-MM-dd hh:mm:ss"));
        }

        if (!lnk.description.isEmpty()) {
            qDebug() << QString("  설명: %1").arg(lnk.description);
        }

        if (!lnk.working_directory.isEmpty()) {
            qDebug() << QString("  작업디렉토리: %1").arg(lnk.working_directory);
        }

        if (!lnk.command_line_args.isEmpty()) {
            qDebug() << QString("  인수: %1").arg(lnk.command_line_args);
        }

        if (!lnk.drive_serial.isEmpty()) {
            qDebug() << QString("  드라이브시리얼: %1").arg(lnk.drive_serial);
        }

        if (!lnk.machine_id.isEmpty()) {
            qDebug() << QString("  머신 ID: %1").arg(lnk.machine_id);
        }

        qDebug() << "";
    }
}

// =============================================================================
// JSON 변환 함수들 구현 (QByteArray 에러 수정 완료)
// =============================================================================

QJsonObject LnkCollector::toJsonObject() const {
    QJsonObject result;

    /*
    // 메타데이터
    QJsonObject collectionInfo;
    collectionInfo["module_name"] = "LNK_Files";
    collectionInfo["collection_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    collectionInfo["total_processed_files"] = static_cast<qint64>(total_processed_files_);
    collectionInfo["total_successful_parses"] = static_cast<qint64>(total_successful_parses_);
    collectionInfo["collected_files"] = collected_lnk_files_.size();
    collectionInfo["version"] = "1.0";

    if (collection_start_time_.isValid()) {
        collectionInfo["collection_start_time"] = collection_start_time_.toString(Qt::ISODate);
    }
    if (collection_end_time_.isValid()) {
        collectionInfo["collection_end_time"] = collection_end_time_.toString(Qt::ISODate);
        if (collection_start_time_.isValid()) {
            qint64 duration = collection_start_time_.msecsTo(collection_end_time_);
            collectionInfo["collection_duration_ms"] = duration;
        }
    }

    result["collection_info"] = collectionInfo;
    */
    // 검색 디렉토리 정보
    QJsonArray searchDirs;
    for (const QString& dir : search_directories_) {
        searchDirs.append(dir);
    }
    result["search_directories"] = searchDirs;

    // 모든 LNK 파일 데이터
    QJsonArray lnkArray;
    for (int i = 0; i < collected_lnk_files_.size(); i++) {
        const LnkFileInfo& info = collected_lnk_files_[i];

        QJsonObject lnkObj = lnkFileInfoToJson(info);
        lnkObj["file_index"] = i + 1;

        lnkArray.append(lnkObj);
    }

    result["lnk_files"] = lnkArray;

    return result;
}

QJsonObject LnkCollector::lnkFileInfoToJson(const LnkFileInfo& info) const {
    QJsonObject obj;

    // === 기본 파일 정보 ===
    obj["file_path"] = info.file_path;
    obj["file_name"] = info.file_name;
    obj["file_size"] = static_cast<qint64>(info.file_size);

    // LNK 파일 시간 정보
    QJsonObject lnkTimes;
    if (info.creation_time.isValid()) {
        lnkTimes["creation"] = info.creation_time.toString(Qt::ISODate);
        lnkTimes["creation_timestamp"] = info.creation_time.toSecsSinceEpoch();
    }
    if (info.modification_time.isValid()) {
        lnkTimes["modification"] = info.modification_time.toString(Qt::ISODate);
        lnkTimes["modification_timestamp"] = info.modification_time.toSecsSinceEpoch();
    }
    if (info.access_time.isValid()) {
        lnkTimes["access"] = info.access_time.toString(Qt::ISODate);
        lnkTimes["access_timestamp"] = info.access_time.toSecsSinceEpoch();
    }
    obj["lnk_file_times"] = lnkTimes;

    // === LNK 헤더 정보 ===
    obj["header"] = lnkHeaderToJson(info.header);

    // === 링크 정보 ===
    if (info.has_link_info) {
        obj["link_info"] = linkInfoToJson(info.link_info);
    }

    // === 타겟 정보 ===
    QJsonObject targetInfo;
    targetInfo["target_path"] = info.target_path;
    targetInfo["target_exists"] = info.target_exists;
    targetInfo["target_file_size"] = static_cast<qint64>(info.target_file_size);
    targetInfo["target_file_attributes"] = static_cast<qint64>(info.target_file_attributes);
    targetInfo["target_file_attributes_formatted"] = formatFileAttributes(info.target_file_attributes);

    // 타겟 파일 시간 정보
    QJsonObject targetTimes;
    if (info.target_creation_time.isValid()) {
        targetTimes["creation"] = info.target_creation_time.toString(Qt::ISODate);
        targetTimes["creation_timestamp"] = info.target_creation_time.toSecsSinceEpoch();
    }
    if (info.target_access_time.isValid()) {
        targetTimes["access"] = info.target_access_time.toString(Qt::ISODate);
        targetTimes["access_timestamp"] = info.target_access_time.toSecsSinceEpoch();
    }
    if (info.target_write_time.isValid()) {
        targetTimes["write"] = info.target_write_time.toString(Qt::ISODate);
        targetTimes["write_timestamp"] = info.target_write_time.toSecsSinceEpoch();
    }
    targetInfo["target_times"] = targetTimes;

    obj["target_info"] = targetInfo;

    // === 링크 유형 정보 ===
    QJsonObject linkType;
    linkType["is_file_link"] = info.is_file_link;
    linkType["is_directory_link"] = info.is_directory_link;
    obj["link_type"] = linkType;

    // === 문자열 데이터 ===
    QJsonObject stringData;
    if (!info.description.isEmpty()) {
        stringData["description"] = info.description;
    }
    if (!info.working_directory.isEmpty()) {
        stringData["working_directory"] = info.working_directory;
    }
    if (!info.command_line_args.isEmpty()) {
        stringData["command_line_args"] = info.command_line_args;
    }
    if (!info.icon_location.isEmpty()) {
        stringData["icon_location"] = info.icon_location;
    }
    if (!info.relative_path.isEmpty()) {
        stringData["relative_path"] = info.relative_path;
    }
    obj["string_data"] = stringData;

    // === 플래그 정보 ===
    QJsonObject flags;
    flags["has_target_id_list"] = info.has_target_id_list;
    flags["has_link_info"] = info.has_link_info;
    flags["has_name"] = info.has_name;
    flags["has_relative_path"] = info.has_relative_path;
    flags["has_working_dir"] = info.has_working_dir;
    flags["has_arguments"] = info.has_arguments;
    flags["has_icon_location"] = info.has_icon_location;
    obj["flags"] = flags;

    // === 포렌식 정보 ===
    QJsonObject forensics;
    if (!info.drive_serial.isEmpty()) {
        forensics["drive_serial"] = info.drive_serial;
    }
    if (!info.volume_label.isEmpty()) {
        forensics["volume_label"] = info.volume_label;
    }
    if (!info.machine_id.isEmpty()) {
        forensics["machine_id"] = info.machine_id;
    }
    obj["forensics"] = forensics;

    return obj;
}

QJsonObject LnkCollector::lnkHeaderToJson(const LnkHeader& header) const {
    QJsonObject obj;

    obj["header_size"] = static_cast<qint64>(header.header_size);
    obj["header_size_hex"] = QString("0x%1").arg(header.header_size, 8, 16, QChar('0'));

    // Link CLSID를 hex 문자열로 변환 (핵심 수정 - QByteArray에서 QString으로 변환)
    QByteArray clsidBytes(reinterpret_cast<const char*>(header.link_clsid), 16);
    obj["link_clsid"] = QString::fromLatin1(clsidBytes.toHex().toUpper());

    obj["link_flags"] = static_cast<qint64>(header.link_flags);
    obj["link_flags_hex"] = QString("0x%1").arg(header.link_flags, 8, 16, QChar('0'));
    obj["link_flags_formatted"] = formatLinkFlags(header.link_flags);

    obj["file_attributes"] = static_cast<qint64>(header.file_attributes);
    obj["file_attributes_hex"] = QString("0x%1").arg(header.file_attributes, 8, 16, QChar('0'));
    obj["file_attributes_formatted"] = formatFileAttributes(header.file_attributes);

    // 시간 정보 (raw + formatted)
    obj["creation_time_raw"] = QString::number(header.creation_time, 16);
    obj["access_time_raw"] = QString::number(header.access_time, 16);
    obj["write_time_raw"] = QString::number(header.write_time, 16);

    QDateTime creationTime = filetimeToDateTime(header.creation_time);
    QDateTime accessTime = filetimeToDateTime(header.access_time);
    QDateTime writeTime = filetimeToDateTime(header.write_time);

    if (creationTime.isValid()) {
        obj["creation_time"] = creationTime.toString(Qt::ISODate);
        obj["creation_time_timestamp"] = creationTime.toSecsSinceEpoch();
    }
    if (accessTime.isValid()) {
        obj["access_time"] = accessTime.toString(Qt::ISODate);
        obj["access_time_timestamp"] = accessTime.toSecsSinceEpoch();
    }
    if (writeTime.isValid()) {
        obj["write_time"] = writeTime.toString(Qt::ISODate);
        obj["write_time_timestamp"] = writeTime.toSecsSinceEpoch();
    }

    obj["file_size"] = static_cast<qint64>(header.file_size);
    obj["icon_index"] = static_cast<qint64>(header.icon_index);
    obj["show_command"] = static_cast<qint64>(header.show_command);
    obj["show_command_formatted"] = formatShowCommand(header.show_command);
    obj["hot_key"] = static_cast<qint64>(header.hot_key);

    return obj;
}

QJsonObject LnkCollector::linkInfoToJson(const LinkInfo& linkInfo) const {
    QJsonObject obj;

    obj["link_info_size"] = static_cast<qint64>(linkInfo.link_info_size);
    obj["link_info_header_size"] = static_cast<qint64>(linkInfo.link_info_header_size);
    obj["link_info_flags"] = static_cast<qint64>(linkInfo.link_info_flags);
    obj["link_info_flags_hex"] = QString("0x%1").arg(linkInfo.link_info_flags, 8, 16, QChar('0'));

    // 오프셋 정보
    QJsonObject offsets;
    offsets["volume_id_offset"] = static_cast<qint64>(linkInfo.volume_id_offset);
    offsets["local_base_path_offset"] = static_cast<qint64>(linkInfo.local_base_path_offset);
    offsets["common_network_relative_link_offset"] = static_cast<qint64>(linkInfo.common_network_relative_link_offset);
    offsets["common_path_suffix_offset"] = static_cast<qint64>(linkInfo.common_path_suffix_offset);
    obj["offsets"] = offsets;

    // 볼륨 정보
    if (linkInfo.volume_id_offset > 0) {
        obj["volume_info"] = volumeInfoToJson(linkInfo.volume_info);
    }

    // 경로 정보
    QJsonObject paths;
    if (!linkInfo.local_base_path.isEmpty()) {
        paths["local_base_path"] = linkInfo.local_base_path;
    }
    if (!linkInfo.common_path_suffix.isEmpty()) {
        paths["common_path_suffix"] = linkInfo.common_path_suffix;
    }
    obj["paths"] = paths;

    return obj;
}

QJsonObject LnkCollector::volumeInfoToJson(const VolumeInfo& volumeInfo) const {
    QJsonObject obj;

    obj["volume_id_size"] = static_cast<qint64>(volumeInfo.volume_id_size);
    obj["drive_type"] = static_cast<qint64>(volumeInfo.drive_type);
    obj["drive_type_formatted"] = formatDriveType(volumeInfo.drive_type);
    obj["drive_serial_number"] = static_cast<qint64>(volumeInfo.drive_serial_number);
    obj["drive_serial_hex"] = QString("0x%1").arg(volumeInfo.drive_serial_number, 8, 16, QChar('0'));
    obj["volume_label_offset"] = static_cast<qint64>(volumeInfo.volume_label_offset);

    if (!volumeInfo.volume_label.isEmpty()) {
        obj["volume_label"] = volumeInfo.volume_label;
    }

    return obj;
}
