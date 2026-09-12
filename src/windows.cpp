#include <windows.h>
#include <winioctl.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
#include "windows.hpp"
#include "protocol.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>

namespace tpc {
namespace {
double now_ms() {return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
std::atomic<bool> quit{false};
BOOL WINAPI stop(DWORD) {quit=true;return TRUE;}
bool foreground(const std::wstring& name) {
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid); if(!p) return false;
    wchar_t path[32768]; DWORD count=32768;
    const bool ok=QueryFullProcessImageNameW(p,0,path,&count)!=0; CloseHandle(p);
    if(!ok) return false;
    std::wstring full(path,count); const auto pos=full.find_last_of(L"\\/");
    return _wcsicmp(full.substr(pos==std::wstring::npos?0:pos+1).c_str(),name.c_str())==0;
}
class Output {
    HANDLE handle=INVALID_HANDLE_VALUE;
    bool previous_zero=true;
    bool stream=false;
public:
    explicit Output(bool driver, bool streamMode=false) : stream(streamMode) {
        if(driver) {
            handle=CreateFileW(L"\\\\.\\TrackPadCAD",GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
            if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open TrackPadCAD driver (Win32 "+std::to_string(GetLastError())+"). Driver must be installed; its prototype ACL requires elevation.");
        }
    }
    void send(const Frame& f) {
        if(handle!=INVALID_HANDLE_VALUE) {
            TPC_FRAME wire{}; wire.version=TPC_ABI_VERSION; wire.buttons=f.buttons;
            for(size_t i=0;i<6;++i) wire.axes[i]=f.axes[i];
            DWORD bytes=0;
            if(!DeviceIoControl(handle,TPC_IOCTL_FRAME,&wire,sizeof(wire),nullptr,0,&bytes,nullptr)) throw std::runtime_error("Driver frame rejected: "+std::to_string(GetLastError()));
        } else if(stream) {
            std::cout<<"FRAME"; for(auto a:f.axes) std::cout<<' '<<a;
            std::cout<<' '<<f.buttons<<std::endl;
        } else if(!f.zero() || !previous_zero) {
            for(auto a:f.axes) std::cout<<a<<' '; std::cout<<"buttons="<<f.buttons<<'\n';
        }
        previous_zero=f.zero();
    }
    ~Output() {if(handle!=INVALID_HANDLE_VALUE) {try {send({});} catch(...) {} CloseHandle(handle);}}
};
struct Decoder {
    std::vector<BYTE> storage;
    std::vector<HIDP_VALUE_CAPS> values;
    std::vector<HIDP_BUTTON_CAPS> buttons;
    std::set<USHORT> fingers;
    bool warned=false;
    PHIDP_PREPARSED_DATA data() {return reinterpret_cast<PHIDP_PREPARSED_DATA>(storage.data());}
    explicit Decoder(HANDLE device) {
        UINT size=0;
        if(GetRawInputDeviceInfoW(device,RIDI_PREPARSEDDATA,nullptr,&size)==UINT(-1) || !size) return;
        storage.resize(size);
        if(GetRawInputDeviceInfoW(device,RIDI_PREPARSEDDATA,storage.data(),&size)==UINT(-1)) {storage.clear();return;}
        HIDP_CAPS caps{};
        if(HidP_GetCaps(data(),&caps)!=HIDP_STATUS_SUCCESS || caps.UsagePage!=0x0d || caps.Usage!=0x05) {storage.clear();return;}
        values.resize(caps.NumberInputValueCaps); USHORT nv=static_cast<USHORT>(values.size());
        if(HidP_GetValueCaps(HidP_Input,values.data(),&nv,data())!=HIDP_STATUS_SUCCESS) {storage.clear();return;}
        values.resize(nv);
        buttons.resize(caps.NumberInputButtonCaps); USHORT nb=static_cast<USHORT>(buttons.size());
        if(nb && HidP_GetButtonCaps(HidP_Input,buttons.data(),&nb,data())!=HIDP_STATUS_SUCCESS) {storage.clear();return;}
        buttons.resize(nb);
        for(auto v:values) if(v.UsagePage==0x0d && !v.IsRange && v.NotRange.Usage==0x51) fingers.insert(v.LinkCollection);
    }
    const HIDP_VALUE_CAPS* cap(USHORT page,USHORT usage,USHORT link) {
        for(const auto& c:values) if(c.UsagePage==page && c.LinkCollection==link &&
            (c.IsRange ? usage>=c.Range.UsageMin && usage<=c.Range.UsageMax : usage==c.NotRange.Usage)) return &c;
        return nullptr;
    }
    bool value(char* r,ULONG len,USHORT page,USHORT usage,USHORT link,ULONG& out) {
        return HidP_GetUsageValue(HidP_Input,page,link,usage,&out,data(),r,len)==HIDP_STATUS_SUCCESS;
    }
    bool has_button(USHORT usage,USHORT link) {
        for(auto b:buttons) if(b.UsagePage==0x0d && b.LinkCollection==link &&
            (b.IsRange ? usage>=b.Range.UsageMin && usage<=b.Range.UsageMax : usage==b.NotRange.Usage)) return true;
        return false;
    }
    bool pressed(char* r,ULONG len,USHORT usage,USHORT link) {
        USAGE list[64]; ULONG n=64;
        if(HidP_GetUsages(HidP_Input,0x0d,link,list,&n,data(),r,len)!=HIDP_STATUS_SUCCESS) return false;
        return std::find(list,list+n,usage)!=list+n;
    }
    double mm(ULONG raw,const HIDP_VALUE_CAPS* c) {
        // Precision Touchpad axes use centimetres (SI linear, 0x11) or inches (0x13).
        if(!c || c->LogicalMax<=c->LogicalMin || c->PhysicalMax<=c->PhysicalMin) throw std::runtime_error("Touchpad lacks calibrated physical axes");
        int exp=static_cast<int>(c->UnitsExp&15); if(exp>=8) exp-=16;
        const double unit=c->Units==0x11 ? 10.0 : c->Units==0x13 ? 25.4 : 0;
        if(unit==0) throw std::runtime_error("Unsupported touchpad coordinate units");
        return (c->PhysicalMin+(static_cast<double>(raw)-c->LogicalMin)*(c->PhysicalMax-c->PhysicalMin)/(c->LogicalMax-c->LogicalMin))*std::pow(10.0,exp)*unit;
    }
    std::optional<Sample> decode(char* r,ULONG len,double ms,bool shift) {
        if(storage.empty()) return {};
        ULONG count=0;
        if(!value(r,len,0x0d,0x54,0,count)) return {};
        Sample s{ms,{},shift};
        unsigned reported=0;
        for(auto link:fingers) {
            if(reported>=count) break;
            ULONG id=0,x=0,y=0;
            if(!value(r,len,0x0d,0x51,link,id) || !value(r,len,1,0x30,link,x) || !value(r,len,1,0x31,link,y)) continue;
            ++reported;
            if(!pressed(r,len,0x42,link)) continue;
            if(has_button(0x47,link) && !pressed(r,len,0x47,link)) continue; // reject palms
            s.contacts.push_back({id,mm(x,cap(1,0x30,link)),mm(y,cap(1,0x31,link))});
        }
        // Only accept complete parallel reports. Never guess a partial/hybrid frame.
        if(count!=reported) {
            if(!warned) {std::cerr<<"Incomplete/hybrid touchpad report: capture is disabled for this packet; decoder needs device-specific assembly.\n"; warned=true;}
            return {};
        }
        return s;
    }
};
struct App {
    Engine engine;
    std::wstring target;
    std::map<HANDLE,std::unique_ptr<Decoder>> decoders;
    HANDLE active_device=nullptr;
    bool armed=false;
    bool stream=false;
    bool allowed() {return stream || ((GetAsyncKeyState(VK_F8)&0x8000) && foreground(target));}
    void input(HRAWINPUT handle) {
        if(!armed || !allowed()) {engine.cancel();return;}
        UINT size=0;
        if(GetRawInputData(handle,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))==UINT(-1)) return;
        std::vector<BYTE> buffer(size);
        if(GetRawInputData(handle,RID_INPUT,buffer.data(),&size,sizeof(RAWINPUTHEADER))==UINT(-1)) return;
        auto* raw=reinterpret_cast<RAWINPUT*>(buffer.data());
        if(raw->header.dwType!=RIM_TYPEHID) return;
        auto& decoder=decoders[raw->header.hDevice];
        if(!decoder) decoder=std::make_unique<Decoder>(raw->header.hDevice);
        if(decoder->storage.empty()) return;
        if(active_device && active_device!=raw->header.hDevice) return;
        active_device=raw->header.hDevice;
        for(DWORD i=0;i<raw->data.hid.dwCount;++i) {
            auto s=decoder->decode(reinterpret_cast<char*>(raw->data.hid.bRawData)+i*raw->data.hid.dwSizeHid,raw->data.hid.dwSizeHid,now_ms(),(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0);
            if(s) engine.feed(std::move(*s)); else engine.cancel();
        }
    }
};
LRESULT CALLBACK window_proc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    auto* app=reinterpret_cast<App*>(GetWindowLongPtrW(w,GWLP_USERDATA));
    if(msg==WM_INPUT && app) {
        try {app->input(reinterpret_cast<HRAWINPUT>(lp));}
        catch(const std::exception& e) {std::cerr<<e.what()<<'\n';app->engine.cancel();quit=true;}
    }
    if(msg==WM_INPUT_DEVICE_CHANGE && app) {app->engine.cancel();app->active_device=nullptr;app->decoders.clear();}
    return DefWindowProcW(w,msg,wp,lp);
}
}
int devices() {
    UINT count=0;
    if(GetRawInputDeviceList(nullptr,&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1)) throw std::runtime_error("Cannot enumerate raw input devices");
    std::vector<RAWINPUTDEVICELIST> list(count);
    if(GetRawInputDeviceList(list.data(),&count,sizeof(RAWINPUTDEVICELIST))==UINT(-1)) throw std::runtime_error("Raw device enumeration failed");
    unsigned found=0;
    for(UINT i=0;i<count;++i) {
        RID_DEVICE_INFO info{}; info.cbSize=sizeof(info); UINT size=sizeof(info);
        if(GetRawInputDeviceInfoW(list[i].hDevice,RIDI_DEVICEINFO,&info,&size)==UINT(-1) || info.dwType!=RIM_TYPEHID) continue;
        std::cout<<"HID VID="<<std::hex<<info.hid.dwVendorId<<" PID="<<info.hid.dwProductId<<" page="<<info.hid.usUsagePage<<" usage="<<info.hid.usUsage<<std::dec;
        if(info.hid.usUsagePage==0x0d && info.hid.usUsage==5) {
            ++found;std::cout<<" [Precision Touchpad]";
            Decoder decoder(list[i].hDevice);
            std::cout<<" finger-slots="<<decoder.fingers.size();
            for(auto link:decoder.fingers) {
                auto x=decoder.cap(1,0x30,link), y=decoder.cap(1,0x31,link);
                if(x && y) std::cout<<"\n  link="<<link<<" report="<<unsigned(x->ReportID)
                    <<" X="<<x->LogicalMin<<".."<<x->LogicalMax<<" physical="<<x->PhysicalMin<<".."<<x->PhysicalMax
                    <<" Y="<<y->LogicalMin<<".."<<y->LogicalMax<<" physical="<<y->PhysicalMin<<".."<<y->PhysicalMax
                    <<" units="<<std::hex<<x->Units<<std::dec<<" exponent="<<x->UnitsExp;
            }
        }
        if(info.hid.usUsagePage==1 && info.hid.usUsage==8) std::cout<<" [Multi-axis Controller]";
        std::cout<<'\n';
    }
    std::cout<<found<<" raw Precision Touchpad collection(s) exposed.\n";
    return 0;
}
int live(const std::wstring& target,bool driver,bool stream) {
    if(target.find_first_of(L"\\/")!=std::wstring::npos) throw std::runtime_error("Use a process basename, e.g. chrome.exe");
    Output output(driver,stream); App app; app.target=target; app.stream=stream; quit=false;
    const auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW cls{}; cls.hInstance=instance; cls.lpszClassName=L"TrackPadCADCapture"; cls.lpfnWndProc=window_proc;
    if(!RegisterClassW(&cls)) throw std::runtime_error("Cannot register input window");
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"TrackPad CAD",0,0,0,0,0,HWND_MESSAGE,nullptr,instance,nullptr);
    if(!window) throw std::runtime_error("Cannot create input window");
    SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&app));
    RAWINPUTDEVICE rid{0x0d,0x05,RIDEV_INPUTSINK|RIDEV_DEVNOTIFY,window};
    if(!RegisterRawInputDevices(&rid,1,sizeof(rid))) {DestroyWindow(window);throw std::runtime_error("Touchpad Raw Input registration failed");}
    SetConsoleCtrlHandler(stop,TRUE);
    if(!stream) std::cout<<"Hold F8 in the target CAD process to enable capture. Ctrl+C exits.\n";
    double next=now_ms();
    while(!quit) {
        const bool allow=app.allowed();
        if(!allow || !app.armed) {app.engine.cancel();app.active_device=nullptr;}
        app.armed=allow;
        MSG msg{};
        // Bound draining so input floods cannot starve zero reports or focus checks.
        for(int n=0;n<256 && PeekMessageW(&msg,nullptr,0,0,PM_REMOVE);++n) {TranslateMessage(&msg);DispatchMessageW(&msg);}
        const auto now=now_ms();
        if(now>=next) {
            Frame f;
            if(app.allowed()) {
                f=app.engine.tick(now);
                for(auto a:app.engine.take_actions()) {
                    if(a==Action::frame_selection) {f.buttons|=1;std::cerr<<"Frame selection: button 1 (CAD binding required)\n";}
                    else {POINT pt{};GetCursorPos(&pt);std::cerr<<"Pivot requested at screen "<<pt.x<<","<<pt.y<<"; CAD adapter not implemented\n";}
                }
            } else app.engine.cancel();
            output.send(f); next=now+8;
        }
        Sleep(1);
    }
    output.send({}); DestroyWindow(window); SetConsoleCtrlHandler(stop,FALSE); return 0;
}
int rotation_test(const std::wstring& target) {
    Output output(true); quit=false; SetConsoleCtrlHandler(stop,TRUE);
    std::cout<<"Focus the target CAD process and hold F8. One second of Ry=80 will be sent.\n";
    while(!quit && !((GetAsyncKeyState(VK_F8)&0x8000) && foreground(target))) Sleep(8);
    const auto start=now_ms();
    while(!quit && now_ms()-start<1000 && (GetAsyncKeyState(VK_F8)&0x8000) && foreground(target)) {
        Frame f;f.axes[4]=80;output.send(f);Sleep(8);
    }
    output.send({});SetConsoleCtrlHandler(stop,FALSE);return 0;
}
}
