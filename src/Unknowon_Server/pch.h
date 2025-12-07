#ifndef PCH_H
#define PCH_H

// =================================================================
// 🔥 Forensic Server - Precompiled Header (PCH)1
// Agent 호환 Qt TLS 서버 구현용
// =================================================================

// 표준 C++ 헤더
#include <iostream>
#include <string>
#include <fstream>
#include <cstdint>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <queue>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <exception>
#include <algorithm>
#include <memory>
#include <atomic>
#include <cstring>
#include <functional>
#include <chrono>

// Windows API 헤더 (Windows 전용)
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <winevt.h>
#include <winnt.h>
#include <winreg.h>
#include <shlobj.h>
#include <shellapi.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ntdll.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "secur32.lib")

typedef u_long Ip;
// Windows에서는 socklen_t가 정의되지 않으므로 int 사용
typedef int socklen_t;
#else
// Unix/Linux 헤더
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
typedef uint32_t Ip;
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#endif

// Qt Core 헤더
#include <QtCore>
#include <QObject>
#include <QDebug>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QList>
#include <QQueue>
#include <QMap>
#include <QHash>
#include <QSet>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QDateTime>
#include <QTime>
#include <QTimer>
#include <QUuid>

// Qt 파일시스템 및 유틸리티 헤더
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QTextStream>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QByteArray>
#include <QSettings>
#include <QTemporaryFile>
#include <QProcess>

// Qt 스레딩 및 동시성 헤더
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QReadWriteLock>
#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QFuture>
#include <QFutureWatcher>

// Qt Concurrent 모듈
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QtConcurrent/QtConcurrent>
#else
#include <QtCore/QtConcurrentRun>
#endif

// Qt Network 헤더 (기본 네트워킹)
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QTcpServer>
#include <QUdpSocket>
#include <QHostAddress>

// Qt SSL/TLS 헤더 (보안 통신)
#include <QSslSocket>
#include <QSslServer>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslCipher>

// ✅ Qt SQL 헤더 (데이터베이스) - 활성화!
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlField>
#include <QSqlDriver>

// Qt JSON 헤더
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

// Qt XML 헤더 (필요시)
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

// 네임스페이스
using namespace std;

// 공통 타입 정의
typedef std::string qstring;  // 기존 코드와의 호환성
typedef uint32_t dword;
typedef uint64_t qword;

// 디버그 매크로
#ifdef DEBUG_MODE
#define DEBUG_LOG(msg) qDebug() << "[DEBUG]" << msg
#define DEBUG_FUNCTION() qDebug() << "[DEBUG] Function:" << __FUNCTION__
#else
#define DEBUG_LOG(msg)
#define DEBUG_FUNCTION()
#endif

// 에러 처리 매크로
#define SAFE_DELETE(ptr) { if(ptr) { delete ptr; ptr = nullptr; } }
#define SAFE_DELETE_ARRAY(ptr) { if(ptr) { delete[] ptr; ptr = nullptr; } }

// 상수 정의
namespace ForensicConstants {
const int DEFAULT_BUFFER_SIZE = 8192;
const int DEFAULT_TIMEOUT_MS = 30000;
const int HEARTBEAT_INTERVAL_MS = 30000;
const int TASK_CHECK_INTERVAL_MS = 5000;

const char* const DEFAULT_SERVER_PORT = "8443";
const char* const DEFAULT_DB_PORT = "5432";
const char* const CONFIG_FILE_NAME = "forensic_agent_config.ini";
const char* const LOG_FILE_NAME = "forensic_agent.log";
}

// 🔥 포렌식 메시지 타입 정의 (Agent 호환)
enum MessageType {
    MSG_CLIENT_REGISTER = 1,     // 클라이언트 등록
    MSG_CLIENT_HEARTBEAT = 2,    // 생존 신호
    MSG_TASK_REQUEST = 3,        // 작업 요청
    MSG_TASK_RESPONSE = 4,       // 작업 결과
    MSG_FORENSIC_DATA = 5,       // 포렌식 데이터 ⭐
    MSG_STATUS_REPORT = 6,       // 상태 보고
    MSG_DISCONNECT = 7           // 연결 종료
};

// 🔥 포렌식 데이터 구조체 (Agent 호환)
struct ForensicData {
    QString moduleType;        // "USB_DATA", "PREFETCH_DATA", "BROWSER_DATA" 등
    QString dataFormat;        // "JSON" (기본값)
    QByteArray payload;        // JSON 데이터 (Base64 인코딩)
    QString checksum;          // SHA256 체크섬
    QString taskId;            // 작업 ID 추가

    ForensicData() : dataFormat("JSON") {}
};

Q_DECLARE_METATYPE(ForensicData)

// 🔥 메시지 구조체 (Agent 호환)
template<typename T>
struct Message {
    MessageType type;           // 메시지 타입
    uint32_t dataSize;         // 데이터 크기
    QString clientId;          // 클라이언트 UUID
    QDateTime timestamp;       // 타임스탬프
    T data;                    // 실제 데이터

    Message() : type(MSG_CLIENT_REGISTER), dataSize(0) {
        timestamp = QDateTime::currentDateTime();
    }
};

#endif // PCH_H
