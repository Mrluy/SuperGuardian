#include "ProcessUtils.h"
#include "AppStorage.h"
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
#include <QSettings>

QString resolveShortcut(const QString& path) {
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

bool launchProgram(const QString& path) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOASYNC;
    sei.hwnd = nullptr;
    sei.lpVerb = L"open";
    std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
    sei.lpFile = wpath.c_str();
    sei.nShow = SW_SHOWNORMAL;
    BOOL ok = ShellExecuteExW(&sei);
    if (!ok) appendWatchdogLog(QString("launch guarded app failed: %1").arg(path), GetLastError());
    return ok == TRUE;
}

void setAutostart(bool enable) {
    HKEY hKey;
    LPCWSTR runKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, runKey, 0, KEY_WRITE, &hKey) != ERROR_SUCCESS) return;
    wchar_t exePath[MAX_PATH];
    wcscpy_s(exePath, (wchar_t*)QCoreApplication::applicationFilePath().utf16());
    if (enable) {
        RegSetValueExW(hKey, L"SuperGuardian", 0, REG_SZ, (const BYTE*)exePath, (DWORD)((wcslen(exePath)+1)*sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, L"SuperGuardian");
    }
    RegCloseKey(hKey);
}

// --- Watchdog executable helpers ---

static QString localWatchdogExecutablePath() {
    return QDir(appCacheDirPath()).filePath("SuperGuardianWatchdog.exe");
}

static QString ensureLocalWatchdogExecutable() {
    QString target = localWatchdogExecutablePath();
    QString source = QCoreApplication::applicationFilePath();
    if (target.isEmpty() || source.isEmpty()) return source;

    QFileInfo srcInfo(source);
    QFileInfo dstInfo(target);
    if (!dstInfo.exists() || srcInfo.lastModified() > dstInfo.lastModified() || srcInfo.size() != dstInfo.size()) {
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            appendWatchdogLog(QString("copy watchdog helper failed: %1 -> %2").arg(source, target));
            return source;
        }
    }
    return target;
}

QString outputWatchdogExecutablePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath("SuperGuardianWatchdog.exe");
}

QString ensureOutputWatchdogExecutable() {
    QString target = outputWatchdogExecutablePath();
    QString source = QCoreApplication::applicationFilePath();
    if (target.isEmpty() || source.isEmpty()) return source;

    QFileInfo srcInfo(source);
    QFileInfo dstInfo(target);
    if (!dstInfo.exists() || srcInfo.lastModified() > dstInfo.lastModified() || srcInfo.size() != dstInfo.size()) {
        QFile::remove(target);
        if (!QFile::copy(source, target)) {
            appendWatchdogLog(QString("copy output watchdog failed: %1 -> %2").arg(source, target));
            return ensureLocalWatchdogExecutable();
        }
    }
    return target;
}

// --- Watchdog mode ---

int runWatchdogMode(int argc, char* argv[]) {
    if (argc < 4) return 0;
    DWORD parentPid = (DWORD)QString::fromUtf8(argv[2]).toUInt();
    QString parentExe = QString::fromUtf8(argv[3]);
    DWORD selfPid = GetCurrentProcessId();
    appendWatchdogLog(QString("watchdog started self=%1 main=%2 exe=%3").arg(selfPid).arg(parentPid).arg(parentExe));

    const QString workingDir = QFileInfo(parentExe).absolutePath();

    while (true) {
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, parentPid);
        if (!h) {
            appendWatchdogLog("open main process failed, trying restart", GetLastError());
        } else {
            DWORD waitRc = WaitForSingleObject(h, 1000);
            CloseHandle(h);
            if (waitRc == WAIT_TIMEOUT) {
                continue;
            }
        }

        QSettings s(appSettingsFilePath(), QSettings::IniFormat);
        bool enabled = s.value("self_guard_enabled", false).toBool();
        bool manualExit = s.value("self_guard_manual_exit", false).toBool();
        if (!enabled || manualExit) break;

        bool restarted = false;
        for (int attempt = 1; attempt <= 5; ++attempt) {
            qint64 newPid = 0;
            bool ok = QProcess::startDetached(parentExe, {}, workingDir, &newPid);
            if (ok && newPid > 0) {
                parentPid = static_cast<DWORD>(newPid);
                appendWatchdogLog(QString("main restarted pid=%1 attempt=%2").arg(parentPid).arg(attempt));
                Sleep(1500);
                restarted = true;
                break;
            }

            appendWatchdogLog(QString("main restart attempt failed attempt=%1").arg(attempt), GetLastError());
            Sleep(1000 * attempt);
        }

        if (!restarted) {
            appendWatchdogLog("main restart failed permanently", GetLastError());
            break;
        }
    }
    appendWatchdogLog(QString("watchdog exit self=%1").arg(selfPid));
    return 0;
}
