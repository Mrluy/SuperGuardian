#include "ProcessUtils.h"
#include "AppStorage.h"
#include "LogDatabase.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include "ConfigDatabase.h"

using namespace Qt::Literals::StringLiterals;

QString resolveShortcut(const QString& path, QString* outArgs) {
    if (!path.endsWith(".lnk", Qt::CaseInsensitive)) return path;

    CoInitialize(nullptr);
    IShellLink* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLink, (void**)&psl);
    if (SUCCEEDED(hr) && psl) {
        IPersistFile* ppf = nullptr;
        hr = psl->QueryInterface(IID_IPersistFile, (void**)&ppf);
        if (SUCCEEDED(hr) && ppf) {
            wchar_t wszPath[MAX_PATH];
            path.toWCharArray(wszPath);
            wszPath[path.length()] = 0;
            hr = ppf->Load(wszPath, STGM_READ);
            if (SUCCEEDED(hr)) {
                wchar_t wszTarget[MAX_PATH];
                WIN32_FIND_DATAW wfd;
                hr = psl->GetPath(wszTarget, MAX_PATH, &wfd, SLGP_RAWPATH);
                if (SUCCEEDED(hr)) {
                    QString target = QString::fromWCharArray(wszTarget);
                    if (outArgs) {
                        wchar_t wszArgs[1024];
                        HRESULT argsHr = psl->GetArguments(wszArgs, 1024);
                        if (SUCCEEDED(argsHr))
                            *outArgs = QString::fromWCharArray(wszArgs).trimmed();
                    }
                    ppf->Release();
                    psl->Release();
                    CoUninitialize();
                    return target;
                }
            }
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();
    return path;
}

void killProcessesByName(const QString& name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            QString exe = QString::fromWCharArray(pe.szExeFile);
            if (exe.compare(name, Qt::CaseInsensitive) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) {
                    TerminateProcess(h, 1);
                    CloseHandle(h);
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
}

QIcon getFileIcon(const QString& path) {
    QFileIconProvider provider;
    QFileInfo fi(path);
    return provider.icon(fi);
}

bool isProcessRunning(const QString& name, int& count) {
    count = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snap, &pe)) {
        do {
            QString exe = QString::fromWCharArray(pe.szExeFile);
            if (exe.compare(name, Qt::CaseInsensitive) == 0) count++;
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return count > 0;
}

QDateTime getProcessStartTime(const QString& processName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return QDateTime();
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            QString exe = QString::fromWCharArray(pe.szExeFile);
            if (exe.compare(processName, Qt::CaseInsensitive) == 0) {
                HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                if (h) {
                    FILETIME ftCreate, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        CloseHandle(h);
                        CloseHandle(snap);
                        ULARGE_INTEGER ull;
                        ull.LowPart = ftCreate.dwLowDateTime;
                        ull.HighPart = ftCreate.dwHighDateTime;
                        qint64 epoch = (qint64)(ull.QuadPart / 10000000ULL - 11644473600ULL);
                        return QDateTime::fromSecsSinceEpoch(epoch);
                    }
                    CloseHandle(h);
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return QDateTime();
}

bool launchProgram(const QString& path, const QString& args) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = nullptr;
    sei.lpVerb = L"open";
    std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    sei.lpFile = wpath.c_str();
    std::wstring wargs;
    if (!args.isEmpty()) {
        wargs = args.toStdWString();
        sei.lpParameters = wargs.c_str();
    }
    std::wstring wdir = QDir::toNativeSeparators(QFileInfo(path).absolutePath()).toStdWString();
    sei.lpDirectory = wdir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    BOOL ok = ShellExecuteExW(&sei);
    if (!ok) logRuntime(QString("launch guarded app failed: %1 (err=%2)").arg(path).arg(GetLastError()));
    return ok == TRUE;
}

void setAutostart(bool enable) {
    HKEY hKey;
    LPCWSTR runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    // 清理旧版本启动项名称
    RegDeleteValueW(hKey, L"SuperGuardian");
    if (enable) {
        QString appPath = QCoreApplication::applicationFilePath();
        QString quoted = QString("\"%1\"").arg(QDir::toNativeSeparators(appPath));
        const wchar_t* wpath = (const wchar_t*)quoted.utf16();
        RegSetValueExW(hKey, L"\x8d85\x7ea7\x5b88\x62a4", 0, REG_SZ, (const BYTE*)wpath, (DWORD)((wcslen(wpath)+1)*sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"\x8d85\x7ea7\x5b88\x62a4");
    }
    RegCloseKey(hKey);
}

// --- Watchdog mode ---

int runWatchdogMode(int argc, char* argv[]) {
    Q_UNUSED(argc);
    Q_UNUSED(argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 4) return 0;

    DWORD parentPid = static_cast<DWORD>(args[2].toUInt());
    QString parentExe = QDir::fromNativeSeparators(args[3]);
    DWORD selfPid = GetCurrentProcessId();
    logRuntime(QString("watchdog started self=%1 main=%2 exe=%3").arg(selfPid).arg(parentPid).arg(parentExe));

    const QString workingDir = QFileInfo(parentExe).absolutePath();

    while (true) {
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, parentPid);
        if (!h) {
            logRuntime(QString("open main process failed, trying restart (err=%1)").arg(GetLastError()));
        } else {
            DWORD waitRc = WaitForSingleObject(h, 1000);
            CloseHandle(h);
            if (waitRc == WAIT_TIMEOUT) {
                continue;
            }
        }

        auto& db = ConfigDatabase::instance();
        bool enabled = db.value(u"self_guard_enabled"_s, false).toBool();
        bool manualExit = db.value(u"self_guard_manual_exit"_s, false).toBool();
        if (!enabled || manualExit) break;

        bool restarted = false;
        for (int attempt = 1; attempt <= 5; ++attempt) {
            qint64 newPid = 0;
            bool ok = QProcess::startDetached(parentExe, { "--restart" }, workingDir, &newPid);
            if (ok && newPid > 0) {
                parentPid = static_cast<DWORD>(newPid);
                logRuntime(QString("main restarted pid=%1 attempt=%2").arg(parentPid).arg(attempt));
                Sleep(1500);
                restarted = true;
                break;
            }

            logRuntime(QString("main restart attempt failed attempt=%1 (err=%2)").arg(attempt).arg(GetLastError()));
            Sleep(1000 * attempt);
        }

        if (!restarted) {
            logRuntime(QString("main restart failed permanently (err=%1)").arg(GetLastError()));
            break;
        }
    }
    logRuntime(QString("watchdog exit self=%1").arg(selfPid));
    return 0;
}
