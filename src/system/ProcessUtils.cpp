#include "ProcessUtils.h"
#include "LogDatabase.h"
#include "ConfigDatabase.h"
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#include <dbghelp.h>
#include <QFileIconProvider>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <string>
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

QHash<QString, ProcessInfo> takeProcessSnapshot() {
    QHash<QString, ProcessInfo> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            QString exe = QString::fromWCharArray(pe.szExeFile).toLower();
            ProcessInfo& info = result[exe];
            info.count++;
            // 获取进程启动时间（仅首次获取）
            if (!info.startTime.isValid()) {
                HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pe.th32ProcessID);
                if (h) {
                    FILETIME ftCreate, ftExit, ftKernel, ftUser;
                    if (GetProcessTimes(h, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
                        ULARGE_INTEGER ull;
                        ull.LowPart = ftCreate.dwLowDateTime;
                        ull.HighPart = ftCreate.dwHighDateTime;
                        qint64 epoch = (qint64)(ull.QuadPart / 10000000ULL - 11644473600ULL);
                        info.startTime = QDateTime::fromSecsSinceEpoch(epoch);
                    }
                    CloseHandle(h);
                }
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return result;
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

bool launchProgram(const QString& path, const QString& args, bool hideWindow) {
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_ASYNCOK;
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
    sei.nShow = hideWindow ? SW_HIDE : SW_SHOWNORMAL;
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

#pragma comment(lib, "dbghelp.lib")

namespace {

constexpr DWORD kIntentionalCrashCode = 0xE0534701;
volatile LONG s_crashDumpSuppressed = FALSE;
std::wstring s_crashDumpDirectory;

LONG WINAPI crashDumpUnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) {
    if (InterlockedCompareExchange(&s_crashDumpSuppressed, FALSE, FALSE) != FALSE
        || GetSystemMetrics(SM_SHUTTINGDOWN) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    if (s_crashDumpDirectory.empty())
        return EXCEPTION_EXECUTE_HANDLER;

    CreateDirectoryW(s_crashDumpDirectory.c_str(), nullptr);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    const DWORD pid = GetCurrentProcessId();
    wchar_t fileName[160]{};
    swprintf_s(fileName, L"crash_%04u%02u%02u_%02u%02u%02u_%03u_%lu.dmp",
        now.wYear, now.wMonth, now.wDay,
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        static_cast<unsigned long>(pid));

    std::wstring dumpPath = s_crashDumpDirectory;
    if (!dumpPath.empty() && dumpPath.back() != L'\\')
        dumpPath.push_back(L'\\');
    dumpPath += fileName;

    HANDLE file = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return EXCEPTION_EXECUTE_HANDLER;

    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
    exceptionInfo.ThreadId = GetCurrentThreadId();
    exceptionInfo.ExceptionPointers = exceptionPointers;
    exceptionInfo.ClientPointers = FALSE;

    const BOOL written = MiniDumpWriteDump(GetCurrentProcess(), pid, file,
        MiniDumpNormal, exceptionPointers ? &exceptionInfo : nullptr, nullptr, nullptr);
    CloseHandle(file);
    if (!written)
        DeleteFileW(dumpPath.c_str());

    return EXCEPTION_EXECUTE_HANDLER;
}

QString findCrashDumpForProcess(DWORD pid, const QDateTime& monitoredSince) {
    QDir dir(appRootPath());
    const QString pattern = u"crash_*_%1.dmp"_s.arg(pid);
    const QFileInfoList candidates = dir.entryInfoList(
        QStringList{pattern}, QDir::Files, QDir::Time);
    const QDateTime earliest = monitoredSince.addSecs(-2);
    for (const QFileInfo& info : candidates) {
        if (info.lastModified() >= earliest)
            return info.absoluteFilePath();
    }
    return {};
}

} // namespace

void installCrashDumpHandler() {
    s_crashDumpDirectory = QDir::toNativeSeparators(appRootPath()).toStdWString();
    InterlockedExchange(&s_crashDumpSuppressed, FALSE);
    SetUnhandledExceptionFilter(crashDumpUnhandledExceptionFilter);
}

void setCrashDumpSuppressedForSystemShutdown(bool suppressed) {
    InterlockedExchange(&s_crashDumpSuppressed, suppressed ? TRUE : FALSE);
}

[[noreturn]] void triggerIntentionalCrash() {
    RaiseException(kIntentionalCrashCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    TerminateProcess(GetCurrentProcess(), kIntentionalCrashCode);
    ExitProcess(kIntentionalCrashCode);
}

struct FindWndData { DWORD pid; HWND hwnd; };
static BOOL CALLBACK enumWndCb(HWND hwnd, LPARAM lp) {
    auto* d = reinterpret_cast<FindWndData*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == d->pid && IsWindowVisible(hwnd)) { d->hwnd = hwnd; return FALSE; }
    return TRUE;
}
static HWND findMainWindow(DWORD pid) {
    FindWndData d{pid, nullptr};
    EnumWindows(enumWndCb, reinterpret_cast<LPARAM>(&d));
    return d.hwnd;
}

static QString createMiniDump(DWORD pid) {
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) return {};
    QString dir = appRootPath();
    QString path = dir + u"/hang_"_s + QDateTime::currentDateTime().toString(u"yyyyMMdd_HHmmss"_s) + u".dmp"_s;
    HANDLE file = CreateFileW(path.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MiniDumpWriteDump(proc, pid, file, MiniDumpNormal, nullptr, nullptr, nullptr);
        CloseHandle(file);
    } else {
        path.clear();
    }
    CloseHandle(proc);
    return path;
}

static bool isSystemShutdownInProgress(ConfigDatabase& db) {
    // SM_SHUTTINGDOWN 可覆盖主程序尚未来得及写入 SQLite 标记的窗口；
    // SQLite 标记可覆盖看门狗轮询期间没有及时观察到系统指标的窗口。
    return GetSystemMetrics(SM_SHUTTINGDOWN) != 0
        || db.value(u"system_shutdown_in_progress"_s, false).toBool();
}

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
    int hangCounter = 0;   // 连续未响应计数（每次1秒轮询）
    QDateTime monitoredSince = QDateTime::currentDateTime();

    while (true) {
        auto& db = ConfigDatabase::instance();
        if (isSystemShutdownInProgress(db)) {
            logRuntime(u"watchdog detected Windows session ending; exit without restart or dump"_s);
            break;
        }

        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, parentPid);
        if (!h) {
            logRuntime(QString("open main process failed, trying restart (err=%1)").arg(GetLastError()));
        } else {
            DWORD waitRc = WaitForSingleObject(h, 1000);
            CloseHandle(h);
            if (waitRc == WAIT_TIMEOUT) {
                if (isSystemShutdownInProgress(db)) {
                    logRuntime(u"watchdog detected Windows session ending while waiting for main process"_s);
                    break;
                }
                // 进程仍在运行，检查是否未响应
                HWND mainWnd = findMainWindow(parentPid);
                if (mainWnd) {
                    DWORD_PTR result = 0;
                    LRESULT lr = SendMessageTimeoutW(mainWnd, WM_NULL, 0, 0,
                        SMTO_ABORTIFHUNG, 1000, &result);
                    if (lr == 0 && GetLastError() == ERROR_TIMEOUT) {
                        hangCounter++;
                        if (hangCounter >= 3) {
                            // 在生成 Dump 前做最后一次关机检查，避免把关机阶段的
                            // 窗口无响应误判为应用卡死。
                            if (isSystemShutdownInProgress(db)) {
                                logRuntime(u"watchdog suppressed hang dump because Windows is shutting down"_s);
                                break;
                            }
                            logRuntime(QString("main process not responding for %1s, creating dump").arg(hangCounter));
                            QString dumpPath = createMiniDump(parentPid);
                            if (isSystemShutdownInProgress(db)) {
                                // 关机可能刚好发生在转储过程中，只删除本轮刚创建的文件。
                                if (!dumpPath.isEmpty())
                                    QFile::remove(dumpPath);
                                logRuntime(u"watchdog discarded hang dump created during Windows shutdown"_s);
                                break;
                            }
                            if (!dumpPath.isEmpty())
                                logRuntime(QString("dump created: %1").arg(dumpPath));
                            // 终止未响应的进程
                            HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, parentPid);
                            if (hp) { TerminateProcess(hp, 0xDEAD); CloseHandle(hp); }
                            // 记录重启原因
                            db.setValue(u"restart_reason"_s, u"hang"_s);
                            if (!dumpPath.isEmpty())
                                db.setValue(u"restart_dump_path"_s, dumpPath);
                            hangCounter = 0;
                            Sleep(1000);
                            // 进入下方重启流程
                        } else {
                            continue;
                        }
                    } else {
                        hangCounter = 0;
                        continue;
                    }
                } else {
                    hangCounter = 0;
                    continue;
                }
            } else {
                hangCounter = 0;
            }
        }

        if (isSystemShutdownInProgress(db)) {
            logRuntime(u"watchdog suppressed restart because Windows is shutting down"_s);
            break;
        }

        bool enabled = db.value(u"self_guard_enabled"_s, false).toBool();
        bool manualExit = db.value(u"self_guard_manual_exit"_s, false).toBool();
        if (!enabled || manualExit) break;

        // 未处理异常会由主进程先写入 crash_*.dmp；看门狗在进程退出后
        // 关联本次 PID 的最新转储，再记录重启通知信息。
        const QString crashDumpPath = findCrashDumpForProcess(parentPid, monitoredSince);
        if (db.value(u"restart_reason"_s).toString().isEmpty()) {
            db.setValue(u"restart_reason"_s, u"crash"_s);
            if (!crashDumpPath.isEmpty()) {
                db.setValue(u"restart_dump_path"_s, crashDumpPath);
                logRuntime(u"crash dump associated: %1"_s.arg(crashDumpPath));
            }
        }

        bool restarted = false;
        for (int attempt = 1; attempt <= 5; ++attempt) {
            if (isSystemShutdownInProgress(db))
                break;
            qint64 newPid = 0;
            bool ok = QProcess::startDetached(parentExe, { "--restart" }, workingDir, &newPid);
            if (ok && newPid > 0) {
                parentPid = static_cast<DWORD>(newPid);
                monitoredSince = QDateTime::currentDateTime();
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
