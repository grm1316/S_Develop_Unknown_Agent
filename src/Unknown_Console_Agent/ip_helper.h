#ifndef IP_HELPER_H
#define IP_HELPER_H

#include "pch.h"

class IPHelper {
public:
    /**
     * @brief 서버 연결을 위한 최적의 IP 주소를 찾습니다
     * @return 연결 가능한 서버 IP 주소 (실패시 빈 문자열)
     */
    static QString findServerIP() {
        // 고정된 우선순위로 IP 검색 (자동 감지 비활성화)
        QStringList candidateIPs = {
            "13.124.25.47",  // 실제 탄력적 IP로 변경
            "15.165.12.47",          // 기존 EC2 서버 IP (백업)
            "192.168.0.100",         // 로컬 네트워크 (테스트용)
            "192.168.1.100",         // 또 다른 로컬 네트워크
            "10.0.2.2",              // VirtualBox NAT
            "192.168.1.1",           // 게이트웨이
            "192.168.0.1",           // 또 다른 게이트웨이
            "127.0.0.1"              // 로컬호스트
        };

        // 자동 감지 기능을 주석 처리하여 비활성화
        /*
    QString detectedIP = getHostIPFromNetwork();
    if (!detectedIP.isEmpty()) {
        candidateIPs.prepend(detectedIP);
    }
    */

        qDebug() << "🔍 서버 IP 주소 검색 중...";
        qDebug() << "📋 고정 순서 검색:" << candidateIPs;

        for (const QString& ip : candidateIPs) {
            if (testConnection(ip, 8443)) {
                qDebug() << QString("✅ 서버 발견: %1:8443").arg(ip);
                return ip;
            }
        }

        qDebug() << "❌ 연결 가능한 서버를 찾을 수 없습니다.";
        return QString();
    }

    /**
     * @brief 지정된 IP와 포트로 연결 테스트
     */
    static bool testConnection(const QString& ip, int port) {
        qDebug() << QString("   🔍 %1:%2 연결 테스트...").arg(ip).arg(port);

        // 간단한 소켓 연결 테스트
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        SOCKET testSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (testSocket == INVALID_SOCKET) {
            WSACleanup();
            return false;
        }

        sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, ip.toLocal8Bit().constData(), &serverAddr.sin_addr);

        // 타임아웃 설정 (1초)
        DWORD timeout = 1000;
        setsockopt(testSocket, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
        setsockopt(testSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout, sizeof(timeout));

        bool success = (connect(testSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == 0);

        closesocket(testSocket);
        WSACleanup();

        return success;
#else
#endif
    }

    /**
     * @brief 네트워크에서 실제 호스트 IP를 감지 시도
     */
    static QString getHostIPFromNetwork() {
        // 기본 게이트웨이 정보에서 호스트 IP 추정
        QProcess process;

#ifdef _WIN32
        // Windows: route print에서 게이트웨이 정보 추출
        process.start("route", QStringList() << "print" << "0.0.0.0");
        process.waitForFinished(3000);

        QString output = process.readAllStandardOutput();
        QStringList lines = output.split('\n');

        for (const QString& line : lines) {
            if (line.contains("0.0.0.0") && line.contains("0.0.0.0")) {
                QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    QString gateway = parts[2];
                    // 게이트웨이에서 호스트 IP 추정 (마지막 옥텟을 100으로 변경)
                    QStringList ipParts = gateway.split('.');
                    if (ipParts.size() == 4) {
                        ipParts[3] = "100";  // 일반적인 호스트 IP
                        return ipParts.join('.');
                    }
                }
            }
        }
#endif

        return QString();  // 감지 실패
    }

    /**
     * @brief main.cpp에서 사용할 C 스타일 문자열 반환
     */
    static const char* getServerIPAsCString() {
        static QString serverIP = findServerIP();
        if (serverIP.isEmpty()) {
            return "13.124.25.47";  // 기본값
        }
        return serverIP.toLocal8Bit().constData();
    }
};

#endif // IP_HELPER_H
