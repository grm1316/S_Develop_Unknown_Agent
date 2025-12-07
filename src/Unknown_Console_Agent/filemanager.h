#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <QObject>

class FileManager : public QObject {
    Q_OBJECT

public:
    enum FileType {
        BINARY,
        fileTypeCount,
    };

private:
    static constexpr const char* FOLDER_NAME = "Unknown_Console_Agent";
    static constexpr const char* VssCommand =
        "powershell -Command \""
        "$shadow = (Get-WmiObject -List Win32_ShadowCopy).Create('C:\\', 'ClientAccessible');"
        "$shadowId = $shadow.ShadowID;"
        "$shadowObj = Get-WmiObject Win32_ShadowCopy | Where-Object { $_.ID -eq $shadowId };"
        "$shadowDevice = $shadowObj.DeviceObject;"
        "$sourcePathInShadow = $shadowDevice + '%s';"
        "$destinationPath = '%s';"
        "Copy-Item -LiteralPath $sourcePathInShadow -Destination $destinationPath;"
        "try { $shadowObj.Delete() | Out-Null; Write-Host 'Sucess to delete snapshot.' } catch { Write-Warning 'Failed to delete snapshot: ' + $_ }"
        "\"";

    QString tmpDirPath_{};
    std::map<std::string, std::unique_ptr<QTemporaryFile>> tmpFileHandles_{}; //path, handle

    bool init();

    void WarningMsg(const std::string msg) const;

    void CreateTempFile(const std::string& path);

protected:

public:
    explicit FileManager(QObject *parent = nullptr);
    virtual ~FileManager() = default;

    QString copy(const QString srcPath, QString dstPath = nullptr) const; //return temp file name
    std::string VSScopy(const std::string srcPath, std::string dstPath = "");
    std::vector<uint8_t> read(const std::string& path, const FileType& type) const;
    std::vector<std::string> GetFilePathsInFolder(const std::string& path);
};

using FileManagerPtr = FileManager*;

#endif // FILEMANAGER_H
