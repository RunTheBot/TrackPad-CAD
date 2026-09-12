/* Experimental KMDF/VHF source driver. Build with the WDK, not CMake. */
#include <ntddk.h>
#include <wdf.h>
#include <hidport.h>
#include <vhf.h>
#include "../include/protocol.h"

C_ASSERT(sizeof(TPC_FRAME) == 20);
typedef struct _TPC_DEVICE_CONTEXT {
    VHFHANDLE Vhf;
    WDFTIMER Timer;
    WDFWAITLOCK Lock;
    TPC_FRAME Latest;
    ULONGLONG LastWrite;
    BOOLEAN Online;
} TPC_DEVICE_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(TPC_DEVICE_CONTEXT, GetDeviceContext)
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD AddDevice;
EVT_WDF_OBJECT_CONTEXT_CLEANUP Cleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Ioctl;
EVT_WDF_TIMER Tick;
EVT_WDF_DEVICE_D0_ENTRY D0Entry;
EVT_WDF_DEVICE_D0_EXIT D0Exit;
EVT_WDF_FILE_CLEANUP FileCleanup;

static NTSTATUS Submit(TPC_DEVICE_CONTEXT* c, const TPC_FRAME* frame) {
    UCHAR axes[13] = {1};
    UCHAR buttons[5] = {2};
    HID_XFER_PACKET packet;
    NTSTATUS first, second;
    ULONG i;
    for (i=0;i<6;++i) {
        USHORT a=(USHORT)frame->axes[i];
        axes[1+2*i]=(UCHAR)a; axes[2+2*i]=(UCHAR)(a>>8);
    }
    for(i=0;i<4;++i) buttons[1+i]=(UCHAR)(frame->buttons>>(8*i));
    RtlZeroMemory(&packet,sizeof(packet));
    packet.reportId=1;packet.reportBuffer=axes;packet.reportBufferLen=sizeof(axes);
    first=VhfReadReportSubmit(c->Vhf,&packet);
    packet.reportId=2;packet.reportBuffer=buttons;packet.reportBufferLen=sizeof(buttons);
    second=VhfReadReportSubmit(c->Vhf,&packet);
    return NT_SUCCESS(first) ? second : first;
}
VOID Tick(WDFTIMER timer) {
    TPC_DEVICE_CONTEXT* c=GetDeviceContext(WdfTimerGetParentObject(timer));
    TPC_FRAME frame;
    WdfWaitLockAcquire(c->Lock,NULL);
    frame=c->Latest;
    // Independent driver watchdog: a crashed/blocked producer cannot latch motion.
    if(KeQueryInterruptTime()-c->LastWrite > 1000000ULL) RtlZeroMemory(&frame,sizeof(frame));
    if(c->Online && !NT_SUCCESS(Submit(c,&frame))) {
        RtlZeroMemory(&c->Latest,sizeof(c->Latest));
        // Next timer interval retries zero, including button release.
    }
    if(c->Online) WdfTimerStart(timer,WDF_REL_TIMEOUT_IN_MS(8));
    WdfWaitLockRelease(c->Lock);
}
VOID Ioctl(WDFQUEUE queue,WDFREQUEST request,size_t outputLength,size_t inputLength,ULONG code) {
    TPC_DEVICE_CONTEXT* c=GetDeviceContext(WdfIoQueueGetDevice(queue));
    TPC_FRAME* frame;
    NTSTATUS status;
    ULONG i;
    UNREFERENCED_PARAMETER(outputLength);
    if(code!=TPC_IOCTL_FRAME) {WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);return;}
    if(inputLength!=sizeof(TPC_FRAME)) {WdfRequestComplete(request,STATUS_INFO_LENGTH_MISMATCH);return;}
    status=WdfRequestRetrieveInputBuffer(request,sizeof(TPC_FRAME),(PVOID*)&frame,NULL);
    if(!NT_SUCCESS(status)) {WdfRequestComplete(request,status);return;}
    if(frame->version!=TPC_ABI_VERSION) {WdfRequestComplete(request,STATUS_REVISION_MISMATCH);return;}
    for(i=0;i<6;++i) if(frame->axes[i]<-350 || frame->axes[i]>350) {WdfRequestComplete(request,STATUS_INVALID_PARAMETER);return;}
    WdfWaitLockAcquire(c->Lock,NULL);
    if(c->Online) {c->Latest=*frame;c->LastWrite=KeQueryInterruptTime();status=STATUS_SUCCESS;}
    else status=STATUS_DEVICE_NOT_READY;
    WdfWaitLockRelease(c->Lock);
    WdfRequestComplete(request,status);
}
VOID FileCleanup(WDFFILEOBJECT file) {
    TPC_DEVICE_CONTEXT* c=GetDeviceContext(WdfFileObjectGetDevice(file));
    WdfWaitLockAcquire(c->Lock,NULL);
    RtlZeroMemory(&c->Latest,sizeof(c->Latest));c->LastWrite=0;
    WdfWaitLockRelease(c->Lock);
}
NTSTATUS D0Entry(WDFDEVICE device,WDF_POWER_DEVICE_STATE previous) {
    TPC_DEVICE_CONTEXT* c=GetDeviceContext(device);
    UNREFERENCED_PARAMETER(previous);
    WdfWaitLockAcquire(c->Lock,NULL);
    RtlZeroMemory(&c->Latest,sizeof(c->Latest));c->LastWrite=0;c->Online=TRUE;
    WdfWaitLockRelease(c->Lock);
    WdfTimerStart(c->Timer,WDF_REL_TIMEOUT_IN_MS(8));
    return STATUS_SUCCESS;
}
NTSTATUS D0Exit(WDFDEVICE device,WDF_POWER_DEVICE_STATE target) {
    TPC_DEVICE_CONTEXT* c=GetDeviceContext(device);
    TPC_FRAME zero={0};
    UNREFERENCED_PARAMETER(target);
    WdfWaitLockAcquire(c->Lock,NULL);
    c->Online=FALSE;RtlZeroMemory(&c->Latest,sizeof(c->Latest));
    WdfWaitLockRelease(c->Lock);
    WdfTimerStop(c->Timer,TRUE);
    if(c->Vhf) Submit(c,&zero);
    return STATUS_SUCCESS;
}
VOID Cleanup(WDFOBJECT object) {
    TPC_DEVICE_CONTEXT* c=GetDeviceContext(object);
    if(c->Lock) {
        WdfWaitLockAcquire(c->Lock,NULL);c->Online=FALSE;WdfWaitLockRelease(c->Lock);
    }
    if(c->Timer) WdfTimerStop(c->Timer,TRUE);
    if(c->Vhf) {VhfDelete(c->Vhf,TRUE);c->Vhf=NULL;}
}
NTSTATUS AddDevice(WDFDRIVER driver,PWDFDEVICE_INIT init) {
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES attrs, child;
    WDF_IO_QUEUE_CONFIG queue;
    WDF_TIMER_CONFIG timer;
    WDF_PNPPOWER_EVENT_CALLBACKS power;
    WDF_FILEOBJECT_CONFIG files;
    VHF_CONFIG vhf;
    TPC_DEVICE_CONTEXT* c;
    NTSTATUS status;
    DECLARE_CONST_UNICODE_STRING(name,L"\\Device\\TrackPadCAD");
    DECLARE_CONST_UNICODE_STRING(link,L"\\DosDevices\\TrackPadCAD");
    DECLARE_CONST_UNICODE_STRING(sddl,L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    UNREFERENCED_PARAMETER(driver);
    WdfDeviceInitSetDeviceType(init,0x8000);
    WdfDeviceInitSetExclusive(init,TRUE);
    WdfDeviceInitSetCharacteristics(init,FILE_DEVICE_SECURE_OPEN,TRUE);
    status=WdfDeviceInitAssignName(init,&name);if(!NT_SUCCESS(status))return status;
    status=WdfDeviceInitAssignSDDLString(init,&sddl);if(!NT_SUCCESS(status))return status;
    WDF_FILEOBJECT_CONFIG_INIT(&files,WDF_NO_EVENT_CALLBACK,WDF_NO_EVENT_CALLBACK,FileCleanup);
    WdfDeviceInitSetFileObjectConfig(init,&files,WDF_NO_OBJECT_ATTRIBUTES);
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&power);power.EvtDeviceD0Entry=D0Entry;power.EvtDeviceD0Exit=D0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(init,&power);
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attrs,TPC_DEVICE_CONTEXT);
    attrs.ExecutionLevel=WdfExecutionLevelPassive;attrs.EvtCleanupCallback=Cleanup;
    status=WdfDeviceCreate(&init,&attrs,&device);if(!NT_SUCCESS(status))return status;
    c=GetDeviceContext(device);
    WDF_OBJECT_ATTRIBUTES_INIT(&child);child.ParentObject=device;
    status=WdfWaitLockCreate(&child,&c->Lock);if(!NT_SUCCESS(status))return status;
    WDF_TIMER_CONFIG_INIT(&timer,Tick);timer.AutomaticSerialization=FALSE;
    status=WdfTimerCreate(&timer,&child,&c->Timer);if(!NT_SUCCESS(status))return status;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queue,WdfIoQueueDispatchSequential);queue.EvtIoDeviceControl=Ioctl;
    status=WdfIoQueueCreate(device,&queue,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);if(!NT_SUCCESS(status))return status;
    status=WdfDeviceCreateSymbolicLink(device,&link);if(!NT_SUCCESS(status))return status;
    VHF_CONFIG_INIT(&vhf,WdfDeviceWdmGetDeviceObject(device),sizeof(TPC_DESCRIPTOR),(PUCHAR)TPC_DESCRIPTOR);
    vhf.VendorID=0;vhf.ProductID=0;vhf.VersionNumber=1; // unassigned local experiment; no vendor spoofing
    status=VhfCreate(&vhf,&c->Vhf);if(!NT_SUCCESS(status))return status;
    return VhfStart(c->Vhf);
}
NTSTATUS DriverEntry(PDRIVER_OBJECT object,PUNICODE_STRING path) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,AddDevice);
    return WdfDriverCreate(object,path,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}



