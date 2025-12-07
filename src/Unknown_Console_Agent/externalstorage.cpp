#include "externalstorage.h"

externalstorage::externalstorage(QObject *parent)
    : QObject{parent}
{
}

// =============================================================================
// 메인 수집 함수
// =============================================================================

bool externalstorage::collectUSBForensicsData()
{
    m_usbstorInfoList.clear();
    m_usbInfoList.clear();
    m_deviceClassesList.clear();
    m_portableDevicesList.clear();
    m_setupAPIInfoList.clear();
    m_eventViewerInfoList.clear();

    extractUSBSTORInfo();
    extractUSBInfo();
    extractDeviceClassesInfo();
    extractPortableDevicesInfo();
    extractSetupAPIInfo();
    extractEventViewerInfo();

    return combineAndMatchData();
}

const QVector<USBForensicsData>& externalstorage::getUSBForensicsDevices() const
{
    return m_usbForensicsDevices;
}

// =============================================================================
// 1. USBSTOR 정보 추출
// =============================================================================

bool externalstorage::extractUSBSTORInfo()
{
    HKEY hUSBSTORKey = nullptr;
    if (!openRegistryKey(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\USBSTOR", hUSBSTORKey)) {
        return false;
    }

    QVector<QString> deviceClasses;
    enumerateSubKeys(hUSBSTORKey, deviceClasses);

    for (const QString& deviceClass : deviceClasses) {
        QString deviceClassPath = "SYSTEM\\CurrentControlSet\\Enum\\USBSTOR\\" + deviceClass;
        HKEY hDeviceClassKey = nullptr;

        if (openRegistryKey(HKEY_LOCAL_MACHINE, deviceClassPath, hDeviceClassKey)) {
            QVector<QString> instances;
            enumerateSubKeys(hDeviceClassKey, instances);

            for (const QString& instance : instances) {
                USBSTORInfo info;
                QString instancePath = deviceClassPath + "\\" + instance;
                HKEY hInstanceKey = nullptr;

                if (openRegistryKey(HKEY_LOCAL_MACHINE, instancePath, hInstanceKey)) {
                    readRegistryString(hInstanceKey, "", info.path);
                    readRegistryDWORD(hInstanceKey, "Address", info.address);
                    readRegistryDWORD(hInstanceKey, "Capabilities", info.capabilities);
                    readRegistryString(hInstanceKey, "ClassGUID", info.classGUID);
                    readRegistryMultiString(hInstanceKey, "CompatibleIDs", info.compatibleIDs);
                    readRegistryDWORD(hInstanceKey, "ConfigFlags", info.configFlags);
                    readRegistryString(hInstanceKey, "ContainerID", info.containerID);
                    readRegistryString(hInstanceKey, "DeviceDesc", info.deviceDesc);
                    readRegistryString(hInstanceKey, "Driver", info.driver);
                    readRegistryString(hInstanceKey, "FriendlyName", info.friendlyName);
                    readRegistryMultiString(hInstanceKey, "HardwareID", info.hardwareID);
                    readRegistryString(hInstanceKey, "Mfg", info.mfg);
                    readRegistryString(hInstanceKey, "Service", info.service);

                    RegCloseKey(hInstanceKey);
                    m_usbstorInfoList.append(info);
                }
            }
            RegCloseKey(hDeviceClassKey);
        }
    }

    RegCloseKey(hUSBSTORKey);
    return true;
}

// =============================================================================
// 2. USB 정보 추출 (저장장치만)
// =============================================================================

bool externalstorage::extractUSBInfo()
{
    HKEY hUSBKey = nullptr;
    if (!openRegistryKey(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\USB", hUSBKey)) {
        return false;
    }

    QVector<QString> deviceClasses;
    enumerateSubKeys(hUSBKey, deviceClasses);

    for (const QString& deviceClass : deviceClasses) {
        if (!isExternalStorageDevice(deviceClass)) {
            continue;
        }

        QString deviceClassPath = "SYSTEM\\CurrentControlSet\\Enum\\USB\\" + deviceClass;
        HKEY hDeviceClassKey = nullptr;

        if (openRegistryKey(HKEY_LOCAL_MACHINE, deviceClassPath, hDeviceClassKey)) {
            QVector<QString> instances;
            enumerateSubKeys(hDeviceClassKey, instances);

            for (const QString& instance : instances) {
                USBInfo info;
                QString instancePath = deviceClassPath + "\\" + instance;
                HKEY hInstanceKey = nullptr;

                if (openRegistryKey(HKEY_LOCAL_MACHINE, instancePath, hInstanceKey)) {
                    readRegistryString(hInstanceKey, "", info.path);
                    readRegistryDWORD(hInstanceKey, "Address", info.address);
                    readRegistryDWORD(hInstanceKey, "Capabilities", info.capabilities);
                    readRegistryString(hInstanceKey, "ClassGUID", info.classGUID);
                    readRegistryMultiString(hInstanceKey, "CompatibleIDs", info.compatibleIDs);
                    readRegistryDWORD(hInstanceKey, "ConfigFlags", info.configFlags);
                    readRegistryString(hInstanceKey, "ContainerID", info.containerID);
                    readRegistryString(hInstanceKey, "DeviceDesc", info.deviceDesc);
                    readRegistryString(hInstanceKey, "Driver", info.driver);
                    readRegistryMultiString(hInstanceKey, "HardwareID", info.hardwareID);
                    readRegistryString(hInstanceKey, "Mfg", info.mfg);
                    readRegistryString(hInstanceKey, "ParentIdPrefix", info.parentIdPrefix);
                    readRegistryString(hInstanceKey, "Service", info.service);

                    RegCloseKey(hInstanceKey);
                    m_usbInfoList.append(info);
                }
            }
            RegCloseKey(hDeviceClassKey);
        }
    }

    RegCloseKey(hUSBKey);
    return true;
}

// =============================================================================
// 3. DeviceClasses 정보 추출 (시간 정보 포함)
// =============================================================================

bool externalstorage::extractDeviceClassesInfo()
{
    HKEY hDeviceClassesKey = nullptr;
    if (!openRegistryKey(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\DeviceClasses", hDeviceClassesKey)) {
        return false;
    }

    QVector<QString> allGUIDs;
    enumerateSubKeys(hDeviceClassesKey, allGUIDs);
    RegCloseKey(hDeviceClassesKey);

    for (const QString& guid : allGUIDs) {
        // 🔥 저장장치 관련 GUID만 처리
        if (!isStorageDeviceFromDeviceClasses(guid)) {
            continue;
        }

        QString guidPath = "SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\" + guid;
        HKEY hGUIDKey = nullptr;

        if (openRegistryKey(HKEY_LOCAL_MACHINE, guidPath, hGUIDKey)) {
            QVector<QString> deviceEntries;
            enumerateSubKeys(hGUIDKey, deviceEntries);

            for (const QString& entry : deviceEntries) {
                // 🔥 USB 저장장치만 필터링
                if (entry.contains("USB", Qt::CaseInsensitive) &&
                    isStorageDeviceFromDeviceClasses(entry)) {

                    DeviceClassesInfo info;
                    QString entryPath = guidPath + "\\" + entry;
                    HKEY hEntryKey = nullptr;

                    if (openRegistryKey(HKEY_LOCAL_MACHINE, entryPath, hEntryKey)) {
                        readRegistryString(hEntryKey, "", info.path);
                        readRegistryString(hEntryKey, "DeviceInstance", info.deviceinstance);
                        info.lastWriteTime = getKeyLastWriteTime(hEntryKey);

                        RegCloseKey(hEntryKey);

                        if (info.deviceinstance != "N/A") {
                            m_deviceClassesList.append(info);
                        }
                    }
                }
            }

            RegCloseKey(hGUIDKey);
        }
    }

    qDebug() << "DeviceClasses 파싱 완료. 저장장치" << m_deviceClassesList.size() << "개 발견";
    return true;
}

/**
 * @brief 제외할 장치들 필터링 (마우스, 키보드 등)
 */
bool externalstorage::isExcludedDevice(const QString& deviceInfo)
{
    QStringList excludeKeywords = {
        // 입력 장치
        "Mouse", "Keyboard", "HID", "Gaming",
        "Optical", "Wireless", "Touchpad",

        // 오디오/비디오 장치
        "Audio", "Microphone", "Speaker", "Headset", "Webcam", "Camera",

        // 네트워크 장치
        "Ethernet", "WiFi", "Bluetooth", "Network",

        // 기타 장치
        "Printer", "Scanner", "Fax", "Phone", "Tablet",
        "Composite Device", "Root Hub", "Hub",

        // 특정 클래스 GUID (HID 등)
        "{745a17a0-74d3-11d0-b6fe-00a0c90f57da}",  // HID
        "{4d36e96f-e325-11ce-bfc1-08002be10318}",  // Mouse
        "{4d36e96b-e325-11ce-bfc1-08002be10318}",  // Keyboard

        // 특정 VID (마우스/키보드 제조사)
        "VID_046D",  // Logitech (대부분 마우스/키보드)
        "VID_1532",  // Razer (게이밍 장비)
        "VID_0079"   // DragonRise (게임패드)
    };

    for (const QString& exclude : excludeKeywords) {
        if (deviceInfo.contains(exclude, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief SetupAPI 로그에서 저장장치 여부 판별
 */
bool externalstorage::isStorageDeviceFromSetupAPI(const QString& logSection)
{
    // 1. 제외할 장치 먼저 체크
    if (isExcludedDevice(logSection)) {
        return false;
    }

    // 2. 저장장치 관련 키워드 확인
    QStringList storageIndicators = {
        "USBSTOR", "Mass Storage", "USB_DISK", "Disk&Ven_",
        "Removable Disk", "Generic MassStorageClass",
        "Storage Volume", "Volume", "Drive Letter",
        "WPD", "WPDBUSENUM"  // Windows Portable Device
    };

    for (const QString& indicator : storageIndicators) {
        if (logSection.contains(indicator, Qt::CaseInsensitive)) {
            return true;
        }
    }

    // 3. VID 기반 확인
    return isExternalStorageDevice(logSection);
}


/**
 * @brief DeviceClasses에서 저장장치 여부 판별
 */
bool externalstorage::isStorageDeviceFromDeviceClasses(const QString& devicePath)
{
    // 1. 제외할 장치 먼저 체크
    if (isExcludedDevice(devicePath)) {
        return false;
    }

    // 2. 저장장치 관련 GUID 확인
    QStringList storageGUIDs = {
        "{53f56307-b6bf-11d0-94f2-00a0c91efb8b}",  // GUID_DEVINTERFACE_DISK
        "{53f5630a-b6bf-11d0-94f2-00a0c91efb8b}",  // GUID_DEVINTERFACE_CDROM
        "{53f5630b-b6bf-11d0-94f2-00a0c91efb8b}",  // GUID_DEVINTERFACE_PARTITION
        "{53f5630c-b6bf-11d0-94f2-00a0c91efb8b}",  // GUID_DEVINTERFACE_TAPE
        "{53f5630d-b6bf-11d0-94f2-00a0c91efb8b}",  // GUID_DEVINTERFACE_WRITEONCEDISK
        "{6ac27878-a6fa-4155-ba85-f98f491d4f33}",  // WPD
        "{f33fdc04-d1ac-4e8e-9a30-19bbd4b108ae}"   // Mass Storage
    };

    for (const QString& guid : storageGUIDs) {
        if (devicePath.contains(guid, Qt::CaseInsensitive)) {
            return true;
        }
    }

    // 3. 경로에서 USBSTOR/Storage 확인
    if (devicePath.contains("USBSTOR", Qt::CaseInsensitive) ||
        devicePath.contains("Storage\\Volume", Qt::CaseInsensitive)) {
        return true;
    }

    return false;
}

// =============================================================================
// 4. Portable Devices 정보 추출
// =============================================================================

bool externalstorage::extractPortableDevicesInfo()
{
    HKEY hPortableKey = nullptr;
    if (!openRegistryKey(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows Portable Devices\\Devices", hPortableKey)) {
        return false;
    }

    QVector<QString> deviceKeys;
    enumerateSubKeys(hPortableKey, deviceKeys);

    for (const QString& deviceKey : deviceKeys) {
        if (deviceKey.contains("USB", Qt::CaseInsensitive)) {
            PortableDeviceInfo info;
            QString devicePath = "SOFTWARE\\Microsoft\\Windows Portable Devices\\Devices\\" + deviceKey;
            HKEY hDeviceKey = nullptr;

            if (openRegistryKey(HKEY_LOCAL_MACHINE, devicePath, hDeviceKey)) {
                readRegistryString(hDeviceKey, "", info.path);
                readRegistryString(hDeviceKey, "FriendlyName", info.friendlyName);

                RegCloseKey(hDeviceKey);
                m_portableDevicesList.append(info);
            }
        }
    }

    RegCloseKey(hPortableKey);
    return true;
}

// =============================================================================
// 5. SetupAPI 정보 추출 (9가지 핵심 정보)
// =============================================================================

bool externalstorage::extractSetupAPIInfo()
{
    QString logPath = findSetupAPILogPath();
    if (logPath.isEmpty()) {
        // debugging 용 qDebug() << "SetupAPI 로그 파일을 찾을 수 없습니다.";
        return false;
    }

    //qDebug() << "SetupAPI 로그 파일 위치:" << logPath;
    return parseSetupAPILogFile(logPath);
}

QString externalstorage::findSetupAPILogPath()
{
    QStringList possiblePaths = {
        "C:\\Windows\\inf\\setupapi.dev.log",
        "C:\\Windows\\INF\\setupapi.dev.log",
        "C:\\Windows\\Inf\\setupapi.dev.log"
    };

    for (const QString& path : possiblePaths) {
        QFile file(path);
        if (file.exists() && file.size() > 0) {
            return path;
        }
    }
    return QString();
}

bool externalstorage::parseSetupAPILogFile(const QString& logFilePath)
{
    QFile logFile(logFilePath);
    if (!logFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream in(&logFile);
    QString currentSection;
    bool inUSBSection = false;
    QString currentSectionHeader;
    QString globalBootInfo;

    while (!in.atEnd()) {
        QString line = in.readLine();

        if (line.contains("[Boot Session:", Qt::CaseInsensitive)) {
            globalBootInfo = line;
        }

        if (line.startsWith(">>>")) {
            // 이전 섹션 처리
            if (inUSBSection && !currentSection.isEmpty()) {
                // 🔥 저장장치 필터링 적용
                if (isStorageDeviceFromSetupAPI(currentSection)) {
                    SetupAPIInfo info;
                    QString fullSection = globalBootInfo + "\n" + currentSection;
                    extractSetupAPIDeviceInfo(fullSection, info);

                    if (info.serialNumber != "N/A" || info.firstConnectionTime != "N/A") {
                        m_setupAPIInfoList.append(info);
                    }
                }
            }

            currentSectionHeader = line;
            currentSection = line + "\n";

            // 🔥 USB 관련 섹션 판단 (저장장치 중심으로)
            inUSBSection = (line.contains("USB", Qt::CaseInsensitive) ||
                            line.contains("USBSTOR", Qt::CaseInsensitive) ||
                            line.contains("Mass Storage", Qt::CaseInsensitive) ||
                            line.contains("WPDBUSENUM", Qt::CaseInsensitive) ||
                            line.contains("Disk&Ven_", Qt::CaseInsensitive));
        }
        else if (line.contains("Section start", Qt::CaseInsensitive)) {
            currentSection += line + "\n";
        }
        else if (inUSBSection ||
                 line.contains("USB\\VID_", Qt::CaseInsensitive) ||
                 line.contains("USBSTOR\\", Qt::CaseInsensitive) ||
                 line.contains("WPDBUSENUM", Qt::CaseInsensitive)) {
            currentSection += line + "\n";

            if (line.contains("USB\\VID_", Qt::CaseInsensitive) ||
                line.contains("USBSTOR\\", Qt::CaseInsensitive) ||
                line.contains("WPDBUSENUM", Qt::CaseInsensitive)) {
                inUSBSection = true;
            }
        }
    }

    // 마지막 섹션 처리
    if (inUSBSection && !currentSection.isEmpty()) {
        // 🔥 저장장치 필터링 적용
        if (isStorageDeviceFromSetupAPI(currentSection)) {
            SetupAPIInfo info;
            QString fullSection = globalBootInfo + "\n" + currentSection;
            extractSetupAPIDeviceInfo(fullSection, info);

            if (info.serialNumber != "N/A" || info.firstConnectionTime != "N/A") {
                m_setupAPIInfoList.append(info);
            }
        }
    }

    logFile.close();
    //qDebug() << "SetupAPI 파싱 완료. 저장장치" << m_setupAPIInfoList.size() << "개 발견";
    return true;
}

void externalstorage::extractSetupAPIDeviceInfo(const QString& logSection, SetupAPIInfo& info)
{
    info.volumeName = extractVolumeName(logSection);
    info.serialNumber = extractSerialNumber(logSection);
    info.volumeGUID = extractVolumeGUID(logSection);
    info.productID = extractProductID(logSection);
    info.firstConnectionTime = extractFirstConnectionTime(logSection);
    info.firstConnectionAfterBoot = extractFirstConnectionAfterBoot(logSection);
    info.lastConnectionTime = extractLastConnectionTime(logSection);
    info.lastDisconnectionTime = extractLastDisconnectionTime(logSection);
    info.userAccount = extractUserAccount(logSection);
}

QString externalstorage::extractVolumeName(const QString& logText)
{
    // SetupAPI 로그에서 실제 볼륨명/장치명 추출 (우선순위별)
    QStringList volumePatterns = {
        // 1. Configure Driver 라인: {Configure Driver: USB Mass Storage Device}
        R"(\{Configure Driver:\s*([^}]+)\})",
        // 2. Display Name: Display Name = Samsung Type-C
        R"(Display Name\s*=\s*([^\r\n;]+?)(?:\s*$|\s*;))",
        // 3. Device Description
        R"(Device Description\s*=\s*([^\r\n;]+?)(?:\s*$|\s*;))",
        // 4. Service Display Name
        R"(Service Display Name\s*=\s*([^\r\n;]+?)(?:\s*$|\s*;))",
        // 5. 벤더&제품 정보에서: Ven_Samsung&Prod_Type-C
        R"(Ven_([^&\s]+)&Prod_([^&\s\\#]+))",
        // 6. USBSTOR 라인에서 제품명: USBSTOR\Disk&Ven_SMI&Prod_USB_DISK
        R"(USBSTOR\\Disk&Ven_([^&\s]+)&Prod_([^&\s\\#]+))",
        // 7. WPD 장치명
        R"(WPDBUSENUM.*?Disk&Ven_([^&\s]+)&Prod_([^&\s\\#]+))",
        // 8. 간단한 장치명
        R"(Device:\s*([^\r\n]+))"
    };

    for (int i = 0; i < volumePatterns.size(); i++) {
        const QString& pattern = volumePatterns[i];
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = regex.match(logText);

        if (match.hasMatch()) {
            QString volume;

            // Ven_&Prod_ 패턴들 (5, 6, 7번)
            if (i >= 4 && i <= 6 && match.capturedTexts().size() >= 3) {
                QString vendor = match.captured(1).trimmed();
                QString product = match.captured(2).trimmed();

                // 벤더명 정리
                if (vendor == "Generic" || vendor == "SMI") {
                    volume = product.replace("_", " ");
                } else {
                    volume = vendor + " " + product.replace("_", " ");
                }
            } else {
                volume = match.captured(1).trimmed();
            }

            // 유효성 검사
            if (!volume.startsWith("@") &&
                volume.length() > 2 &&
                volume.length() < 100 &&
                !volume.contains("exit", Qt::CaseInsensitive) &&
                !volume.contains("0x") &&
                !volume.contains("ERROR")) {

                // 특수 문자 정리
                volume = volume.replace("_", " ");
                volume = volume.replace("  ", " ").trimmed();
                return volume;
            }
        }
    }

    return "N/A";
}

QString externalstorage::extractSerialNumber(const QString& logText)
{
    // 실제 SetupAPI 로그 형식에 맞춘 시리얼 번호 추출 (우선순위별)
    QStringList serialPatterns = {
        // 1. USB VID&PID 뒤의 시리얼: USB\VID_090C&PID_1000\HG2811162261300003
        R"(USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\([^\\&\s\]\r\n}]+))",
        // 2. USBSTOR에서: USBSTOR\Disk&Ven_Samsung&Prod_Type-C&Rev_1100\0375419060019935&0
        R"(USBSTOR\\Disk&[^\\]+\\([^\\&\s]+)&\d+)",
        // 3. WPD 경로에서: WPDBUSENUM\_??_USBSTOR#Disk&Ven_SMI&Prod_USB_DISK&Rev_1100#HG2811162261300003&0
        R"(WPDBUSENUM[^#]*#[^#]*#([^&\s\\}#]+)&\d+)",
        // 4. Device Install 헤더에서
        R"(Device Install.*?USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\([^\\&\s\]\r\n}]+))",
        // 5. Starting device에서
        R"(Starting device.*?USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\([^\\&\s\]\r\n}]+))",
        // 6. Configure Device에서
        R"(Configure Device.*?USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\([^\\&\s\]\r\n}]+))"
    };

    for (const QString& pattern : serialPatterns) {
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = regex.match(logText);

        if (match.hasMatch()) {
            QString serial = match.captured(1).trimmed();

            // 특수문자 제거 및 정리
            serial = serial.remove(QRegularExpression("[\\]\\}\\{\\)\\(#]"));

            // 유효한 시리얼 번호 검증
            if (serial.length() >= 8 &&
                serial.length() <= 50 &&
                !serial.isEmpty() &&
                serial != "0" &&
                serial != "00000000" &&
                !serial.startsWith("??") &&
                !serial.contains("ERROR")) {
                return serial;
            }
        }
    }

    return "N/A";
}

QString externalstorage::extractVolumeGUID(const QString& logText)
{
    QRegularExpression guidPattern(R"(\{[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\})");
    QRegularExpressionMatch match = guidPattern.match(logText);
    return match.hasMatch() ? match.captured(0) : "N/A";
}

QString externalstorage::extractProductID(const QString& logText)
{
    // VID_090C&PID_1000에서 PID 추출
    QRegularExpression pidPattern(R"(VID_[0-9A-F]{4}&PID_([0-9A-F]{4}))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = pidPattern.match(logText);
    return match.hasMatch() ? match.captured(1) : "N/A";
}

QString externalstorage::extractFirstConnectionTime(const QString& logText)
{
    // 실제 SetupAPI 로그의 시간 형식들을 모두 처리 (우선순위별)
    QStringList timePatterns = {
        // 1. Section start: Section start 2025/07/23 02:33:08.613
        R"(Section start\s+(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\s)",
        // 2. Device Install 시작: >>> [Device Install ... >>> Section start
        R"(>>>\s*\[Device Install.*?>>>\s*Section start\s+(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\s)",
        // 3. 첫 번째 타임스탬프가 있는 라인
        R"((\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\s)",
        // 4. 로그 전체에서 첫 번째 발견되는 시간
        R"((\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?))"
    };

    for (const QString& pattern : timePatterns) {
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = regex.match(logText);

        if (match.hasMatch()) {
            QString timeStr = match.captured(1).trimmed();
            // 시간 형식 검증 및 변환
            if (timeStr.length() >= 19) { // 최소 "2025/07/23 02:33:08" 형식
                return timeStr;
            }
        }
    }

    // 추가: Boot Session 정보에서 시간 추출
    QRegularExpression bootPattern(R"(\[Boot Session:\s*(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\])",
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch bootMatch = bootPattern.match(logText);
    if (bootMatch.hasMatch()) {
        return bootMatch.captured(1).trimmed();
    }

    return "N/A";
}

QString externalstorage::extractFirstConnectionAfterBoot(const QString& logText)
{
    // Boot Session 이후 첫 번째 Section start 시간 찾기
    QRegularExpression bootPattern(R"(\[Boot Session:\s*(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\])",
                                   QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch bootMatch = bootPattern.match(logText);

    if (bootMatch.hasMatch()) {
        QString bootTime = bootMatch.captured(1);

        // Boot Session 이후의 첫 번째 Section start 찾기
        int bootPos = bootMatch.capturedEnd();
        QString afterBootText = logText.mid(bootPos);

        QRegularExpression sectionPattern(R"(Section start\s+(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\s)",
                                          QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch sectionMatch = sectionPattern.match(afterBootText);

        if (sectionMatch.hasMatch()) {
            return sectionMatch.captured(1).trimmed();
        }
    }

    // Boot Session 정보가 없으면 일반 첫 연결 시간 반환
    return extractFirstConnectionTime(logText);
}

QString externalstorage::extractLastConnectionTime(const QString& logText)
{
    // 로그 텍스트에서 마지막 Section start 시간 찾기
    QRegularExpression timePattern(R"(Section start\s+(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?)\s)",
                                   QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator iterator = timePattern.globalMatch(logText);
    QString lastTime = "N/A";

    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        lastTime = match.captured(1).trimmed();
    }

    // 마지막 시간이 없으면 첫 연결 시간으로 대체
    if (lastTime == "N/A") {
        lastTime = extractFirstConnectionTime(logText);
    }

    return lastTime;
}

QString externalstorage::extractLastDisconnectionTime(const QString& logText)
{
    QString lastDisconnectTime = "N/A";

    //qDebug() << "Section end 추출 시작...";

    // 가장 간단한 패턴: Section end + 날짜/시간
    QRegularExpression pattern(R"(Section\s+end\s+(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?))");

    QRegularExpressionMatchIterator iterator = pattern.globalMatch(logText);
    int count = 0;

    while (iterator.hasNext()) {
        QRegularExpressionMatch match = iterator.next();
        QString foundTime = match.captured(1).trimmed();
        count++;
        //qDebug() << "Section end 발견" << count << ":" << foundTime;
        lastDisconnectTime = foundTime; // 마지막 것을 유지
    }

    //qDebug() << "총" << count << "개 Section end 발견";

    // 아무것도 못 찾았으면 더 단순한 패턴 시도
    if (lastDisconnectTime == "N/A") {
        //qDebug() << "기본 패턴 실패. 단순 패턴 시도...";

        // 그냥 "end" + 시간 패턴
        QRegularExpression simplePattern(R"(end\s+(\d{4}/\d{2}/\d{2}\s+\d{2}:\d{2}:\d{2}(?:\.\d{3})?))");
        QRegularExpressionMatchIterator simpleIterator = simplePattern.globalMatch(logText);

        int simpleCount = 0;
        while (simpleIterator.hasNext()) {
            QRegularExpressionMatch match = simpleIterator.next();
            QString foundTime = match.captured(1).trimmed();
            simpleCount++;
           // qDebug() << "단순 end 발견" << simpleCount << ":" << foundTime;
            lastDisconnectTime = foundTime;
        }

        //qDebug() << "단순 패턴으로" << simpleCount << "개 발견";
    }
    return lastDisconnectTime;
}

QString externalstorage::extractUserAccount(const QString& logText)
{
    // 사용자 계정 정보 추출 (우선순위별)
    QStringList userPatterns = {
        // 1. Client process ID와 User
        R"(Client.*?process.*?user.*?([A-Za-z0-9\\-_]+))",
        // 2. User context
        R"(User.*?context.*?([A-Za-z0-9\\-_]+))",
        // 3. 도메인\사용자 형식
        R"(([A-Za-z0-9-_]+\\[A-Za-z0-9-_]+))",
        // 4. 단순 사용자명
        R"(User:\s*([A-Za-z0-9-_]+))",
        // 5. Process user
        R"(Process.*?user[:\s]*([A-Za-z0-9\\-_]+))"
    };

    for (const QString& pattern : userPatterns) {
        QRegularExpression regex(pattern, QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatch match = regex.match(logText);

        if (match.hasMatch()) {
            QString user = match.captured(1).trimmed();
            // 시스템 계정이 아닌 실제 사용자만 반환
            if (!user.contains("SYSTEM", Qt::CaseInsensitive) &&
                !user.contains("SERVICE", Qt::CaseInsensitive) &&
                !user.contains("LOCAL", Qt::CaseInsensitive) &&
                user.length() > 2) {
                return user;
            }
        }
    }

    // 시간대로 사용자 추정
    QRegularExpression timePattern(R"(\d{4}/\d{2}/\d{2}\s+(\d{2}):\d{2}:\d{2})");
    QRegularExpressionMatch timeMatch = timePattern.match(logText);

    if (timeMatch.hasMatch()) {
        int hour = timeMatch.captured(1).toInt();
        // 업무 시간대 (9-18시)는 사용자, 그 외는 SYSTEM
        if (hour >= 9 && hour <= 18) {
            return "User (추정)";
        } else if ((hour >= 1 && hour <= 6) || hour >= 22) {
            return "SYSTEM (추정)";
        }
    }

    // 장치 설치 특성상 대부분 SYSTEM
    if (logText.contains("Device Install", Qt::CaseInsensitive) ||
        logText.contains("Configure Device", Qt::CaseInsensitive)) {
        return "SYSTEM (추정)";
    }

    return "N/A";
}

// =============================================================================
// 레지스트리 접근 함수들
// =============================================================================

bool externalstorage::openRegistryKey(HKEY rootKey, const QString& subKeyPath, HKEY& resultKey)
{
    std::wstring wSubKeyPath = subKeyPath.toStdWString();
    LONG result = RegOpenKeyExW(rootKey, wSubKeyPath.c_str(), 0, KEY_READ, &resultKey);
    return (result == ERROR_SUCCESS);
}

bool externalstorage::readRegistryDWORD(HKEY hKey, const QString& valueName, dword& result)
{
    std::wstring wValueName = valueName.toStdWString();
    DWORD dataType = 0;
    DWORD dataSize = sizeof(DWORD);

    LONG regResult = RegQueryValueExW(hKey, wValueName.c_str(), nullptr, &dataType,
                                      reinterpret_cast<LPBYTE>(&result), &dataSize);

    if (regResult != ERROR_SUCCESS || dataType != REG_DWORD) {
        result = 0;
        return false;
    }
    return true;
}

bool externalstorage::readRegistryString(HKEY hKey, const QString& valueName, QString& result)
{
    std::wstring wValueName = valueName.toStdWString();
    DWORD dataType = 0;
    DWORD dataSize = 0;

    LONG regResult = RegQueryValueExW(hKey, wValueName.c_str(), nullptr, &dataType, nullptr, &dataSize);

    if (regResult != ERROR_SUCCESS || (dataType != REG_SZ && dataType != REG_EXPAND_SZ)) {
        result = "N/A";
        return false;
    }

    std::vector<wchar_t> buffer(dataSize / sizeof(wchar_t));
    regResult = RegQueryValueExW(hKey, wValueName.c_str(), nullptr, &dataType,
                                 reinterpret_cast<LPBYTE>(buffer.data()), &dataSize);

    if (regResult == ERROR_SUCCESS) {
        result = QString::fromWCharArray(buffer.data());
        return true;
    } else {
        result = "N/A";
        return false;
    }
}

bool externalstorage::readRegistryMultiString(HKEY hKey, const QString& valueName, QString& result)
{
    std::wstring wValueName = valueName.toStdWString();
    DWORD dataType = 0;
    DWORD dataSize = 0;

    LONG regResult = RegQueryValueExW(hKey, wValueName.c_str(), nullptr, &dataType, nullptr, &dataSize);

    if (regResult != ERROR_SUCCESS || dataType != REG_MULTI_SZ) {
        result = "N/A";
        return false;
    }

    std::vector<wchar_t> buffer(dataSize / sizeof(wchar_t));
    regResult = RegQueryValueExW(hKey, wValueName.c_str(), nullptr, &dataType,
                                 reinterpret_cast<LPBYTE>(buffer.data()), &dataSize);

    if (regResult == ERROR_SUCCESS) {
        result = QString::fromWCharArray(buffer.data());
        return true;
    } else {
        result = "N/A";
        return false;
    }
}

bool externalstorage::enumerateSubKeys(HKEY hKey, QVector<QString>& subKeys)
{
    subKeys.clear();
    DWORD subKeyCount = 0;
    DWORD maxSubKeyLength = 0;

    LONG result = RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, &subKeyCount,
                                   &maxSubKeyLength, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);

    if (result != ERROR_SUCCESS) {
        return false;
    }

    for (DWORD i = 0; i < subKeyCount; i++) {
        std::vector<wchar_t> keyName(maxSubKeyLength + 1);
        DWORD keyNameSize = maxSubKeyLength + 1;

        result = RegEnumKeyExW(hKey, i, keyName.data(), &keyNameSize,
                               nullptr, nullptr, nullptr, nullptr);

        if (result == ERROR_SUCCESS) {
            subKeys.append(QString::fromWCharArray(keyName.data()));
        }
    }

    return true;
}

// =============================================================================
// 헬퍼 함수들
// =============================================================================
bool externalstorage::isExternalStorageDevice(const QString& deviceClass)
{
    // 1. 명시적으로 제외할 장치들 먼저 체크
    if (isExcludedDevice(deviceClass)) {
        return false;
    }

    // 2. 저장장치 VID 목록 (확장됨)
    QStringList storageVIDs = {
        // 주요 USB 플래시 드라이브 제조사
        "VID_0781",  // SanDisk
        "VID_1058",  // Western Digital
        "VID_090C",  // Silicon Motion (SM3267 등)
        "VID_0930",  // Toshiba
        "VID_04E8",  // Samsung
        "VID_0BC2",  // Seagate
        "VID_152D",  // JMicron (USB-SATA 브릿지)
        "VID_0951",  // Kingston
        "VID_8564",  // Transcend
        "VID_13FE",  // Kingston (일부 모델)
        "VID_05E3",  // Genesys Logic (USB 허브/저장장치)
        "VID_058F",  // Alcor Micro (카드리더)
        "VID_0424",  // SMSC (일부 저장장치)
        "VID_1F75",  // Innostor (USB 컨트롤러)
        "VID_14CD",  // Super Top (외장 하드)
        "VID_174C",  // ASMedia (USB-SATA)
        "VID_067B",  // Prolific (USB-IDE/SATA)
        "VID_1A2C",  // China Electronics Corp
        "VID_0BDA"   // Realtek (일부 카드리더)
    };

    // 3. VID 매칭 확인
    for (const QString& vid : storageVIDs) {
        if (deviceClass.contains(vid, Qt::CaseInsensitive)) {
            return true;
        }
    }

    // 4. 저장장치 키워드 확인 (확장됨)
    QStringList storageKeywords = {
        "Mass", "Storage", "Disk", "Drive", "USBSTOR",
        "Card Reader", "CardReader", "SD", "TF", "CF",
        "USB_DISK", "Flash", "Thumb", "Stick",
        "External", "Portable", "Removable"
    };

    for (const QString& keyword : storageKeywords) {
        if (deviceClass.contains(keyword, Qt::CaseInsensitive)) {
            return true;
        }
    }

    // 5. USBSTOR 경로 확인
    if (deviceClass.contains("USBSTOR", Qt::CaseInsensitive)) {
        return true;
    }

    return false;
}

/**
 * @brief DeviceClasses 정보 추출 (필터링 적용) - 기존 함수 교체
 */


// =============================================================================
// 출력 함수들
// =============================================================================

void externalstorage::printDeviceSummary() const
{
    qDebug() << "=== USBSTOR 정보 (" << m_usbstorInfoList.size() << "개) ===";
    for (const auto& info : m_usbstorInfoList) {
        qDebug() << "Path:" << info.path;
        qDebug() << "Address:" << info.address;
        qDebug() << "Capabilities:" << info.capabilities;
        qDebug() << "ClassGUID:" << info.classGUID;
        qDebug() << "CompatibleIDs:" << info.compatibleIDs;
        qDebug() << "ConfigFlags:" << info.configFlags;
        qDebug() << "ContainerID:" << info.containerID;
        qDebug() << "DeviceDesc:" << info.deviceDesc;
        qDebug() << "Driver:" << info.driver;
        qDebug() << "FriendlyName:" << info.friendlyName;
        qDebug() << "HardwareID:" << info.hardwareID;
        qDebug() << "Mfg:" << info.mfg;
        qDebug() << "Service:" << info.service;
        qDebug() << "---";
    }

    qDebug() << "\n=== USB 정보 (" << m_usbInfoList.size() << "개) ===";
    for (const auto& info : m_usbInfoList) {
        qDebug() << "Path:" << info.path;
        qDebug() << "Address:" << info.address;
        qDebug() << "Capabilities:" << info.capabilities;
        qDebug() << "ClassGUID:" << info.classGUID;
        qDebug() << "CompatibleIDs:" << info.compatibleIDs;
        qDebug() << "ConfigFlags:" << info.configFlags;
        qDebug() << "ContainerID:" << info.containerID;
        qDebug() << "DeviceDesc:" << info.deviceDesc;
        qDebug() << "Driver:" << info.driver;
        qDebug() << "HardwareID:" << info.hardwareID;
        qDebug() << "Mfg:" << info.mfg;
        qDebug() << "ParentIdPrefix:" << info.parentIdPrefix;
        qDebug() << "Service:" << info.service;
        qDebug() << "---";
    }

    qDebug() << "\n=== DeviceClasses 정보 (" << m_deviceClassesList.size() << "개) ===";
    for (const auto& info : m_deviceClassesList) {
        qDebug() << "Path:" << info.path;
        qDebug() << "DeviceInstance:" << info.deviceinstance;
        qDebug() << "LastWriteTime:" << info.lastWriteTime;
        qDebug() << "---";
    }

    qDebug() << "\n=== Portable Devices 정보 (" << m_portableDevicesList.size() << "개) ===";
    for (const auto& info : m_portableDevicesList) {
        qDebug() << "Path:" << info.path;
        qDebug() << "FriendlyName:" << info.friendlyName;
        qDebug() << "---";
    }

    qDebug() << "\n=== SetupAPI 정보 (" << m_setupAPIInfoList.size() << "개) ===";
    for (const auto& info : m_setupAPIInfoList) {
        qDebug() << "VolumeName:" << info.volumeName;
        qDebug() << "SerialNumber:" << info.serialNumber;
        qDebug() << "VolumeGUID:" << info.volumeGUID;
        qDebug() << "ProductID:" << info.productID;
        qDebug() << "FirstConnectionTime:" << info.firstConnectionTime;
        qDebug() << "FirstConnectionAfterBoot:" << info.firstConnectionAfterBoot;
        qDebug() << "LastConnectionTime:" << info.lastConnectionTime;
        qDebug() << "LastDisconnectionTime:" << info.lastDisconnectionTime;
        qDebug() << "UserAccount:" << info.userAccount;
        qDebug() << "---";
    }

    qDebug() << "\n=== EventViewer 정보 (" << m_eventViewerInfoList.size() << "개) ===";
    for (const auto& info : m_eventViewerInfoList) {
        qDebug() << "EventTime:" << info.eventTime;
        qDebug() << "EventType:" << info.eventType;
        qDebug() << "EventID:" << info.eventID;
        qDebug() << "ProviderName:" << info.providerName;
        qDebug() << "DeviceInstanceID:" << info.deviceInstanceID;
        qDebug() << "EventRecordID:" << info.eventRecordID;
        qDebug() << "UserID:" << info.userID;
        qDebug() << "ServiceName:" << info.serviceName;
        qDebug() << "ChannelName:" << info.channelName;
        qDebug() << "---";
    }
}

void externalstorage::printTimestampAnalysis() const
{
    printDeviceSummary();
}

void externalstorage::printDetailedResults() const
{
    printDeviceSummary();
}

void externalstorage::printStatistics() const
{
    qDebug() << "총 수집된 데이터:";
    qDebug() << "USBSTOR:" << m_usbstorInfoList.size();
    qDebug() << "USB:" << m_usbInfoList.size();
    qDebug() << "DeviceClasses:" << m_deviceClassesList.size();
    qDebug() << "PortableDevices:" << m_portableDevicesList.size();
    qDebug() << "SetupAPI:" << m_setupAPIInfoList.size();
    qDebug() << "EventViewer:" << m_eventViewerInfoList.size();
}

bool externalstorage::extractVolumeInfo()
{
    // 현재는 SetupAPI에서 충분한 볼륨 정보를 얻고 있으므로 기본 동작
    //qDebug() << "볼륨 정보 추출: SetupAPI에서 이미 수집됨";
    return true;
}

bool externalstorage::extractConnectionTimes()
{
    // 현재는 SetupAPI에서 충분한 시간 정보를 얻고 있으므로 기본 동작
    //qDebug() << "연결 시간 정보 추출: SetupAPI에서 이미 수집됨";
    return true;
}

bool externalstorage::combineAndMatchData()
{
    m_usbForensicsDevices.clear();
    //qDebug() << "EventViewer 통합 데이터 매칭 시작...";

    // SetupAPI 정보를 기반으로 USBForensicsData 생성 (기존 로직)
    for (const auto& setupInfo : m_setupAPIInfoList) {
        USBForensicsData device;

        device.setupAPIInfo = setupInfo;
        device.hasSetupAPIData = true;
        device.primarySource = "SetupAPI";
        device.deviceKey = setupInfo.serialNumber;

        QString serial = setupInfo.serialNumber;

        // 기존 USBSTOR 데이터 매칭
        for (const auto& usbstorInfo : m_usbstorInfoList) {
            if (matchDevicesBySerial(serial, extractSerialFromInstanceId(usbstorInfo.friendlyName))) {
                device.usbstorInfo = usbstorInfo;
                device.hasUSBSTORData = true;
                break;
            }
        }

        // 기존 USB 데이터 매칭
        for (const auto& usbInfo : m_usbInfoList) {
            if (matchDevicesBySerial(serial, extractSerialFromInstanceId(usbInfo.deviceDesc))) {
                device.usbInfo = usbInfo;
                device.hasUSBData = true;
                break;
            }
        }

        // 기존 DeviceClasses 데이터 매칭
        for (const auto& deviceClassInfo : m_deviceClassesList) {
            if (deviceClassInfo.deviceinstance.contains(serial, Qt::CaseInsensitive)) {
                device.deviceClassesInfo = deviceClassInfo;
                device.hasDeviceClassesData = true;
                break;
            }
        }

        // 기존 Portable Devices 데이터 매칭
        for (const auto& portableInfo : m_portableDevicesList) {
            if (portableInfo.path.contains(serial, Qt::CaseInsensitive)) {
                device.portableDeviceInfo = portableInfo;
                device.hasPortableDeviceData = true;
                break;
            }
        }

        // 메타데이터 생성
        generateDeviceMetadata(device);

        // 최종 리스트에 추가
        m_usbForensicsDevices.append(device);
    }

    // ⭐ 새로 추가: EventViewer에만 있는 고아 이벤트들 처리
    handleOrphanEvents();

    //qDebug() << "EventViewer 통합 완료:" << m_usbForensicsDevices.size() << "개 장치";
    return true;
}

void externalstorage::generateDeviceMetadata(USBForensicsData& device)
{
    // 장치 식별용 메타데이터 생성
    device.metadata.serialNumber = device.setupAPIInfo.serialNumber;
    device.metadata.vidPid = extractVidPidFromHardwareId(device.setupAPIInfo.productID);
    device.metadata.deviceClassName = device.setupAPIInfo.volumeName;

    // Instance ID 생성 (USB\VID_xxxx&PID_xxxx\SerialNumber 형식)
    if (device.setupAPIInfo.productID != "N/A" && device.setupAPIInfo.serialNumber != "N/A") {
        device.metadata.instanceID = QString("USB\\VID_090C&PID_%1\\%2")
        .arg(device.setupAPIInfo.productID)
            .arg(device.setupAPIInfo.serialNumber);
    }
}

bool externalstorage::matchDevicesBySerial(const QString& serial1, const QString& serial2) const
{
    // 시리얼 번호로 장치 매칭
    if (serial1 == "N/A" || serial2 == "N/A" || serial1.isEmpty() || serial2.isEmpty()) {
        return false;
    }

    // 대소문자 무시하고 비교
    QString s1 = serial1.trimmed().toUpper();
    QString s2 = serial2.trimmed().toUpper();

    // 정확한 일치
    if (s1 == s2) {
        return true;
    }

    // 부분 일치 (한쪽이 다른 쪽을 포함)
    if (s1.length() >= 8 && s2.length() >= 8) {
        if (s1.contains(s2) || s2.contains(s1)) {
            return true;
        }
    }

    return false;
}

bool externalstorage::matchDevicesByContainerID(const QString& id1, const QString& id2)
{
    // Container ID로 장치 매칭
    if (id1 == "N/A" || id2 == "N/A" || id1.isEmpty() || id2.isEmpty()) {
        return false;
    }

    return id1.compare(id2, Qt::CaseInsensitive) == 0;
}

bool externalstorage::matchDevicesByVidPid(const QString& vidpid1, const QString& vidpid2) const
{
    // VID&PID로 장치 매칭
    if (vidpid1 == "N/A" || vidpid2 == "N/A" || vidpid1.isEmpty() || vidpid2.isEmpty()) {
        return false;
    }

    return vidpid1.compare(vidpid2, Qt::CaseInsensitive) == 0;
}

QString externalstorage::extractSerialFromInstanceId(const QString& instanceId) const
{
    // Instance ID에서 시리얼 번호 추출
    // 예: USB\VID_090C&PID_1000\HG2811162261300003 → HG2811162261300003
    QRegularExpression serialPattern(R"(USB\\VID_[0-9A-F]{4}&PID_[0-9A-F]{4}\\([^\\&\s]+))",
                                     QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = serialPattern.match(instanceId);

    if (match.hasMatch()) {
        QString serial = match.captured(1).trimmed();
        if (serial.length() >= 8 && serial.length() <= 50) {
            return serial;
        }
    }

    // USBSTOR 형식도 시도
    QRegularExpression usbstorPattern(R"(USBSTOR\\[^\\]+\\([^\\&\s]+)&\d+)",
                                      QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch usbstorMatch = usbstorPattern.match(instanceId);

    if (usbstorMatch.hasMatch()) {
        QString serial = usbstorMatch.captured(1).trimmed();
        if (serial.length() >= 8 && serial.length() <= 50) {
            return serial;
        }
    }

    return "N/A";
}

QString externalstorage::extractVidPidFromDeviceClass(const QString& deviceClass) const
{
    // Device Class에서 VID&PID 추출
    QRegularExpression vidPidPattern(R"((VID_[0-9A-F]{4}&PID_[0-9A-F]{4}))",
                                     QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = vidPidPattern.match(deviceClass);

    return match.hasMatch() ? match.captured(1) : "N/A";
}

QString externalstorage::extractVidPidFromHardwareId(const QString& hardwareId) const
{
    // Hardware ID에서 VID&PID 추출 (Product ID를 이용해 VID&PID 구성)
    if (hardwareId != "N/A" && hardwareId.length() == 4) {
        // Product ID만 있는 경우 일반적인 VID와 결합
        return QString("VID_090C&PID_%1").arg(hardwareId);
    }

    // 완전한 VID&PID 패턴 찾기
    QRegularExpression vidPidPattern(R"((VID_[0-9A-F]{4}&PID_[0-9A-F]{4}))",
                                     QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = vidPidPattern.match(hardwareId);

    return match.hasMatch() ? match.captured(1) : "N/A";
}

QString externalstorage::fileTimeToUTCString(qword fileTime) const
{
    if (fileTime == 0) {
        return "N/A";
    }

    FILETIME ft;
    ft.dwLowDateTime = (DWORD)(fileTime & 0xFFFFFFFF);
    ft.dwHighDateTime = (DWORD)(fileTime >> 32);

    SYSTEMTIME st;
    if (FileTimeToSystemTime(&ft, &st)) {
        return QString("%1-%2-%3 %4:%5:%6")
        .arg(st.wYear, 4, 10, QChar('0'))
            .arg(st.wMonth, 2, 10, QChar('0'))
            .arg(st.wDay, 2, 10, QChar('0'))
            .arg(st.wHour, 2, 10, QChar('0'))
            .arg(st.wMinute, 2, 10, QChar('0'))
            .arg(st.wSecond, 2, 10, QChar('0'));
    }

    return "N/A";
}

qword externalstorage::getKeyLastWriteTime(HKEY hKey)
{
    FILETIME lastWriteTime;
    LONG result = RegQueryInfoKeyW(hKey, nullptr, nullptr, nullptr, nullptr, nullptr,
                                   nullptr, nullptr, nullptr, nullptr, nullptr, &lastWriteTime);

    if (result == ERROR_SUCCESS) {
        ULARGE_INTEGER uli;
        uli.LowPart = lastWriteTime.dwLowDateTime;
        uli.HighPart = lastWriteTime.dwHighDateTime;
        return uli.QuadPart;
    }

    return 0;
}


// extractEventViewerInfo() 함수
bool externalstorage::extractEventViewerInfo()
{
    //qDebug() << "EventViewer USB 이벤트 수집 시작...";

    if (!initializeEventLogAPI()) {
        //qDebug() << "EventLog API 초기화 실패";
        return false;
    }

    bool success = true;

    // System 로그에서 USB 이벤트 수집
    if (!queryEventLogForUSBEvents("System", m_eventViewerInfoList)) {
        //qDebug() << "System 로그 조회 실패";
        success = false;
    }

    // Security 로그에서 USB 이벤트 수집 (선택적 - 권한 필요)
    QVector<USBEventInfo> securityEvents;
    if (queryEventLogForUSBEvents("Security", securityEvents)) {
        m_eventViewerInfoList.append(securityEvents);
        //qDebug() << "Security 로그에서" << securityEvents.size() << "개 이벤트 수집";
    }

    // 시간순 정렬
    std::sort(m_eventViewerInfoList.begin(), m_eventViewerInfoList.end(),
              [](const USBEventInfo& a, const USBEventInfo& b) {
                  return a.eventTime < b.eventTime;
              });

    //qDebug() << "EventViewer 수집 완료. 총" << m_eventViewerInfoList.size() << "개 USB 이벤트 발견";

    cleanupEventLogAPI();
    return success;
}

//queryEventLogForUSBEvents() 함수
bool externalstorage::queryEventLogForUSBEvents(const QString& channelName, QVector<USBEventInfo>& eventList)
{
    std::wstring wChannelName = channelName.toStdWString();

    // USB 관련 이벤트만 필터링하는 XPath 쿼리
    QString xpathQuery = QString(
        "*[System["
        "(Provider[@Name='Microsoft-Windows-DriverFrameworks-UserMode'] and EventID=10000) or "
        "(Provider[@Name='Microsoft-Windows-UserPnp'] and (EventID=20001 or EventID=20003 or EventID=20004)) or "
        "(Provider[@Name='Microsoft-Windows-Kernel-PnP'] and (EventID=400 or EventID=410)) or "
        "(Provider[@Name='Microsoft-Windows-Partition'] and EventID=1006) or "
        "(EventID=6416 and Provider[@Name='Microsoft-Windows-Security-Auditing'])"
        "]]"
        );

    std::wstring wXPathQuery = xpathQuery.toStdWString();

    // 이벤트 로그 쿼리 생성
    EVT_HANDLE hQuery = EvtQuery(NULL, wChannelName.c_str(), wXPathQuery.c_str(),
                                 EvtQueryChannelPath | EvtQueryReverseDirection);

    if (!hQuery) {
        DWORD error = GetLastError();
        //qDebug() << "EvtQuery 실패:" << channelName << "Error:" << error;
        return false;
    }

    // 이벤트 배치 처리
    const DWORD BATCH_SIZE = 100;
    EVT_HANDLE events[BATCH_SIZE];
    DWORD returned = 0;
    DWORD totalProcessed = 0;

    while (EvtNext(hQuery, BATCH_SIZE, events, INFINITE, 0, &returned)) {
        for (DWORD i = 0; i < returned; i++) {
            USBEventInfo eventInfo;

            if (parseEventXML(renderEventAsXML(events[i]), eventInfo)) {
                // USB 관련 이벤트인지 확인
                if (isUSBRelatedEvent(eventInfo.deviceInstanceID, eventInfo.rawEventData)) {
                    eventInfo.channelName = channelName;
                    eventList.append(eventInfo);
                }
            }

            EvtClose(events[i]);
        }

        totalProcessed += returned;

        // 진행상황 표시 (대량 데이터 처리시)
        if (totalProcessed % 1000 == 0) {
           // qDebug() << channelName << "채널:" << totalProcessed << "개 이벤트 처리됨";
        }
    }

    DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_ITEMS) {
       // qDebug() << "EvtNext 오류:" << error;
    }

    EvtClose(hQuery);
    //qDebug() << channelName << "채널에서 총" << eventList.size() << "개 USB 이벤트 수집 완료";
    return true;
}

// parseEventXML() 함수
bool externalstorage::parseEventXML(const QString& xmlData, USBEventInfo& eventInfo)
{
    if (xmlData.isEmpty()) {
        return false;
    }

    eventInfo.rawEventData = xmlData;

    // QXmlStreamReader를 사용한 XML 파싱
    QXmlStreamReader xml(xmlData);

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            QString elementName = xml.name().toString();

            // System 정보 추출
            if (elementName == "Provider") {
                eventInfo.providerName = xml.attributes().value("Name").toString();
            }
            else if (elementName == "EventID") {
                eventInfo.eventID = xml.readElementText().toUInt();
            }
            else if (elementName == "TimeCreated") {
                QString systemTime = xml.attributes().value("SystemTime").toString();
                eventInfo.eventTime = convertISODateToString(systemTime);
            }
            else if (elementName == "EventRecordID") {
                eventInfo.eventRecordID = xml.readElementText();
            }
            else if (elementName == "Security") {
                eventInfo.userID = xml.attributes().value("UserID").toString();
            }
            else if (elementName == "Execution") {
                eventInfo.processID = xml.attributes().value("ProcessID").toString();
                eventInfo.threadID = xml.attributes().value("ThreadID").toString();
            }
            // UserData 및 EventData에서 USB 관련 정보 추출
            else if (elementName == "DeviceInstanceID" || elementName == "DeviceId") {
                eventInfo.deviceInstanceID = xml.readElementText();
            }
            else if (elementName == "ServiceName") {
                eventInfo.serviceName = xml.readElementText();
            }
            else if (elementName == "DriverFileName") {
                eventInfo.driverFileName = xml.readElementText();
            }
            // EventData의 Data 요소들 처리
            else if (elementName == "Data") {
                QString name = xml.attributes().value("Name").toString();
                QString value = xml.readElementText();

                if (name == "DeviceInstanceId" || name == "InstanceId") {
                    eventInfo.deviceInstanceID = value;
                }
                else if (name == "DeviceDescription" || name == "Description") {
                    eventInfo.deviceDescription = value;
                }
            }
        }
    }

    if (xml.hasError()) {
        //qDebug() << "XML 파싱 오류:" << xml.errorString();
        return false;
    }

    // 이벤트 타입 결정
    eventInfo.eventType = determineEventType(eventInfo.eventID, eventInfo.providerName);

    return !eventInfo.deviceInstanceID.isEmpty() || !eventInfo.deviceDescription.isEmpty();
}

// determineEventType() 함수
QString externalstorage::determineEventType(dword eventID, const QString& providerName)
{
    // Microsoft-Windows-DriverFrameworks-UserMode
    if (providerName.contains("DriverFrameworks-UserMode", Qt::CaseInsensitive)) {
        switch (eventID) {
        case 10000: return "Install";           // USB 장치 설치 시작
        case 10001: return "InstallComplete";   // USB 장치 설치 완료
        case 10002: return "InstallFailed";     // USB 장치 설치 실패
        default: return "DriverFramework";
        }
    }
    // Microsoft-Windows-UserPnp
    else if (providerName.contains("UserPnp", Qt::CaseInsensitive)) {
        switch (eventID) {
        case 20001: return "ServiceInstall";    // 서비스 설치
        case 20003: return "Remove";            // 강제 제거 (안전 제거 없이)
        case 20004: return "SafeRemove";        // ⭐ 안전 제거
        case 20005: return "RemoveComplete";    // 제거 완료
        default: return "PnpEvent";
        }
    }
    // Microsoft-Windows-Kernel-PnP
    else if (providerName.contains("Kernel-PnP", Qt::CaseInsensitive)) {
        switch (eventID) {
        case 400: return "Connect";             // ⭐ 장치 연결
        case 410: return "Disconnect";          // ⭐ 장치 연결 해제
        case 420: return "StartDevice";         // 장치 시작
        case 421: return "StopDevice";          // 장치 중지
        default: return "KernelPnp";
        }
    }
    // Microsoft-Windows-Partition
    else if (providerName.contains("Partition", Qt::CaseInsensitive)) {
        switch (eventID) {
        case 1006: return "VolumeMount";        // 볼륨 마운트
        case 1007: return "VolumeUnmount";      // 볼륨 언마운트
        default: return "Partition";
        }
    }
    // Security 이벤트
    else if (providerName.contains("Security", Qt::CaseInsensitive)) {
        switch (eventID) {
        case 6416: return "DeviceRecognition";  // 장치 인식
        case 6417: return "DeviceRemoval";      // 장치 제거
        default: return "Security";
        }
    }

    return QString("Unknown_%1").arg(eventID);
}

// isUSBRelatedEvent() 함수
bool externalstorage::isUSBRelatedEvent(const QString& deviceInstanceID, const QString& eventData)
{
    // DeviceInstanceID 기반 필터링
    if (!deviceInstanceID.isEmpty()) {
        if (deviceInstanceID.contains("USB", Qt::CaseInsensitive) ||
            deviceInstanceID.contains("USBSTOR", Qt::CaseInsensitive) ||
            deviceInstanceID.contains("WPDBUSENUM", Qt::CaseInsensitive)) {
            return true;
        }
    }

    // 이벤트 데이터에서 USB 관련 키워드 검색
    if (eventData.contains("USB", Qt::CaseInsensitive) ||
        eventData.contains("Mass Storage", Qt::CaseInsensitive) ||
        eventData.contains("Removable", Qt::CaseInsensitive) ||
        eventData.contains("VID_", Qt::CaseInsensitive) ||
        eventData.contains("PID_", Qt::CaseInsensitive)) {
        return true;
    }

    return false;
}

//initializeEventLogAPI() 함수
bool externalstorage::initializeEventLogAPI()
{
    // 렌더링 컨텍스트 생성 (XML 렌더링용)
    m_hRenderContext = EvtCreateRenderContext(0, NULL, EvtRenderContextSystem);

    if (!m_hRenderContext) {
        //qDebug() << "EvtCreateRenderContext 실패:" << GetLastError();
        return false;
    }

    return true;
}

//cleanupEventLogAPI() 함수
void externalstorage::cleanupEventLogAPI()
{
    if (m_hRenderContext) {
        EvtClose(m_hRenderContext);
        m_hRenderContext = NULL;
    }
}

// renderEventAsXML() 함수
QString externalstorage::renderEventAsXML(EVT_HANDLE hEvent)
{
    DWORD bufferSize = 0;
    DWORD bufferUsed = 0;
    DWORD propertyCount = 0;

    // 필요한 버퍼 크기 계산
    if (!EvtRender(NULL, hEvent, EvtRenderEventXml, bufferSize, NULL, &bufferUsed, &propertyCount)) {
        if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
            bufferSize = bufferUsed;
            std::vector<wchar_t> buffer(bufferSize / sizeof(wchar_t));

            if (EvtRender(NULL, hEvent, EvtRenderEventXml, bufferSize,
                          buffer.data(), &bufferUsed, &propertyCount)) {
                return QString::fromWCharArray(buffer.data());
            }
        }
    }

    return QString();
}

// convertISODateToString() 함수
QString externalstorage::convertISODateToString(const QString& isoDateTime)
{
    // ISO 8601 형식 (2025-07-26T14:35:03.0956108Z)을 읽기 쉬운 형식으로 변환
    QDateTime dateTime = QDateTime::fromString(isoDateTime, Qt::ISODate);
    if (dateTime.isValid()) {
        return dateTime.toString("yyyy/MM/dd hh:mm:ss.zzz");
    }
    return isoDateTime; // 변환 실패시 원본 반환
}

// convertSIDToString() 함수
QString externalstorage::convertSIDToString(const QString& sidString)
{
    // SID를 사용자 친화적 이름으로 변환 (기본 구현)
    if (sidString == "S-1-5-18") return "SYSTEM";
    if (sidString == "S-1-5-19") return "LOCAL SERVICE";
    if (sidString == "S-1-5-20") return "NETWORK SERVICE";
    if (sidString.startsWith("S-1-5-21")) return "User Account";

    return sidString;
}

//  matchEventWithDevice() 함수
bool externalstorage::matchEventWithDevice(const USBEventInfo& eventInfo, const USBForensicsData& device) const
{
    // 1. DeviceInstanceID 직접 매칭
    if (!eventInfo.deviceInstanceID.isEmpty() && !device.setupAPIInfo.serialNumber.isEmpty()) {
        if (eventInfo.deviceInstanceID.contains(device.setupAPIInfo.serialNumber, Qt::CaseInsensitive)) {
            return true;
        }
    }

    // 2. 시리얼 번호 추출 후 매칭
    QString eventSerial = extractSerialFromInstanceId(eventInfo.deviceInstanceID);
    if (matchDevicesBySerial(eventSerial, device.setupAPIInfo.serialNumber)) {
        return true;
    }

    // 3. VID/PID 매칭
    if (eventInfo.deviceInstanceID.contains("VID_", Qt::CaseInsensitive)) {
        QString eventVidPid = extractVidPidFromDeviceClass(eventInfo.deviceInstanceID);
        QString deviceVidPid = extractVidPidFromHardwareId(device.setupAPIInfo.productID);
        if (matchDevicesByVidPid(eventVidPid, deviceVidPid)) {
            return true;
        }
    }

    return false;
}

// ⭐ 12. handleOrphanEvents() 함수
void externalstorage::handleOrphanEvents()
{
    // 중복 체크를 위한 장치 ID 목록
    QSet<QString> existingDeviceIds;
    for (const auto& device : m_usbForensicsDevices) {
        existingDeviceIds.insert(device.deviceKey);
    }

    // EventViewer에만 있고 다른 소스에 없는 이벤트들을 별도 장치로 처리
    QMap<QString, QVector<USBEventInfo>> orphanGroups;

    for (const auto& eventInfo : m_eventViewerInfoList) {
        bool isMatched = false;

        // 기존 장치들과 매칭 확인
        for (auto& device : m_usbForensicsDevices) {
            if (matchEventWithDevice(eventInfo, device)) {
                if (!device.hasEventViewerData) {
                    device.hasEventViewerData = true;
                }
                device.eventViewerInfoList.append(eventInfo);
                isMatched = true;
                break;
            }
        }

        // 매칭되지 않은 고아 이벤트는 그룹화
        if (!isMatched && !eventInfo.deviceInstanceID.isEmpty()) {
            QString deviceKey = extractSerialFromInstanceId(eventInfo.deviceInstanceID);
            if (deviceKey.isEmpty()) {
                deviceKey = eventInfo.deviceInstanceID;
            }

            if (!existingDeviceIds.contains(deviceKey)) {
                orphanGroups[deviceKey].append(eventInfo);
            }
        }
    }

    // 그룹화된 고아 이벤트들을 새 장치로 생성
    for (auto it = orphanGroups.begin(); it != orphanGroups.end(); ++it) {
        const QString& deviceKey = it.key();
        const QVector<USBEventInfo>& events = it.value();

        USBForensicsData orphanDevice;
        orphanDevice.hasEventViewerData = true;
        orphanDevice.primarySource = "EventViewer";
        orphanDevice.deviceKey = deviceKey;
        orphanDevice.eventViewerInfoList = events;

        // 첫 번째 이벤트에서 메타데이터 추출
        if (!events.isEmpty()) {
            const USBEventInfo& firstEvent = events.first();
            orphanDevice.metadata.serialNumber = extractSerialFromInstanceId(firstEvent.deviceInstanceID);
            orphanDevice.metadata.vidPid = extractVidPidFromDeviceClass(firstEvent.deviceInstanceID);
            orphanDevice.metadata.deviceClassName = firstEvent.deviceDescription;
            orphanDevice.metadata.instanceID = firstEvent.deviceInstanceID;
        }

        m_usbForensicsDevices.append(orphanDevice);
        qDebug() << "단독 이벤트를 새 장치로 생성:" << deviceKey << "(" << events.size() << "개 이벤트)";
    }
}

// =============================================================================
// JSON 변환 함수들 구현 (externalstorage.cpp 파일 끝에 추가)
// =============================================================================

QJsonObject externalstorage::toJsonObject() const {
    QJsonObject result;
    /*
    // 메타데이터
    result["collection_info"] = QJsonObject({
        {"module_name", "USB_Storage"},
        {"collection_time", QDateTime::currentDateTime().toString(Qt::ISODate)},
        {"total_devices", static_cast<int>(m_usbForensicsDevices.size())},
        {"version", "1.0"}
    });
    */
    // 모든 USB 장치 데이터
    QJsonArray devicesArray;
    for (int i = 0; i < m_usbForensicsDevices.size(); i++) {
        const USBForensicsData& device = m_usbForensicsDevices[i];

        QJsonObject deviceObj;
        deviceObj["device_index"] = i + 1;
        deviceObj["device_key"] = device.deviceKey;
        deviceObj["primary_source"] = device.primarySource;

        // 데이터 소스 플래그들
        QJsonObject sourceFlags;
        sourceFlags["has_usbstor_data"] = device.hasUSBSTORData;
        sourceFlags["has_usb_data"] = device.hasUSBData;
        sourceFlags["has_device_classes_data"] = device.hasDeviceClassesData;
        sourceFlags["has_portable_device_data"] = device.hasPortableDeviceData;
        sourceFlags["has_setupapi_data"] = device.hasSetupAPIData;
        sourceFlags["has_event_viewer_data"] = device.hasEventViewerData;
        deviceObj["data_sources"] = sourceFlags;

        // 각 데이터 소스별 상세 정보
        if (device.hasUSBSTORData) {
            deviceObj["usbstor_info"] = usbstorInfoToJson(device.usbstorInfo);
        }

        if (device.hasUSBData) {
            deviceObj["usb_info"] = usbInfoToJson(device.usbInfo);
        }

        if (device.hasDeviceClassesData) {
            deviceObj["device_classes_info"] = deviceClassesInfoToJson(device.deviceClassesInfo);
        }

        if (device.hasPortableDeviceData) {
            deviceObj["portable_device_info"] = portableDeviceInfoToJson(device.portableDeviceInfo);
        }

        if (device.hasSetupAPIData) {
            deviceObj["setupapi_info"] = setupAPIInfoToJson(device.setupAPIInfo);
        }

        deviceObj["volume_info"] = volumeInfoToJson(device.volumeInfo);
        deviceObj["connection_times"] = connectionTimesToJson(device.connectionTimes);
        deviceObj["device_metadata"] = deviceMetadataToJson(device.metadata);

        if (device.hasEventViewerData) {
            deviceObj["event_viewer_info"] = usbEventInfoListToJson(device.eventViewerInfoList);
        }

        devicesArray.append(deviceObj);
    }

    result["usb_devices"] = devicesArray;

    return result;
}

QJsonObject externalstorage::usbstorInfoToJson(const USBSTORInfo& info) const {
    return QJsonObject({
        {"path", info.path},
        {"address", static_cast<int>(info.address)},
        {"capabilities", static_cast<int>(info.capabilities)},
        {"class_guid", info.classGUID},
        {"compatible_ids", info.compatibleIDs},
        {"config_flags", static_cast<int>(info.configFlags)},
        {"container_id", info.containerID},
        {"device_desc", info.deviceDesc},
        {"driver", info.driver},
        {"friendly_name", info.friendlyName},
        {"hardware_id", info.hardwareID},
        {"mfg", info.mfg},
        {"service", info.service}
    });
}

QJsonObject externalstorage::usbInfoToJson(const USBInfo& info) const {
    return QJsonObject({
        {"path", info.path},
        {"address", static_cast<int>(info.address)},
        {"capabilities", static_cast<int>(info.capabilities)},
        {"class_guid", info.classGUID},
        {"compatible_ids", info.compatibleIDs},
        {"config_flags", static_cast<int>(info.configFlags)},
        {"container_id", info.containerID},
        {"device_desc", info.deviceDesc},
        {"driver", info.driver},
        {"hardware_id", info.hardwareID},
        {"mfg", info.mfg},
        {"parent_id_prefix", info.parentIdPrefix},
        {"service", info.service}
    });
}

QJsonObject externalstorage::deviceClassesInfoToJson(const DeviceClassesInfo& info) const {
    return QJsonObject({
        {"path", info.path},
        {"device_instance", info.deviceinstance},
        {"last_write_time", static_cast<qint64>(info.lastWriteTime)},
        {"last_write_time_formatted", fileTimeToUTCString(info.lastWriteTime)}
    });
}


QJsonObject externalstorage::portableDeviceInfoToJson(const PortableDeviceInfo& info) const {
    return QJsonObject({
        {"path", info.path},
        {"friendly_name", info.friendlyName}
    });
}

QJsonObject externalstorage::setupAPIInfoToJson(const SetupAPIInfo& info) const {
    return QJsonObject({
        {"volume_name", info.volumeName},
        {"serial_number", info.serialNumber},
        {"volume_guid", info.volumeGUID},
        {"product_id", info.productID},
        {"first_connection_time", info.firstConnectionTime},
        {"first_connection_after_boot", info.firstConnectionAfterBoot},
        {"last_connection_time", info.lastConnectionTime},
        {"last_disconnection_time", info.lastDisconnectionTime},
        {"user_account", info.userAccount}
    });
}

QJsonObject externalstorage::volumeInfoToJson(const VolumeInfo& info) const {
    return QJsonObject({
        {"volume_label", info.volumeLabel},
        {"drive_letter", info.driveLetter},
        {"volume_guid", info.volumeGUID},
        {"mount_point", info.mountPoint},
        {"file_system", info.fileSystem}
    });
}

QJsonObject externalstorage::connectionTimesToJson(const ConnectionTimes& info) const {
    return QJsonObject({
        {"first_install_time", info.firstInstallTime},
        {"first_connection_time", info.firstConnectionTime},
        {"last_connection_time", info.lastConnectionTime},
        {"last_disconnection_time", info.lastDisconnectionTime},
        {"boot_after_connect_time", info.bootAfterConnectTime}
    });
}

QJsonObject externalstorage::deviceMetadataToJson(const DeviceMetadata& info) const {
    return QJsonObject({
        {"device_class_name", info.deviceClassName},
        {"instance_id", info.instanceID},
        {"serial_number", info.serialNumber},
        {"vid_pid", info.vidPid}
    });
}

QJsonArray externalstorage::usbEventInfoListToJson(const QVector<USBEventInfo>& eventList) const {
    QJsonArray result;

    for (const USBEventInfo& event : eventList) {
        QJsonObject eventObj({
            {"event_time", event.eventTime},
            {"event_type", event.eventType},
            {"device_instance_id", event.deviceInstanceID},
            {"event_record_id", event.eventRecordID},
            {"provider_name", event.providerName},
            {"event_id", static_cast<int>(event.eventID)},
            {"user_id", event.userID},
            {"service_name", event.serviceName},
            {"driver_file_name", event.driverFileName},
            {"device_description", event.deviceDescription},
            {"process_id", event.processID},
            {"thread_id", event.threadID},
            {"channel_name", event.channelName},
            {"raw_event_data", event.rawEventData}
        });
        result.append(eventObj);
    }

    return result;
}
