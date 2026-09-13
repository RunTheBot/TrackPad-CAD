#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

int capture_main(int argc, char** argv);

namespace {
constexpr int R_SCRIPT=201, R_BRIDGE=202, R_HIDMAESTRO=203, R_DESCRIPTOR=204, R_CONFIG=205, R_HOOK_MAIN=206, R_HOOK_RELAY=207, R_HOOK_MANIFEST=208;
std::wstring quote(const std::filesystem::path& value) { return L"\"" + value.wstring() + L"\""; }
std::string utf8(const std::wstring& input) {
    const int size=WideCharToMultiByte(CP_UTF8,0,input.c_str(),-1,nullptr,0,nullptr,nullptr);
    std::string result(size ? size : 0, '\0');
    if(size>1) { WideCharToMultiByte(CP_UTF8,0,input.c_str(),-1,result.data(),size,nullptr,nullptr); result.pop_back(); }
    return result;
}
bool extract(HINSTANCE instance, int id, const std::filesystem::path& path, bool preserve=false) {
    if(preserve && std::filesystem::exists(path)) return true;
    HRSRC resource=FindResourceW(instance,MAKEINTRESOURCEW(id),MAKEINTRESOURCEW(10));
    if(!resource) return false;
    HGLOBAL handle=LoadResource(instance,resource); const void* data=LockResource(handle); const DWORD size=SizeofResource(instance,resource);
    if(!data || !size) return false;
    std::error_code error; std::filesystem::create_directories(path.parent_path(),error);
    const auto temporary=path.wstring()+L".new";
    std::ofstream out(std::filesystem::path(temporary),std::ios::binary|std::ios::trunc);
    if(!out) return false;
    out.write(static_cast<const char*>(data),size); out.close();
    return static_cast<bool>(out) && MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
}
std::filesystem::path local_runtime() {
    PWSTR value=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&value))) return {};
    std::filesystem::path result=std::filesystem::path(value)/L"TrackPad CAD"/L"runtime"; CoTaskMemFree(value); return result;
}
std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR value=nullptr;
    if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&value))) return {};
    std::filesystem::path result(value); CoTaskMemFree(value); return result;
}
bool prepare_runtime(HINSTANCE instance, const std::filesystem::path& root) {
    return extract(instance,R_SCRIPT,root/L"run-trackpad.ps1") && extract(instance,R_BRIDGE,root/L"src"/L"HidMaestroBridge.cs")
        && extract(instance,R_HIDMAESTRO,root/L"build"/L"hidmaestro"/L"HIDMaestro.Core.dll") && extract(instance,R_DESCRIPTOR,root/L"experiments"/L"hidmaestro"/L"spacemouse-pro.hex")
        && extract(instance,R_HOOK_MAIN,root/L"browser-extension"/L"main-hook.js") && extract(instance,R_HOOK_RELAY,root/L"browser-extension"/L"relay.js")
        && extract(instance,R_HOOK_MANIFEST,root/L"browser-extension"/L"manifest.json") && extract(instance,R_CONFIG,root/L"TrackPad CAD.json",true);
}
int launch(HINSTANCE instance, const std::filesystem::path& executable) {
    const auto root=local_runtime();
    if(root.empty() || !prepare_runtime(instance,root)) { MessageBoxW(nullptr,L"TrackPad CAD could not prepare its runtime files.",L"TrackPad CAD",MB_OK|MB_ICONERROR); return 2; }
    std::wstring args=L"-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "+quote(root/L"run-trackpad.ps1")+L" -Config "+quote(root/L"TrackPad CAD.json")+L" -CaptureExecutable "+quote(executable);
    SHELLEXECUTEINFOW info{}; info.cbSize=sizeof(info); info.fMask=SEE_MASK_FLAG_NO_UI; info.lpVerb=L"runas"; info.lpFile=L"pwsh.exe"; info.lpParameters=args.c_str(); info.lpDirectory=root.c_str(); info.nShow=SW_HIDE;
    if(!ShellExecuteExW(&info)) { MessageBoxW(nullptr,L"TrackPad CAD needs PowerShell 7 and permission to start its HID bridge.",L"TrackPad CAD",MB_OK|MB_ICONERROR); return 3; }
    return 0;
}
bool create_shortcut(const std::filesystem::path& executable, const std::filesystem::path& link) {
    std::error_code error; std::filesystem::create_directories(link.parent_path(),error);
    IShellLinkW* shellLink=nullptr;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shellLink)))) return false;
    shellLink->SetPath(executable.c_str()); shellLink->SetDescription(L"TrackPad CAD"); shellLink->SetIconLocation(executable.c_str(),0);
    IPersistFile* persist=nullptr; const HRESULT query=shellLink->QueryInterface(IID_PPV_ARGS(&persist));
    if(SUCCEEDED(query)) { persist->Save(link.c_str(),TRUE); persist->Release(); }
    shellLink->Release(); return SUCCEEDED(query);
}
bool register_app(const std::filesystem::path& executable) {
    HKEY key=nullptr;
    const wchar_t* subkey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TrackPad CAD";
    if(RegCreateKeyExW(HKEY_CURRENT_USER,subkey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS) return false;
    auto set_text=[&](const wchar_t* name,const std::wstring& value) { return RegSetValueExW(key,name,0,REG_SZ,reinterpret_cast<const BYTE*>(value.c_str()),static_cast<DWORD>((value.size()+1)*sizeof(wchar_t)))==ERROR_SUCCESS; };
    DWORD one=1;
    const bool result=set_text(L"DisplayName",L"TrackPad CAD") && set_text(L"DisplayVersion",L"1.0.0") && set_text(L"Publisher",L"TrackPad CAD")
        && set_text(L"InstallLocation",executable.parent_path().wstring()) && set_text(L"DisplayIcon",executable.wstring())
        && set_text(L"UninstallString",quote(executable)+L" --uninstall")
        && RegSetValueExW(key,L"NoModify",0,REG_DWORD,reinterpret_cast<const BYTE*>(&one),sizeof(one))==ERROR_SUCCESS
        && RegSetValueExW(key,L"NoRepair",0,REG_DWORD,reinterpret_cast<const BYTE*>(&one),sizeof(one))==ERROR_SUCCESS;
    RegCloseKey(key); return result;
}
void remove_registration() { RegDeleteTreeW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TrackPad CAD"); }
int uninstall(const std::filesystem::path& executable) {
    if(MessageBoxW(nullptr,L"Remove TrackPad CAD, its saved configuration, browser-extension runtime files, and desktop/start-menu shortcuts?",L"Uninstall TrackPad CAD",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2)!=IDYES) return 0;
    const auto installFolder=executable.parent_path();
    const auto runtimeRoot=local_runtime().parent_path();
    const auto desktopLink=known_folder(FOLDERID_Desktop)/L"TrackPad CAD.lnk";
    const auto startLink=known_folder(FOLDERID_StartMenu)/L"Programs"/L"TrackPad CAD.lnk";
    std::error_code error; std::filesystem::remove(desktopLink,error); std::filesystem::remove(startLink,error); remove_registration();
    const auto script=known_folder(FOLDERID_LocalAppData)/L"Temp"/L"TrackPad CAD uninstall.cmd";
    std::ofstream out(script,std::ios::trunc);
    if(!out) { MessageBoxW(nullptr,L"TrackPad CAD registration was removed, but its files could not be scheduled for cleanup.",L"Uninstall TrackPad CAD",MB_OK|MB_ICONWARNING); return 5; }
    out << "@echo off\r\ntimeout /t 2 /nobreak >nul\r\nrmdir /s /q \"" << installFolder.string() << "\"\r\nrmdir /s /q \"" << runtimeRoot.string() << "\"\r\ndel \"%~f0\"\r\n";
    out.close();
    ShellExecuteW(nullptr,L"open",L"cmd.exe",(L"/c "+quote(script)).c_str(),nullptr,SW_HIDE);
    MessageBoxW(nullptr,L"TrackPad CAD is being removed. Its installed files and saved settings will be deleted in a moment.",L"Uninstall TrackPad CAD",MB_OK|MB_ICONINFORMATION);
    return 0;
}
int install(HINSTANCE instance, const std::filesystem::path& setup) {
    PWSTR appData=nullptr;
    if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&appData))) return 4;
    const std::filesystem::path folder=std::filesystem::path(appData)/L"Programs"/L"TrackPad CAD"; CoTaskMemFree(appData);
    std::error_code error; std::filesystem::create_directories(folder,error);
    const auto installed=folder/L"TrackPad CAD.exe";
    if(error || !CopyFileW(setup.c_str(),installed.c_str(),FALSE)) { MessageBoxW(nullptr,L"TrackPad CAD could not copy itself to your user Applications folder.",L"TrackPad CAD Setup",MB_OK|MB_ICONERROR); return 4; }
    CoInitialize(nullptr);
    create_shortcut(installed,known_folder(FOLDERID_Desktop)/L"TrackPad CAD.lnk");
    create_shortcut(installed,known_folder(FOLDERID_StartMenu)/L"Programs"/L"TrackPad CAD.lnk");
    CoUninitialize();
    if(!register_app(installed)) { MessageBoxW(nullptr,L"TrackPad CAD installed, but Windows could not register it in Installed Apps.",L"TrackPad CAD Setup",MB_OK|MB_ICONWARNING); }
    const int result=launch(instance,installed);
    if(!result) MessageBoxW(nullptr,L"TrackPad CAD is installed. A shortcut was added to your desktop.",L"TrackPad CAD Setup",MB_OK|MB_ICONINFORMATION);
    return result;
}
int run_capture(int argc, wchar_t** wide) {
    std::vector<std::string> values; values.emplace_back("TrackPad CAD");
    for(int i=2;i<argc;++i) values.push_back(utf8(wide[i]));
    std::vector<char*> arguments; for(auto& value:values) arguments.push_back(value.data());
    return capture_main(static_cast<int>(arguments.size()),arguments.data());
}
}
int WINAPI WinMain(HINSTANCE instance,HINSTANCE,LPSTR,int) {
    int argc=0; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc); if(!argv) return 1;
    if(argc>1 && std::wstring(argv[1])==L"--capture") { const int result=run_capture(argc,argv); LocalFree(argv); return result; }
    const bool shouldUninstall=argc>1 && std::wstring(argv[1])==L"--uninstall";
    wchar_t module[MAX_PATH]; DWORD length=GetModuleFileNameW(nullptr,module,MAX_PATH); LocalFree(argv);
    if(!length || length==MAX_PATH) return 1;
    const std::filesystem::path executable(module);
    if(shouldUninstall) return uninstall(executable);
    const std::wstring name=executable.filename().wstring();
    if(name.find(L" Setup")!=std::wstring::npos) return install(instance,executable);
    return launch(instance,executable);
}
