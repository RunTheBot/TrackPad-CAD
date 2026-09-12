#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vector>
#include <iostream>
#include <iomanip>
static unsigned reports=0, nonzero=0;
LRESULT CALLBACK proc(HWND w,UINT m,WPARAM p,LPARAM l) {
    if(m==WM_INPUT) {
        UINT n=0;
        if(GetRawInputData((HRAWINPUT)l,RID_INPUT,nullptr,&n,sizeof(RAWINPUTHEADER))==UINT(-1)) return DefWindowProc(w,m,p,l);
        std::vector<BYTE> b(n);
        if(GetRawInputData((HRAWINPUT)l,RID_INPUT,b.data(),&n,sizeof(RAWINPUTHEADER))==UINT(-1)) return DefWindowProc(w,m,p,l);
        auto* r=(RAWINPUT*)b.data();
        RID_DEVICE_INFO di{};di.cbSize=sizeof(di);UINT dn=sizeof(di);
        if(r->header.dwType==RIM_TYPEHID && GetRawInputDeviceInfo(r->header.hDevice,RIDI_DEVICEINFO,&di,&dn)!=UINT(-1) && di.hid.dwVendorId==0x046d && di.hid.dwProductId==0xc62b) {
            for(DWORD i=0;i<r->data.hid.dwCount;++i) {
                auto* report=r->data.hid.bRawData+i*r->data.hid.dwSizeHid;
                bool nz=false;for(DWORD j=1;j<r->data.hid.dwSizeHid;++j) nz |= report[j]!=0;
                ++reports;if(nz)++nonzero;
                if(reports<=5 || nz) {
                    std::cout<<"HID ";for(DWORD j=0;j<r->data.hid.dwSizeHid;++j)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(report[j])<<' ';
                    std::cout<<std::dec<<std::endl;
                }
            }
        }
    }
    return DefWindowProc(w,m,p,l);
}
int main() {
    WNDCLASS wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandle(nullptr);wc.lpszClassName="TPCReportMonitor";
    if(!RegisterClass(&wc))return 1;
    auto w=CreateWindow(wc.lpszClassName,"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);
    RAWINPUTDEVICE rd{1,8,RIDEV_INPUTSINK,w};
    if(!w || !RegisterRawInputDevices(&rd,1,sizeof(rd)))return 2;
    std::cout<<"Monitoring SpaceMouse HID for 120 seconds"<<std::endl;
    auto start=GetTickCount64();
    while(GetTickCount64()-start<120000) {
        MSG msg;while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessage(&msg);}Sleep(2);
    }
    std::cout<<"Received reports="<<reports<<" nonzero="<<nonzero<<std::endl;
    DestroyWindow(w);return nonzero?0:3;
}
