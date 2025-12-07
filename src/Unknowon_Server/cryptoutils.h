// CryptoUtils.h - AES-256 암호화/복호화 유틸리티
#ifndef CRYPTOUTILS_H
#define CRYPTOUTILS_H

#include "pch.h"
#include <QString>
#include <QByteArray>
#include <windows.h>
#include <wincrypt.h>

// =================================================================
// CryptoUtils - Windows CryptoAPI 기반 AES-256 암호화
// =================================================================

class CryptoUtils {
public:
    // AES-256-CBC 암호화
    static QByteArray encryptAES256(const QByteArray& plaintext, const QString& password);

    // AES-256-CBC 복호화
    static QByteArray decryptAES256(const QByteArray& ciphertext, const QString& password);

    // Base64 인코딩된 암호화 (저장용)
    static QString encryptToBase64(const QString& plaintext, const QString& password);

    // Base64 디코딩 후 복호화 (조회용)
    static QString decryptFromBase64(const QString& base64Ciphertext, const QString& password);

private:
    // 비밀번호에서 AES 키 생성 (SHA-256 해시)
    static QByteArray deriveKey(const QString& password);

    // PKCS7 패딩 추가
    static QByteArray addPadding(const QByteArray& data, int blockSize);

    // PKCS7 패딩 제거
    static QByteArray removePadding(const QByteArray& data);
};

#endif // CRYPTOUTILS_H
