// CryptoUtils.cpp - AES-256 암호화 구현
#include "CryptoUtils.h"
#include <QCryptographicHash>
#include <QDebug>

#pragma comment(lib, "advapi32.lib")

// =================================================================
// 비밀번호에서 AES-256 키 생성 (SHA-256 해시 사용)
// =================================================================
QByteArray CryptoUtils::deriveKey(const QString& password) {
    if (password.isEmpty()) {
        qWarning() << "[CryptoUtils] 암호화 키가 비어있음";
        return QByteArray();
    }

    // SHA-256 해시로 256비트(32바이트) 키 생성
    QByteArray hash = QCryptographicHash::hash(
        password.toUtf8(),
        QCryptographicHash::Sha256
        );

    return hash;
}

// =================================================================
// PKCS7 패딩 추가
// =================================================================
QByteArray CryptoUtils::addPadding(const QByteArray& data, int blockSize) {
    int paddingSize = blockSize - (data.size() % blockSize);
    QByteArray padded = data;
    padded.append(QByteArray(paddingSize, static_cast<char>(paddingSize)));
    return padded;
}

// =================================================================
// PKCS7 패딩 제거
// =================================================================
QByteArray CryptoUtils::removePadding(const QByteArray& data) {
    if (data.isEmpty()) {
        return data;
    }

    int paddingSize = static_cast<unsigned char>(data[data.size() - 1]);

    if (paddingSize > 0 && paddingSize <= 16 && paddingSize <= data.size()) {
        return data.left(data.size() - paddingSize);
    }

    return data;
}

// =================================================================
// AES-256-CBC 암호화 (Windows CryptoAPI)
// =================================================================
// =================================================================
// AES-256-CBC 암호화 (Windows CryptoAPI)
// =================================================================
QByteArray CryptoUtils::encryptAES256(const QByteArray& plaintext, const QString& password) {
    if (plaintext.isEmpty() || password.isEmpty()) {
        qWarning() << "[CryptoUtils] 암호화 실패: 입력 데이터가 비어있음";
        return QByteArray();
    }

    // 1. 키 생성
    QByteArray key = deriveKey(password);
    if (key.isEmpty()) {
        return QByteArray();
    }

    // 2. IV 생성 (16바이트 랜덤)
    QByteArray iv(16, 0);
    HCRYPTPROV hProv = 0;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProv, 16, reinterpret_cast<BYTE*>(iv.data()));
        CryptReleaseContext(hProv, 0);
    }

    // 3. 패딩 추가
    QByteArray paddedData = addPadding(plaintext, 16);

    // 4. Windows CryptoAPI로 암호화
    HCRYPTPROV hCryptProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    QByteArray ciphertext;

    try {
        // Provider 획득
        if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            throw std::runtime_error("CryptAcquireContext 실패");
        }

        // 해시 생성
        if (!CryptCreateHash(hCryptProv, CALG_SHA_256, 0, 0, &hHash)) {
            throw std::runtime_error("CryptCreateHash 실패");
        }

        // 키 해시
        if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(key.constData()), key.size(), 0)) {
            throw std::runtime_error("CryptHashData 실패");
        }

        // 키 생성
        if (!CryptDeriveKey(hCryptProv, CALG_AES_256, hHash, 0, &hKey)) {
            throw std::runtime_error("CryptDeriveKey 실패");
        }

        // IV 설정
        if (!CryptSetKeyParam(hKey, KP_IV, reinterpret_cast<const BYTE*>(iv.constData()), 0)) {
            throw std::runtime_error("CryptSetKeyParam 실패");
        }

        // 🔧 수정: 버퍼 크기를 충분히 크게 설정 (패딩 + 블록 크기 추가)
        DWORD dataLen = paddedData.size();
        DWORD bufferLen = dataLen + 16;  // 추가 블록 크기 확보
        ciphertext.resize(bufferLen);
        memcpy(ciphertext.data(), paddedData.constData(), dataLen);

        // 암호화 수행
        if (!CryptEncrypt(hKey, 0, TRUE, 0, reinterpret_cast<BYTE*>(ciphertext.data()), &dataLen, bufferLen)) {
            DWORD errorCode = GetLastError();
            throw std::runtime_error(QString("CryptEncrypt 실패 (Error: %1)").arg(errorCode).toStdString());
        }

        // 실제 암호화된 데이터 크기로 조정
        ciphertext.resize(dataLen);

        // IV + 암호문 결합
        QByteArray result = iv + ciphertext;

        // 정리
        if (hKey) CryptDestroyKey(hKey);
        if (hHash) CryptDestroyHash(hHash);
        if (hCryptProv) CryptReleaseContext(hCryptProv, 0);

        qDebug() << "[CryptoUtils] 암호화 성공 - 원본:" << plaintext.size()
                 << "바이트, 암호화:" << result.size() << "바이트";
        return result;

    } catch (const std::exception& e) {
        qCritical() << "[CryptoUtils] 암호화 중 오류:" << e.what();

        // 정리
        if (hKey) CryptDestroyKey(hKey);
        if (hHash) CryptDestroyHash(hHash);
        if (hCryptProv) CryptReleaseContext(hCryptProv, 0);

        return QByteArray();
    }
}

// =================================================================
// AES-256-CBC 복호화 (Windows CryptoAPI)
// =================================================================
QByteArray CryptoUtils::decryptAES256(const QByteArray& ciphertext, const QString& password) {
    if (ciphertext.size() < 16 || password.isEmpty()) {
        qWarning() << "[CryptoUtils] 복호화 실패: 입력 데이터가 유효하지 않음";
        return QByteArray();
    }

    // 1. 키 생성
    QByteArray key = deriveKey(password);
    if (key.isEmpty()) {
        return QByteArray();
    }

    // 2. IV 추출 (첫 16바이트)
    QByteArray iv = ciphertext.left(16);
    QByteArray encrypted = ciphertext.mid(16);

    // 3. Windows CryptoAPI로 복호화
    HCRYPTPROV hCryptProv = 0;
    HCRYPTKEY hKey = 0;
    HCRYPTHASH hHash = 0;
    QByteArray plaintext;

    try {
        // Provider 획득
        if (!CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
            throw std::runtime_error("CryptAcquireContext 실패");
        }

        // 해시 생성
        if (!CryptCreateHash(hCryptProv, CALG_SHA_256, 0, 0, &hHash)) {
            throw std::runtime_error("CryptCreateHash 실패");
        }

        // 키 해시
        if (!CryptHashData(hHash, reinterpret_cast<const BYTE*>(key.constData()), key.size(), 0)) {
            throw std::runtime_error("CryptHashData 실패");
        }

        // 키 생성
        if (!CryptDeriveKey(hCryptProv, CALG_AES_256, hHash, 0, &hKey)) {
            throw std::runtime_error("CryptDeriveKey 실패");
        }

        // IV 설정
        if (!CryptSetKeyParam(hKey, KP_IV, reinterpret_cast<const BYTE*>(iv.constData()), 0)) {
            throw std::runtime_error("CryptSetKeyParam 실패");
        }

        // 복호화할 데이터 복사
        plaintext = encrypted;
        DWORD dataLen = plaintext.size();

        // 복호화 수행
        if (!CryptDecrypt(hKey, 0, TRUE, 0, reinterpret_cast<BYTE*>(plaintext.data()), &dataLen)) {
            throw std::runtime_error("CryptDecrypt 실패");
        }

        // 실제 데이터 길이로 조정
        plaintext.resize(dataLen);

        // 패딩 제거
        plaintext = removePadding(plaintext);

        // 정리
        if (hKey) CryptDestroyKey(hKey);
        if (hHash) CryptDestroyHash(hHash);
        if (hCryptProv) CryptReleaseContext(hCryptProv, 0);

        return plaintext;

    } catch (const std::exception& e) {
        qCritical() << "[CryptoUtils] 복호화 중 오류:" << e.what();

        // 정리
        if (hKey) CryptDestroyKey(hKey);
        if (hHash) CryptDestroyHash(hHash);
        if (hCryptProv) CryptReleaseContext(hCryptProv, 0);

        return QByteArray();
    }
}

// =================================================================
// Base64 인코딩된 암호화 (저장용 - DatabaseManager에서 사용)
// =================================================================
QString CryptoUtils::encryptToBase64(const QString& plaintext, const QString& password) {
    if (plaintext.isEmpty() || password.isEmpty()) {
        qWarning() << "[CryptoUtils] encryptToBase64 실패: 입력이 비어있음";
        return QString();
    }

    QByteArray plaintextBytes = plaintext.toUtf8();
    QByteArray encrypted = encryptAES256(plaintextBytes, password);

    if (encrypted.isEmpty()) {
        qWarning() << "[CryptoUtils] 암호화 실패";
        return QString();
    }

    return QString::fromLatin1(encrypted.toBase64());
}

// =================================================================
// Base64 디코딩 후 복호화 (조회용 - DatabaseManager에서 사용)
// =================================================================
QString CryptoUtils::decryptFromBase64(const QString& base64Ciphertext, const QString& password) {
    if (base64Ciphertext.isEmpty() || password.isEmpty()) {
        qWarning() << "[CryptoUtils] decryptFromBase64 실패: 입력이 비어있음";
        return QString();
    }

    QByteArray ciphertext = QByteArray::fromBase64(base64Ciphertext.toLatin1());
    QByteArray decrypted = decryptAES256(ciphertext, password);

    if (decrypted.isEmpty()) {
        qWarning() << "[CryptoUtils] 복호화 실패";
        return QString();
    }

    return QString::fromUtf8(decrypted);
}
