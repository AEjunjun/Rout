#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <iostream>
#include <string>
#include <ctime>
#include <vector>
#include <algorithm>
#include <shellapi.h>  // 新增：系统托盘需要

// 系统托盘相关
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT 1002
#define ID_TRAY_SHOW 1003
#define ID_TRAY_HIDE 1004
#define WM_TRAYICON (WM_USER + 1)

// 全局变量
NOTIFYICONDATA nid = {};
HWND hWnd = NULL;
HINSTANCE hInst = NULL;
bool bHidden = false;
HICON hIcon = NULL;

// 时间段结构体
struct TimeSlot {
    int startHour;   // 开始小时
    int startMin;    // 开始分钟
    int endHour;     // 结束小时
    int endMin;      // 结束分钟
    bool allowed;    // 是否允许运行
};

// 目标进程配置
struct ProcessConfig {
    std::string processName;  // 进程名（如：chrome.exe）
    std::vector<TimeSlot> schedule;  // 时间段安排
};

// 系统托盘图标处理函数
void CreateTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    nid.uCallbackMessage = WM_TRAYICON;
    
    // 使用系统默认图标
    hIcon = LoadIcon(NULL, IDI_APPLICATION);
    nid.hIcon = hIcon;
    
    nid.uTimeout = 3000;  // 3秒
    
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// 显示托盘通知
void ShowTrayNotification(const char* title, const char* message) {
    nid.uTimeout = 2000;
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

// 创建托盘菜单
void ShowTrayMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_SHOW, "显示窗口");
    AppendMenu(hMenu, MF_STRING, ID_TRAY_HIDE, "隐藏窗口");
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, "退出程序");
    
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

// 窗口过程函数
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        } else if (lParam == WM_LBUTTONDBLCLK) {
            // 双击显示/隐藏控制台窗口
            if (bHidden) {
                ShowWindow(GetConsoleWindow(), SW_SHOW);
                bHidden = false;
            } else {
                ShowWindow(GetConsoleWindow(), SW_HIDE);
                bHidden = true;
            }
        }
        break;
        
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_SHOW:
            ShowWindow(GetConsoleWindow(), SW_SHOW);
            SetForegroundWindow(GetConsoleWindow());
            bHidden = false;
            break;
        case ID_TRAY_HIDE:
            ShowWindow(GetConsoleWindow(), SW_HIDE);
            bHidden = true;
            break;
        case ID_TRAY_EXIT:
            // 清理托盘图标
            Shell_NotifyIcon(NIM_DELETE, &nid);
            DestroyIcon(hIcon);
            PostQuitMessage(0);
            exit(0);
            break;
        }
        break;
        
    case WM_CLOSE:
        // 点击窗口关闭按钮时，隐藏窗口而不是退出
        ShowWindow(GetConsoleWindow(), SW_HIDE);
        bHidden = true;
        ShowTrayNotification("进程时间控制器", "程序已最小化到系统托盘");
        return 0;
        
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// 创建隐藏的消息窗口
HWND CreateMessageWindow() {
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "ProcessTimeControllerClass";
    
    RegisterClass(&wc);
    
    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        "进程时间控制器",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
        NULL, NULL, hInst, NULL
    );
    
    return hwnd;
}

// 获取当前时间信息
void getCurrentTime(int& hour, int& min, int& weekDay) {
    time_t now = time(0);
    tm* ltm = localtime(&now);
    hour = ltm->tm_hour;
    min = ltm->tm_min;
    weekDay = ltm->tm_wday;  // 0=周日,1=周一,...,6=周六
}

// 检查指定进程是否存在
bool isProcessRunning(const std::string& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe)) {
        CloseHandle(hSnapshot);
        return false;
    }

    bool found = false;
    do {
        if (_stricmp(pe.szExeFile, processName.c_str()) == 0) {
            found = true;
            break;
        }
    } while (Process32Next(hSnapshot, &pe));

    CloseHandle(hSnapshot);
    return found;
}

// 结束指定进程
bool terminateProcess(const std::string& processName) {
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        return false;
    }

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (!Process32First(hSnapshot, &pe)) {
        CloseHandle(hSnapshot);
        return false;
    }

    bool terminated = false;
    do {
        if (_stricmp(pe.szExeFile, processName.c_str()) == 0) {
            HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (hProcess) {
                terminated = TerminateProcess(hProcess, 0);
                CloseHandle(hProcess);
            }
        }
    } while (Process32Next(hSnapshot, &pe));

    CloseHandle(hSnapshot);
    return terminated;
}

// 检查当前时间是否允许运行
bool isTimeAllowed(const ProcessConfig& config) {
    int hour, min, weekDay;
    getCurrentTime(hour, min, weekDay);
    
    // 将24小时制时间转换为分钟数，方便比较
    int currentMinutes = hour * 60 + min;
    
    for (const auto& slot : config.schedule) {
        int startMinutes = slot.startHour * 60 + slot.startMin;
        int endMinutes = slot.endHour * 60 + slot.endMin;
        
        // 处理跨天时间段
        if (startMinutes <= endMinutes) {
            if (currentMinutes >= startMinutes && currentMinutes < endMinutes) {
                return slot.allowed;
            }
        } else {
            if (currentMinutes >= startMinutes || currentMinutes < endMinutes) {
                return slot.allowed;
            }
        }
    }
    
    // 默认不允许
    return false;
}

// 监控线程函数
DWORD WINAPI MonitorThread(LPVOID lpParam) {
    ProcessConfig* config = (ProcessConfig*)lpParam;
    
    std::cout << "开始监控进程: " << config->processName << std::endl;
    
    while (true) {
        bool allowed = isTimeAllowed(*config);
        
        if (isProcessRunning(config->processName)) {
            if (!allowed) {
                std::cout << "禁止时间段检测到进程，正在结束..." << std::endl;
                if (terminateProcess(config->processName)) {
                    std::cout << "进程已结束" << std::endl;
                } else {
                    std::cout << "结束进程失败" << std::endl;
                }
            } else {
                // 只在控制台显示，不弹出通知
                static int count = 0;
                if (++count % 60 == 0) {  // 每10分钟输出一次
                    std::cout << "允许时间段，进程正常运行" << std::endl;
                }
            }
        } else {
            if (!allowed) {
                static int count2 = 0;
                if (++count2 % 120 == 0) {  // 每20分钟输出一次
                    std::cout << "禁止时间段，进程未运行" << std::endl;
                }
            }
        }
        
        // 每10秒检查一次
        Sleep(10000);
    }
    
    return 0;
}

int main() {
    hInst = GetModuleHandle(NULL);
    
    // 1. 创建隐藏的消息窗口
    hWnd = CreateMessageWindow();
    if (!hWnd) {
        std::cerr << "创建消息窗口失败!" << std::endl;
        return 1;
    }
    
    // 2. 创建系统托盘图标
    CreateTrayIcon(hWnd);
    ShowTrayNotification("进程时间控制器", "程序已启动，最小化到系统托盘");
    
    // 3. 配置监控进程
    ProcessConfig config;
    config.processName = "yuanbao.exe";
    
    // 设置时间段规则 - 中午限制时间是上午和下午的间隙
    
    TimeSlot workMorning = {8, 0, 12, 0, true};//上午可用时间
    TimeSlot workAfternoon = {13, 0, 14, 0, true};//下午可用时间
    TimeSlot workLunch = {14, 0, 15, 0, false};//限制使用时间
    TimeSlot workNight = {21, 0, 8, 0, false};//注意：这个是跨天的禁用时间
    
    config.schedule.push_back(workMorning);
    config.schedule.push_back(workLunch);
    config.schedule.push_back(workAfternoon);
    config.schedule.push_back(workNight);
    
    std::cout << "进程时间控制程序 v1.0" << std::endl;
    std::cout << "目标进程: " << config.processName << std::endl;
    std::cout << "作者：CatFox_Jun" << std::endl;
    std::cout << "提示：关闭此窗口不会退出程序" << std::endl;
    std::cout << "程序会继续在后台运行" << std::endl;
    std::cout << "请在系统托盘图标右键点击退出" << std::endl;
    std::cout << "=====================================" << std::endl;
    
    // 4. 启动监控线程
    HANDLE hThread = CreateThread(NULL, 0, MonitorThread, &config, 0, NULL);
    
    // 5. 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    // 6. 清理
    if (hThread) {
        TerminateThread(hThread, 0);
        CloseHandle(hThread);
    }
    
    return 0;
}
