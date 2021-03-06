#ifndef LFXE_EXPORTS
#define LFXE_EXPORTS
#endif

#include "Log.h"

// Standard includes
#include <atomic>
#include <chrono>
#include <codecvt>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>
#include <queue>

// Windows includes
#include "../Common/Windows.h"
#include <Windows.h>
#include <time.h>

// Project includes
#include "../LightFXExtender.h"
#include "FileIO.h"
#include "NotifyEvent.h"
#include "String.h"


using namespace std;

namespace lightfx {
    namespace utils {

        wstring logFileName = L"LightFXExtender.log";
        wstring logDirectory = GetDataStorageFolder();
#ifdef LFXE_TESTING
        LogLevel minimumLogLevel = LogLevel::Off;
#else
        LogLevel minimumLogLevel = LogLevel::Info;
#endif
        mutex logMutex;

        bool loggerWorkerActive = false;
        atomic<bool> stopLoggerWorker = false;
        thread loggerWorkerThread;
        queue<LogMessage> logQueue;
        mutex logQueueMutex;

        NotifyEvent notifyEvent;


        LFXE_API void Log::StartLoggerWorker() {
            if (!loggerWorkerActive) {
                loggerWorkerActive = true;
                stopLoggerWorker = false;
                loggerWorkerThread = thread(&LoggerWorker);
            }
        }

        LFXE_API void Log::StopLoggerWorker() {
            if (loggerWorkerActive) {
                stopLoggerWorker = true;
                notifyEvent.Notify();
                if (loggerWorkerThread.joinable()) {
                    loggerWorkerThread.join();
                }
                loggerWorkerActive = false;
            }
        }

        wstring GetLastWindowsError() {
            wstring message;

            //Get the error message, if any.
            DWORD errorMessageID = GetLastError();
            if (errorMessageID == 0) {
                LOG_DEBUG(L"No Windows error found");
            }

            LPWSTR messageBuffer = nullptr;
            size_t size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL, errorMessageID, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), (LPWSTR)&messageBuffer, 0, NULL);

            message = wstring(messageBuffer, size);

            //Free the buffer.
            LocalFree(messageBuffer);

            return L"Windows error: " + message;
        }

        LFXE_API void Log::LogLine(const LogLevel logLevel, const string& file, const int line, const string& function, const wstring& message) {
            if (minimumLogLevel > logLevel) {
                return;
            }
            LogMessage log = {
                logLevel,
                string_to_wstring(file),
                line,
                string_to_wstring(function),
                message,
                chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch())
            };
            wstring logLine = GetLine(log);

            try {
                logMutex.lock();
                wofstream logStream = OpenStream();
                logStream << logLine << endl;
                logStream.close();
                logMutex.unlock();
            } catch (...) {
                // Can't log the exception, since we just failed to log something else...
            }
        }

        LFXE_API void Log::LogLastWindowsError(const string& file, const int line, const string& function) {
            if (minimumLogLevel > LogLevel::Error) {
                return;
            }
            LogLine(LogLevel::Error, file, line, function, GetLastWindowsError());
        }

        LFXE_API void Log::LogLineAsync(const LogLevel logLevel, const string& file, const int line, const string& function, const wstring& message) {
            if (minimumLogLevel > logLevel) {
                return;
            }
            LogMessage log = {
                logLevel,
                string_to_wstring(file),
                line,
                string_to_wstring(function),
                message,
                chrono::duration_cast<chrono::milliseconds>(chrono::system_clock::now().time_since_epoch())
            };

            {
                lock_guard<mutex> lock(logQueueMutex);
                logQueue.emplace(log);
            }
            notifyEvent.Notify();
        }

        LFXE_API void Log::LogLastWindowsErrorAsync(const string& file, const int line, const string& function) {
            if (minimumLogLevel > LogLevel::Error) {
                return;
            }
            LogLineAsync(LogLevel::Error, file, line, function, GetLastWindowsError());
        }


        LFXE_API void Log::RotateLog() {
            wstring filePath = logDirectory;
            if (DirExists(filePath)) {
                filePath += L"/" + logFileName;
                if (FileExists(filePath)) {
                    for (int i = 3; i >= 0; --i) {
                        wstring filePathOld = filePath + L"." + to_wstring(i);
                        wstring filePathNew = filePath + L"." + to_wstring(i + 1);
                        if (FileExists(filePathOld)) {
                            if (i == 3) {
                                DeleteFileW(filePathOld.c_str());
                            } else {
                                MoveFileW(filePathOld.c_str(), filePathNew.c_str());
                            }
                        }
                    }
                    MoveFileW(filePath.c_str(), (filePath + L".0").c_str());
                }
            }
        }


        LFXE_API const wstring Log::GetLogFileName() {
            return logFileName;
        }

        LFXE_API const wstring Log::GetLogDirectory() {
            return logDirectory;
        }

        LFXE_API void Log::SetLogDirectory(const wstring& directory) {
            logDirectory = directory;
        }


        LFXE_API LogLevel Log::GetMinimumLogLevel() {
            return minimumLogLevel;
        }

        LFXE_API void Log::SetMinimumLogLevel(const LogLevel logLevel) {
            minimumLogLevel = logLevel;
        }


        LFXE_API void Log::LoggerWorker() {
            while (true) {
                notifyEvent.Wait();
                if (stopLoggerWorker) {
                    break;
                }
                WriteBacklog();
            }
        }

        LFXE_API wofstream Log::OpenStream() {
            wstring filePath = logDirectory;
            if (!DirExists(filePath)) {
                if (CreateDirectoryW(filePath.c_str(), NULL) == FALSE) {
                    throw exception("Failed to create directory");
                }
            }
            filePath += L"/" + logFileName;
            wofstream logStream;
            logStream.imbue(locale(logStream.getloc(), new codecvt_utf8<wchar_t>));
            logStream.open(filePath, wios::out | wios::binary | wios::app);
            return logStream;
        }

        LFXE_API wstring Log::GetLine(const LogMessage& message) {
            // Get a nice date/time prefix first
            wchar_t buff[20];
            time_t t = chrono::duration_cast<chrono::seconds>(message.time).count();
            tm lt;
            localtime_s(&lt, &t);
            wcsftime(buff, 20, L"%Y-%m-%d %H:%M:%S", &lt);
            wstring timePrefix(buff);
            swprintf_s(buff, 4, L"%03ld", message.time.count() % 1000);
            timePrefix += L"." + wstring(buff);

            // Determine the log level prefix
            wstring logLevelPrefix;
            switch (message.level) {
            case LogLevel::Debug:
                logLevelPrefix = L"[DEBUG]";
                break;
            case LogLevel::Info:
                logLevelPrefix = L"[INFO]";
                break;
            case LogLevel::Warning:
                logLevelPrefix = L"[WARNING]";
                break;
            case LogLevel::Error:
                logLevelPrefix = L"[ERROR]";
                break;
            }

            return timePrefix + L" - " + logLevelPrefix + L" " + message.file + L":" + to_wstring(message.line) + L" (" + message.function + L") - " + message.message;
        }

        LFXE_API void Log::WriteBacklog() {
            try {
                logMutex.lock();
                wofstream logStream = OpenStream();

                while (true) {
                    LogMessage message;
                    {
                        lock_guard<mutex> lock(logQueueMutex);
                        if (logQueue.size() > 0) {
                            message = logQueue.front();
                            logQueue.pop();
                        } else {
                            break;
                        }
                    }

                    wstring logLine = GetLine(message);
                    logStream << logLine << endl;
                }

                logStream.close();
                logMutex.unlock();
            } catch (...) {
                // Can't log the exception, since we just failed to log something else...
            }
        }

   }
}
