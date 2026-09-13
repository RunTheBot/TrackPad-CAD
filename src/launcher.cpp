#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>

static std::wstring quote(const std::filesystem::path& value) {
    return L"\"" + value.wstring() + L"\"";
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wchar_t module[MAX_PATH];
    DWORD length=GetModuleFileNameW(nullptr,module,MAX_PATH);
    if(!length||length==MAX_PATH)return 1;
    const auto root=std::filesystem::path(module).parent_path();
    const auto script=root/L"run-trackpad.ps1";
    const auto config=root/L"TrackPad CAD.json";
    if(!std::filesystem::exists(script)||!std::filesystem::exists(config)){
        MessageBoxW(nullptr,L"run-trackpad.ps1 or TrackPad CAD.json is missing beside the launcher.",L"TrackPad CAD",MB_OK|MB_ICONERROR);
        return 2;
    }
    std::wstring args=L"-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "+quote(script)+L" -Config "+quote(config);
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_FLAG_NO_UI;info.lpVerb=L"runas";info.lpFile=L"pwsh.exe";info.lpParameters=args.c_str();info.lpDirectory=root.c_str();info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)){
        MessageBoxW(nullptr,L"TrackPad CAD could not start. PowerShell 7 and elevation are required.",L"TrackPad CAD",MB_OK|MB_ICONERROR);
        return 3;
    }
    return 0;
}
