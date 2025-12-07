#ifndef EXTERNALSTORAGE_H
#define EXTERNALSTORAGE_H

#include "pch.h"

// Qt 기반 타입 정의 (pch.h에 이미 있는 것들 활용)
using qstring = QString;
using dword = DWORD;
using qword = ULONGLONG;

// =============================================================================
// 실제 레지스트리 값 구조체들
// =============================================================================

/**
 * @brief USBSTOR에서 수집되는 USB 장치 정보
 */
struct USBSTORInfo {
    qstring path;               // 경로(기본값) - REG_SZ
    dword address;              // Address - REG_DWORD (0x00000004)
    dword capabilities;         // Capabilities - REG_DWORD (0x00000010)
    qstring classGUID;          // ClassGUID - REG_SZ ({4d36e967-e325-11ce-bfc1-08002be10318})
    qstring compatibleIDs;      // CompatibleIDs - REG_MULTI_SZ (USBSTOR\Disk USBSTOR\RAW GenDisk)
    dword configFlags;          // ConfigFlags - REG_DWORD (0x00000000)
    qstring containerID;        // ContainerID - REG_SZ ({18eaf965-7eca-567b-956c-6f78644d5109})
    qstring deviceDesc;         // DeviceDesc - REG_SZ (@disk.inf%disk_devdesc%;Disk drive)
    qstring driver;             // Driver - REG_SZ ({4d36e967-e325-11ce-bfc1-08002be10318}\000...)
    qstring friendlyName;       // FriendlyName - REG_SZ (Generic MassStorageClass USB Device)
    qstring hardwareID;         // HardwareID - REG_MULTI_SZ (USBSTOR\DiskGeneric_MassStorageClass1539 U...)
    qstring mfg;                // Mfg - REG_SZ (@disk.inf%genmanufacturer%;(Standard disk dri...)
    qstring service;            // Service - REG_SZ (disk)

    USBSTORInfo() :
        path("N/A"), address(0), capabilities(0), classGUID("N/A"), compatibleIDs("N/A"),
        configFlags(0), containerID("N/A"), deviceDesc("N/A"), driver("N/A"),
        friendlyName("N/A"), hardwareID("N/A"), mfg("N/A"), service("N/A") {}
};

/**
 * @brief USB에서 수집되는 USB 장치 정보
 */
struct USBInfo {
    qstring path;               // 경로(기본값) - REG_SZ
    dword address;              // Address - REG_DWORD (0x00000002)
    dword capabilities;         // Capabilities - REG_DWORD (0x00000084)
    qstring classGUID;          // ClassGUID - REG_SZ ({36fc9e60-c465-11cf-8056-444553540000})
    qstring compatibleIDs;      // CompatibleIDs - REG_MULTI_SZ (USB\Class_09&SubClass_00)
    dword configFlags;          // ConfigFlags - REG_DWORD (0x00000000)
    qstring containerID;        // ContainerID - REG_SZ ({94b56f0-1df6-11e0-ac64-000200c9a66})
    qstring deviceDesc;         // DeviceDesc - REG_SZ (@usbhub3.inf%usbhub3.usb20hubdesc%;Generi...)
    qstring driver;             // Driver - REG_SZ ({36fc9e60-c465-11cf-8056-444553540000}\000...)
    qstring hardwareID;         // HardwareID - REG_MULTI_SZ (USB\VID_05E3&PID_0610&REV_0663 USB\VID...)
    qstring mfg;                // Mfg - REG_SZ (@usbhub3.inf%generic.mfg%;(Standard USB HU...)
    qstring parentIdPrefix;     // ParentIdPrefix - REG_SZ (7&3805f545&0)
    qstring service;            // Service - REG_SZ (USBHUB3)

    USBInfo() :
        path("N/A"), address(0), capabilities(0), classGUID("N/A"), compatibleIDs("N/A"),
        configFlags(0), containerID("N/A"), deviceDesc("N/A"), driver("N/A"),
        hardwareID("N/A"), mfg("N/A"), parentIdPrefix("N/A"), service("N/A") {}
};

/**
 * @brief DeviceClasses 레지스트리 정보
 */
struct DeviceClassesInfo {
    qstring path;               // 경로(기본값) - REG_SZ
    qstring deviceinstance;     // DeviceInstance - STORAGE\Volume\_??_USBSTOR#Disk&Ven_SMI&Prod_USB_DISK&Rev_1100#HG2811162261300003&0#{53f56307-b6bf-11d0-94f2-00a0c91efb8b}
    qword lastWriteTime;        // 레지스트리 마지막 수정 시간 (raw FILETIME)

    DeviceClassesInfo() :
        path("N/A"), deviceinstance("N/A"), lastWriteTime(0) {}
};

/**
 * @brief Portable Devices 레지스트리 정보
 */
struct PortableDeviceInfo {
    qstring path;               // 경로(기본값) - REG_SZ - (값 설정 안됨)
    qstring friendlyName;       // 사용자 친화적 이름 - D:\

    PortableDeviceInfo() :
        path("N/A"), friendlyName("N/A") {}
};

/**
 * @brief EventViewer에서 수집되는 USB 관련 이벤트 정보
 */
struct USBEventInfo {
    qstring eventTime;
    qstring eventType;
    qstring deviceInstanceID;
    qstring eventRecordID;
    qstring providerName;
    dword eventID;
    qstring userID;
    qstring serviceName;
    qstring driverFileName;
    qstring deviceDescription;
    qstring processID;
    qstring threadID;
    qstring channelName;
    qstring rawEventData;

    USBEventInfo() :
        eventTime("N/A"), eventType("N/A"), deviceInstanceID("N/A"), eventRecordID("N/A"),
        providerName("N/A"), eventID(0), userID("N/A"), serviceName("N/A"),
        driverFileName("N/A"), deviceDescription("N/A"), processID("N/A"),
        threadID("N/A"), channelName("N/A"), rawEventData("N/A") {}
};

// =============================================================================
// 분석 및 통합 정보 구조체들
// =============================================================================

/**
 * @brief SetupAPI dev log에서 추출되는 9가지 핵심 정보
 */
struct SetupAPIInfo {
    qstring volumeName;                 // 볼륨명
    qstring serialNumber;               // 시리얼넘버
    qstring volumeGUID;                 // 볼륨 GUID
    qstring productID;                  // Product ID
    qstring firstConnectionTime;        // 최초연결 시간
    qstring firstConnectionAfterBoot;   // 부팅 이후 최초 연결 시간
    qstring lastConnectionTime;         // 마지막 연결 시간
    qstring lastDisconnectionTime;      // 마지막 연결 해제 시간
    qstring userAccount;                // 저장매체를 사용한 사용자 계정

    SetupAPIInfo() :
        volumeName("N/A"), serialNumber("N/A"), volumeGUID("N/A"), productID("N/A"),
        firstConnectionTime("N/A"), firstConnectionAfterBoot("N/A"),
        lastConnectionTime("N/A"), lastDisconnectionTime("N/A"), userAccount("N/A") {}
};

/**
 * @brief 볼륨 및 마운트 정보
 */
struct VolumeInfo {
    qstring volumeLabel;        // 볼륨 레이블
    qstring driveLetter;        // 드라이브 문자 (C:, D: 등)
    qstring volumeGUID;         // 볼륨 GUID
    qstring mountPoint;         // 마운트 포인트
    qstring fileSystem;        // 파일 시스템 (NTFS, FAT32, exFAT)

    VolumeInfo() :
        volumeLabel("N/A"), driveLetter("N/A"), volumeGUID("N/A"),
        mountPoint("N/A"), fileSystem("N/A") {}
};

/**
 * @brief 연결 시간 통합 정보
 */
struct ConnectionTimes {
    qstring firstInstallTime;           // 최초 설치 시간 (레지스트리)
    qstring firstConnectionTime;        // 최초 연결 시간 (SetupAPI)
    qstring lastConnectionTime;         // 마지막 연결 시간
    qstring lastDisconnectionTime;      // 마지막 해제 시간
    qstring bootAfterConnectTime;       // 부팅 후 최초 연결 시간

    ConnectionTimes() :
        firstInstallTime("N/A"), firstConnectionTime("N/A"), lastConnectionTime("N/A"),
        lastDisconnectionTime("N/A"), bootAfterConnectTime("N/A") {}
};

/**
 * @brief 장치 매칭 및 식별용 메타데이터
 */
struct DeviceMetadata {
    qstring deviceClassName;    // Device Class 이름 (매칭용)
    qstring instanceID;         // Instance ID (매칭용)
    qstring serialNumber;       // 추출된 시리얼 번호 (매칭용)
    qstring vidPid;            // VID_xxxx&PID_xxxx (매칭용)

    DeviceMetadata() :
        deviceClassName("N/A"), instanceID("N/A"),
        serialNumber("N/A"), vidPid("N/A") {}
};

// =============================================================================
// 최종 통합 구조체
// =============================================================================

/**
 * @brief 통합된 USB 포렌식 장치 정보
 */
struct USBForensicsData {
    // 실제 레지스트리 데이터
    USBSTORInfo usbstorInfo;            // USBSTOR 레지스트리 정보
    USBInfo usbInfo;                    // USB 레지스트리 정보
    DeviceClassesInfo deviceClassesInfo; // DeviceClasses 레지스트리 정보
    PortableDeviceInfo portableDeviceInfo; // Portable Devices 레지스트리 정보
    QVector<USBEventInfo> eventViewerInfoList; // EventViewer에서 수집된 USB 이벤트 정보

    // 분석 정보
    SetupAPIInfo setupAPIInfo;          // SetupAPI에서 추출된 9가지 정보
    VolumeInfo volumeInfo;              // 볼륨 및 마운트 정보
    ConnectionTimes connectionTimes;    // 연결 시간 통합 정보
    DeviceMetadata metadata;            // 매칭 및 식별용 메타데이터

    // 데이터 소스 플래그
    bool hasUSBSTORData;        // USBSTOR 데이터 존재 여부
    bool hasUSBData;            // USB 데이터 존재 여부
    bool hasDeviceClassesData;  // DeviceClasses 데이터 존재 여부
    bool hasPortableDeviceData; // Portable Device 데이터 존재 여부
    bool hasSetupAPIData;       // SetupAPI 데이터 존재 여부
    bool hasEventViewerData; // EventViewer 데이터 존재 여부

    // 매칭 정보
    qstring primarySource;      // 주요 데이터 소스 (USBSTOR/USB/SetupAPI)
    qstring deviceKey;          // 장치 식별 키 (매칭용)

    USBForensicsData() :
        hasUSBSTORData(false), hasUSBData(false), hasDeviceClassesData(false),
        hasPortableDeviceData(false), hasSetupAPIData(false), hasEventViewerData(false),
        primarySource("N/A"), deviceKey("N/A") {}
};

// =============================================================================
// 클래스 선언
// =============================================================================

class externalstorage : public QObject
{
    Q_OBJECT

public:
    explicit externalstorage(QObject *parent = nullptr);

    // 메인 수집 함수
    bool collectUSBForensicsData();

    // 결과 조회 함수
    const QVector<USBForensicsData>& getUSBForensicsDevices() const;

    // 출력 함수들
    void printDeviceSummary() const;
    void printTimestampAnalysis() const;
    void printDetailedResults() const;
    void printStatistics() const;

    // JSON 변환 함수
    QJsonObject toJsonObject() const;

private:
    // =============================================================================
    // 레지스트리 접근 함수들
    // =============================================================================
    bool openRegistryKey(HKEY rootKey, const QString& subKeyPath, HKEY& resultKey);
    bool readRegistryDWORD(HKEY hKey, const QString& valueName, dword& result);
    bool readRegistryString(HKEY hKey, const QString& valueName, QString& result);
    bool readRegistryMultiString(HKEY hKey, const QString& valueName, QString& result);
    qword getKeyLastWriteTime(HKEY hKey);
    bool enumerateSubKeys(HKEY hKey, QVector<QString>& subKeys);

    // =============================================================================
    // 데이터 수집 함수들
    // =============================================================================
    bool extractUSBSTORInfo();
    bool extractUSBInfo();
    bool extractDeviceClassesInfo();
    bool extractPortableDevicesInfo();
    bool extractSetupAPIInfo();
    bool extractVolumeInfo();
    bool extractConnectionTimes();

    // =============================================================================
    // 데이터 통합 및 매칭 함수들
    // =============================================================================
    bool combineAndMatchData();
    void generateDeviceMetadata(USBForensicsData& device);
    bool matchDevicesBySerial(const QString& serial1, const QString& serial2) const;
    bool matchDevicesByContainerID(const QString& id1, const QString& id2);
    bool matchDevicesByVidPid(const QString& vidpid1, const QString& vidpid2) const;

    // =============================================================================
    // SetupAPI dev log 파싱 함수들
    // =============================================================================
    QString findSetupAPILogPath();
    bool parseSetupAPILogFile(const QString& logFilePath);
    void extractSetupAPIDeviceInfo(const QString& logSection, SetupAPIInfo& info);

    // SetupAPI에서 9가지 정보 추출 함수들
    QString extractVolumeName(const QString& logText);
    QString extractSerialNumber(const QString& logText);
    QString extractVolumeGUID(const QString& logText);
    QString extractProductID(const QString& logText);
    QString extractFirstConnectionTime(const QString& logText);
    QString extractFirstConnectionAfterBoot(const QString& logText);
    QString extractLastConnectionTime(const QString& logText);
    QString extractLastDisconnectionTime(const QString& logText);
    QString extractUserAccount(const QString& logText);

    // =============================================================================
    // EventViewer 관련 함수들
    // =============================================================================
    bool extractEventViewerInfo();
    bool queryEventLogForUSBEvents(const QString& channelName, QVector<USBEventInfo>& eventList);
    bool parseEventXML(const QString& xmlData, USBEventInfo& eventInfo);
    QString determineEventType(dword eventID, const QString& providerName);
    bool isUSBRelatedEvent(const QString& deviceInstanceID, const QString& eventData);
    bool initializeEventLogAPI();
    void cleanupEventLogAPI();
    QString renderEventAsXML(EVT_HANDLE hEvent);
    QString convertISODateToString(const QString& isoDateTime);
    QString convertSIDToString(const QString& sidString);
    bool matchEventWithDevice(const USBEventInfo& eventInfo, const USBForensicsData& device) const;
    void handleOrphanEvents();

    // =============================================================================
    // 헬퍼 함수들
    // =============================================================================
    bool isExternalStorageDevice(const QString& deviceClass);           // 기존 함수 (이미 있음)
    bool isStorageDeviceFromSetupAPI(const QString& logSection);        // 새로 추가
    bool isStorageDeviceFromDeviceClasses(const QString& devicePath);   // 새로 추가
    bool isExcludedDevice(const QString& deviceInfo);                   // 새로 추가
    QString extractSerialFromInstanceId(const QString& instanceId) const;
    QString extractVidPidFromDeviceClass(const QString& deviceClass) const;
    QString extractVidPidFromHardwareId(const QString& hardwareId) const;
    QString fileTimeToUTCString(qword fileTime) const;

    // =============================================================================
    // JSON 변환 헬퍼 함수들
    // =============================================================================
    QJsonObject usbstorInfoToJson(const USBSTORInfo& info) const;
    QJsonObject usbInfoToJson(const USBInfo& info) const;
    QJsonObject deviceClassesInfoToJson(const DeviceClassesInfo& info) const;
    QJsonObject portableDeviceInfoToJson(const PortableDeviceInfo& info) const;
    QJsonObject setupAPIInfoToJson(const SetupAPIInfo& info) const;
    QJsonObject volumeInfoToJson(const VolumeInfo& info) const;
    QJsonObject connectionTimesToJson(const ConnectionTimes& info) const;
    QJsonObject deviceMetadataToJson(const DeviceMetadata& info) const;
    QJsonArray usbEventInfoListToJson(const QVector<USBEventInfo>& eventList) const;

    // =============================================================================
    // 데이터 저장소
    // =============================================================================
    QVector<USBForensicsData> m_usbForensicsDevices;    // 최종 통합 데이터

    // 개별 데이터 저장소 (중간 처리용)
    QVector<USBSTORInfo> m_usbstorInfoList;
    QVector<USBInfo> m_usbInfoList;
    QVector<DeviceClassesInfo> m_deviceClassesList;
    QVector<PortableDeviceInfo> m_portableDevicesList;
    QVector<SetupAPIInfo> m_setupAPIInfoList;
    QVector<VolumeInfo> m_volumeInfoList;
    QVector<ConnectionTimes> m_connectionTimesList;
    QVector<USBEventInfo> m_eventViewerInfoList;
    EVT_HANDLE m_hRenderContext;

signals:

};

#endif // EXTERNALSTORAGE_H
