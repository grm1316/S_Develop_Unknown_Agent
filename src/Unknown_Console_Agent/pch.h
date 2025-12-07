#ifndef PCH_H
#define PCH_H

// 표준 C++ 헤더
#include <iostream>
#include <string>
#include <fstream>
#include <cstdint>
#include <vector>
#include <list>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <exception>
#include <algorithm>
#include <memory>
#include <atomic>
#include <cstring>

// Windows API 헤더
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <winevt.h>
#include <winnt.h>
#include <winreg.h>
#include <shlobj.h>
#include <shellapi.h>

// Qt Core 헤더
#include <QtCore>
#include <QObject>
#include <QDebug>
#include <QXmlStreamReader>
#include <QString>
#include <QVector>
#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>
#include <QDateTime>
#include <QTime>
#include <QFile>
#include <QTextStream>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSettings>
#include <QTemporaryFile>
#include <QUuid>
#include <QSslSocket>

// Qt 파일시스템 및 유틸리티 헤더
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QSet>
#include <QByteArray>
#include <QHash>

// Qt 스레딩 및 동시성 헤더
#include <QThread>
#include <QThreadPool>
#include <QRunnable>
#include <QReadWriteLock>
#include <QMutex>
#include <QFuture>
#include <QFutureWatcher>

// Qt Network 헤더 (네트워크 기능용)
#include <QHostInfo>
#include <QNetworkInterface>
#include <QTcpSocket>
#include <QUdpSocket>

// Qt Concurrent 모듈
#if QT_VERSION >= QT_VERSION_CHECK(5, 0, 0)
#include <QtConcurrent/QtConcurrent>
#else
#include <QtCore/QtConcurrentRun>
#endif

// 프로젝트 헤더
#include "Ip.h"
#include "sqlite3.h"
#include "clientutils.h"
//#include "sqlite/sqlite3ext.h"

// 라이브러리 링크
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "wevtapi.lib")
#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "version.lib")
#pragma comment(lib, "ntdll.lib")

//json 형태
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>

// Qt SQL 헤더들 (pch.h에 없는 경우 명시적 include)
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QSqlResult>

// 기타 필요한 헤더들
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>
#include <QNetworkInterface>
#include <QOperatingSystemVersion>
#include <QJsonDocument>
#include <QDir>
#include <QFile>

#endif // PCH_H

