// ClientNetworkManager.h - 프로덕션 레벨 완전 재작성
// 단순하고 안정적인 클라이언트 네트워크 통신
// 서버 프로토콜 100% 호환, PC 정보 수집 통합

#ifndef CLIENTNETWORKMANAGER_H
#define CLIENTNETWORKMANAGER_H

#include "pch.h"
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QMutex>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonDocument>

// =================================================================
// ClientNetworkManager - 단순하고 안정적인 구현
// 4가지 핵심 기능:
// 1. PC 등록 (MAC 기반 + Owner_ID 처리)
// 2. Task 수신 및 처리
// 3. 연결 관리 (재연결, 상태 관리)
// 4. 포렌식 데이터 전송
// =================================================================

class ClientNetworkManager : public QObject {
    Q_OBJECT

public:
    // =================================================================
    // 기본 열거형들
    // =================================================================

    enum ConnectionStatus {
        Disconnected = 0,
        Connecting = 1,
        Connected = 2,
        Registering = 3,
        WaitingOwnerID = 4,
        Ready = 5,
        Error = 6
    };

    // 서버 바이너리 메시지 타입 (서버와 정확히 일치)
    enum class MessageType : uint8_t {
        DATA_PACKET = 0x01,      // 클라이언트 → 서버 (포렌식 데이터)
        TASK_REQUEST = 0x02,     // 서버 → 클라이언트 (작업 지시)
        TASK_RESPONSE = 0x03,    // 클라이언트 → 서버 (작업 완료 응답)
        PC_INFO = 0x04,          // 클라이언트 → 서버 (PC 등록)
        HEARTBEAT = 0x05         // 양방향 (연결 유지)
    };

    // =================================================================
    // 핵심 데이터 구조체들 (DB 스키마와 일치)
    // =================================================================

    // client_info 테이블과 정확히 매칭되는 PC 정보
    struct PCInfo {
        QString pcId;        // MAC_XX-XX-XX-XX-XX-XX (PRIMARY KEY)
        QString pcName;      // Windows PC명
        QString ip;          // 현재 IP 주소
        QString os;          // OS 버전

        PCInfo() = default;
        bool isValid() const { return !pcId.isEmpty() && !pcName.isEmpty(); }
    };

    // Task 요청 구조체
    struct TaskRequest {
        QString taskId;      // Task 고유 ID
        QString taskType;    // Task 타입 (USB_DATA, BROWSER_DATA 등)
        QJsonObject params;  // 추가 파라미터
        QDateTime requestTime;

        TaskRequest() { requestTime = QDateTime::currentDateTime(); }
        bool isValid() const { return !taskId.isEmpty() && !taskType.isEmpty(); }
    };

public:
    // =================================================================
    // 생성자/소멸자
    // =================================================================

    explicit ClientNetworkManager(QObject* parent = nullptr);
    virtual ~ClientNetworkManager();

    // =================================================================
    // 연결 관리 (단순한 인터페이스)
    // =================================================================

    bool connectToServer(const QString& serverIP = "", uint16_t port = 8443);
    void disconnectFromServer();
    bool isConnected() const;
    ConnectionStatus getConnectionStatus() const;
    QString getStatusText() const;

    // =================================================================
    // PC 등록 프로세스 (자동화)
    // =================================================================

    bool startRegistration();                          // PC 등록 시작
    bool submitOwnerID(const QString& ownerID);        // Owner_ID 제출
    bool isRegistrationComplete() const;               // 등록 완료 여부
    QString getCurrentPCId() const;                    // 현재 PC ID
    bool resendPCInfo(const QString& reason = "");     // PC 정보 재전송

    // =================================================================
    // 포렌식 데이터 전송 (기존 인터페이스 호환)
    // =================================================================

    void pushData(const QString& moduleType, const QString& fileName, const QByteArray& jsonData);
    bool sendQueuedData();                             // 큐 데이터 전송
    int getQueueSize() const;                          // 큐 크기

    // Task ID와 함께 데이터 전송 (새 기능)
    bool sendForensicDataWithTaskId(const QString& taskId, const QString& moduleType,
                                    const QString& fileName, const QByteArray& jsonData);

    // =================================================================
    // 연결 설정
    // =================================================================

    void setAutoReconnect(bool enabled);               // 자동 재연결 설정
    void setHeartbeat(bool enabled);                   // 하트비트 설정

signals:
    // =================================================================
    // 연결 관련 시그널
    // =================================================================

    void connected();                                  // 서버 연결 완료
    void disconnected();                              // 연결 끊김
    void connectionStatusChanged(ConnectionStatus status); // 상태 변경
    void errorOccurred(const QString& error);         // 에러 발생

    // =================================================================
    // 등록 관련 시그널
    // =================================================================

    void registrationStarted();                       // 등록 프로세스 시작
    void ownerIdRequired();                           // Owner_ID 입력 필요
    void registrationCompleted(const QString& pcId);  // 등록 완료
    void registrationFailed(const QString& reason);   // 등록 실패

    // =================================================================
    // Task 관련 시그널
    // =================================================================

    void taskReceived(const TaskRequest& task);       // Task 수신
    void dataTransmitted(const QString& moduleType, qint64 bytes); // 데이터 전송 완료

private slots:
    // =================================================================
    // 네트워크 이벤트 처리
    // =================================================================

    void onConnected();
    void onDisconnected();
    void onError(QAbstractSocket::SocketError error);
    void onDataReceived();

    // 타이머 이벤트
    void onReconnectTimer();
    void onHeartbeatTimer();

private:
    // =================================================================
    // 네트워크 관련 멤버
    // =================================================================

    QTcpSocket* socket_;
    QString serverIP_;
    uint16_t serverPort_;
    ConnectionStatus status_;

    // 메시지 수신 버퍼
    QByteArray receiveBuffer_;
    uint32_t expectedMessageSize_;
    bool waitingForHeader_;

    // =================================================================
    // PC 정보 및 등록 상태
    // =================================================================

    PCInfo currentPCInfo_;
    bool registrationComplete_;
    bool ownerIdNeeded_;

    // =================================================================
    // 데이터 전송 큐
    // =================================================================

    struct QueuedData {
        QString moduleType;
        QString fileName;
        QByteArray data;
        QString taskId;      // 선택적 Task ID
    };
    QList<QueuedData> dataQueue_;
    mutable QMutex queueMutex_;

    // =================================================================
    // 자동 관리 타이머
    // =================================================================

    QTimer* reconnectTimer_;
    QTimer* heartbeatTimer_;
    bool autoReconnectEnabled_;
    bool heartbeatEnabled_;
    int reconnectAttempts_;
    static const int MAX_RECONNECT_ATTEMPTS = 5;

    // =================================================================
    // PC 정보 수집 (기존 ClientUtils에서 통합)
    // =================================================================

    PCInfo collectPCInfo();
    QString generatePCId();
    QString getCurrentPCName();
    QString getCurrentIPAddress();
    QString getCurrentOSVersion();
    QStringList getAllMACAddresses();
    QString getPrimaryMACAddress(const QStringList& macs);

    // =================================================================
    // 서버 IP 검색 (기존 ip_helper.h에서 통합)
    // =================================================================

    QString findServerIP();
    bool testConnection(const QString& ip, uint16_t port);

    // =================================================================
    // 메시지 처리
    // =================================================================

    bool sendBinaryMessage(MessageType type, const QByteArray& payload);
    void processIncomingData();
    void handleMessage(MessageType type, const QByteArray& payload);

    // 특정 메시지 처리
    void handlePCInfoResponse(const QByteArray& payload);
    void handleTaskRequest(const QByteArray& payload);
    void handleHeartbeatResponse(const QByteArray& payload);

    // 메시지 생성
    QByteArray createPCInfoMessage();
    QByteArray createOwnerIdMessage(const QString& ownerID);
    QByteArray createTaskResponseMessage(const QString& taskId, bool success);
    QByteArray createHeartbeatMessage();

    // =================================================================
    // 상태 관리
    // =================================================================

    void setConnectionStatus(ConnectionStatus newStatus);
    void startReconnectProcess();
    void stopReconnectProcess();
    void resetConnectionState();

    // =================================================================
    // 유틸리티 함수
    // =================================================================

    void logInfo(const QString& message);
    void logWarning(const QString& message);
    void logError(const QString& message);
};

#endif // CLIENTNETWORKMANAGER_H
