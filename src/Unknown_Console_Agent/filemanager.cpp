#include "pch.h"
#include "filemanager.h"

using namespace std;

FileManager::FileManager(QObject *parent)
    : QObject{parent} {
    //if(!init()) throw runtime_error("Failed to create file manager.");
}

//FileManager::~FileManager() {
//}

void FileManager::WarningMsg(const std::string msg) const {
    //qDebug() << "[FileManager] " << QString::fromStdString(msg);
}

bool FileManager::init() {
    try {
        tmpDirPath_ = QDir::tempPath() + "\\" + FOLDER_NAME;
        tmpDirPath_.replace("/", "\\");

        if(tmpDirPath_.isEmpty()) throw runtime_error("Failed to get temp path.");

        QDir tmpDir = QDir(tmpDirPath_);
        if(!tmpDir.exists()) QDir().mkpath(tmpDirPath_);

    }catch(const exception& e) {
        //WarningMsg(string("<init> ").append(e.what()));
        return false;
    }


    return true;
}

void FileManager::CreateTempFile(const string& path) {

    if(tmpFileHandles_.count(path)) {
        //WarningMsg("<GenerateTempFile> Aleady exist temp file.\
        The existing file will be deleted and recreated.");

        tmpFileHandles_.erase(path);
    }

    tmpFileHandles_[path] = make_unique<QTemporaryFile>(tmpDirPath_ + "\\XXXXXX.tmp");

    tmpFileHandles_[path].get()->open();
    tmpFileHandles_[path].get()->close();

    //std::this_thread::sleep_for(std::chrono::seconds(3));
}

string FileManager::VSScopy(const std::string srcPath, std::string dstPath) {
    try {
        if(dstPath.empty())
            dstPath = (tmpDirPath_ + "\\" + QUuid::createUuid().toString()).toStdString();

        std::string command(1024, 0);

        command.resize(sprintf(command.data(), VssCommand, srcPath.c_str(), dstPath.c_str()));

        system(command.c_str());

        std::this_thread::sleep_for(std::chrono::milliseconds(500));

    }catch(const exception& e) {
        //WarningMsg(string("<VSScopy> ").append(e.what()));
        return {};
    }

    return dstPath;
}

QString FileManager::copy(const QString srcPath, QString dstPath) const {
    try {
        if(dstPath == nullptr) {
            // string srcPathStr = srcPath.toStdString();

            // CreateTempFile(srcPathStr);

            // if(!tmpFileHandles_.count(srcPathStr))
            //     throw runtime_error("Failed to create temporary file handle.");

            // dstPath = tmpFileHandles_[srcPathStr].get()->fileName();

            dstPath = tmpDirPath_ + "\\" + QUuid::createUuid().toString();
        }

        //if(!QFile::copy(srcPath, dstPath)) // same name fail
           // throw runtime_error("Failed to copy");
    }catch(const exception& e) {
        //WarningMsg(string("<copy> ").append(e.what()));
        return {};
    }

    return dstPath;
}

vector<uint8_t> FileManager::read(const string& path, const FileType& type) const{
    try {
        ifstream file{};

        switch(type) {
        case BINARY:
            file.open(path, ios::binary);
            break;
        default:
            break;
        }

        if(!file.is_open())
            //throw runtime_error("Failed to open file.");

        file.seekg(0, ios::end);
        size_t size = file.tellg();
        file.seekg(0, ios::beg);


        vector<uint8_t> buffer(size, 0);

        if(!file.read(reinterpret_cast<char*>(buffer.data()), buffer.size()))
            //throw runtime_error("Failed to read file.");

        return buffer;

    }catch(const exception& e) {
        //WarningMsg(string("<read> ").append(e.what()));
        return {};
    }
}

vector<string> FileManager::GetFilePathsInFolder(const string& path) {
    vector<string> filePaths;

    try {
        for (const auto& entry : filesystem::directory_iterator(path)) {
            if (entry.is_regular_file()) {
                filePaths.push_back(entry.path().string());
            }
        }

        return filePaths;
    } catch (const exception& e) {
        //WarningMsg(string("<GetFilePathsInFolder> ").append(e.what()));
        return {};
    }
}
