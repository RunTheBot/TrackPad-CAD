using System;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using HIDMaestro;

public static class HidMaestroBridge {
    [DllImport("user32.dll")]static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    static bool NativeTargetActive(string[] targets){uint pid;GetWindowThreadProcessId(GetForegroundWindow(),out pid);try{using var p=Process.GetProcessById((int)pid);string exe=p.ProcessName+".exe";foreach(string target in targets)if(exe.Equals(target,StringComparison.OrdinalIgnoreCase))return true;}catch{}return false;}
    public static byte[] Encode(int id,int a=0,int b=0,int c=0) {
        byte[] r=new byte[7];r[0]=(byte)id;int[] v={a,b,c};
        for(int i=0;i<3;i++){short n=(short)Math.Clamp(v[i],-350,350);r[1+i*2]=(byte)n;r[2+i*2]=(byte)(n>>8);}return r;
    }
    public static void SetTouchpadGestures(bool pan,bool zoom){TouchpadOverride.SetGestures(pan,zoom);}
    public static void Check(){var r=Encode(2,-350,80,350);if(r[0]!=2||r[1]!=0xa2||r[2]!=0xfe||r[3]!=80||r[5]!=0x5e||r[6]!=1)throw new Exception("Report encoding failed");bool pan,zoom;if(!TouchpadOverride.Probe(out pan,out zoom))throw new Exception("Windows 11 touchpad settings API is unavailable (error "+Marshal.GetLastWin32Error()+").");Console.WriteLine("Bridge compiled; HID encoding and dynamic touchpad API verified (pan="+pan+", zoom="+zoom+").");}

    sealed class TouchpadOverride:IDisposable {
        const uint SPI_GETTOUCHPADPARAMETERS=0x00AE,SPI_SETTOUCHPADPARAMETERS=0x00AF,VERSION_1=1,SPIF_SENDCHANGE=2;
        const uint PAN_ENABLED=1u<<7,ZOOM_ENABLED=1u<<8;
        [StructLayout(LayoutKind.Sequential)]struct Parameters {
            public uint versionNumber,maxSupportedContacts,legacyTouchpadFeatures,deviceFlags,userFlags;
            public uint sensitivityLevel,cursorSpeed,feedbackIntensity,clickForceSensitivity,rightClickZoneWidth,rightClickZoneHeight;
        }
        [DllImport("user32.dll",EntryPoint="SystemParametersInfoW",SetLastError=true)]
        static extern bool SystemParametersInfo(uint action,uint size,ref Parameters value,uint flags);
        Parameters saved;bool overridden;bool unavailable;
        static bool Get(ref Parameters p){p.versionNumber=VERSION_1;return SystemParametersInfo(SPI_GETTOUCHPADPARAMETERS,(uint)Marshal.SizeOf<Parameters>(),ref p,0);}
        static bool Set(ref Parameters p){return SystemParametersInfo(SPI_SETTOUCHPADPARAMETERS,(uint)Marshal.SizeOf<Parameters>(),ref p,SPIF_SENDCHANGE);}
        public static bool Probe(out bool pan,out bool zoom){var p=new Parameters();bool ok=Get(ref p);pan=ok&&(p.userFlags&PAN_ENABLED)!=0;zoom=ok&&(p.userFlags&ZOOM_ENABLED)!=0;return ok;}
        public static void SetGestures(bool pan,bool zoom){var p=new Parameters();if(!Get(ref p))throw new Exception("Could not read Windows touchpad settings (error "+Marshal.GetLastWin32Error()+").");p.userFlags=(p.userFlags&~(PAN_ENABLED|ZOOM_ENABLED))|(pan?PAN_ENABLED:0)|(zoom?ZOOM_ENABLED:0);if(!Set(ref p))throw new Exception("Could not set Windows touchpad settings (error "+Marshal.GetLastWin32Error()+").");}
        public void Update(bool shouldSuppress){
            if(unavailable||shouldSuppress==overridden)return;
            if(shouldSuppress){
                var current=new Parameters();
                if(!Get(ref current)){unavailable=true;Console.Error.WriteLine("Windows touchpad override unavailable (error "+Marshal.GetLastWin32Error()+").");return;}
                saved=current;current.userFlags&=~(PAN_ENABLED|ZOOM_ENABLED);
                if(!Set(ref current)){unavailable=true;Console.Error.WriteLine("Could not suppress Windows touchpad gestures (error "+Marshal.GetLastWin32Error()+").");return;}
                overridden=true;
            }else Restore();
        }
        void Restore(){if(!overridden)return;var original=saved;if(!Set(ref original))Console.Error.WriteLine("Could not restore Windows touchpad settings (error "+Marshal.GetLastWin32Error()+").");else overridden=false;}
        public void Dispose(){Restore();}
    }

    sealed class WebSdkHook:IDisposable {
        readonly TcpListener listener;readonly Thread thread;readonly object stateLock=new object();volatile bool stopping;long lastHeartbeat;bool sdk;string url="";
        public WebSdkHook(int port){listener=new TcpListener(IPAddress.Loopback,port);listener.Start();thread=new Thread(Listen){IsBackground=true,Name="TrackPad CAD web SDK hook"};thread.Start();}
        public bool IsSdkActive(string[] prefixes){lock(stateLock){if(Environment.TickCount64-lastHeartbeat>=1000||!sdk)return false;if(prefixes.Length==0)return true;foreach(string prefix in prefixes)if(url.StartsWith(prefix,StringComparison.OrdinalIgnoreCase))return true;return false;}}
        void Listen(){while(!stopping){try{using var client=listener.AcceptTcpClient();client.ReceiveTimeout=500;using var stream=client.GetStream();using var reader=new StreamReader(stream,System.Text.Encoding.ASCII,false,1024,true);string request=reader.ReadLine()??"";if(request.StartsWith("POST /page?",StringComparison.Ordinal)){int end=request.IndexOf(' ',5);string query=end>0?request.Substring(11,end-11):"";string newUrl="";bool newSdk=false;foreach(string part in query.Split('&')){int equals=part.IndexOf('=');if(equals<0)continue;string key=part.Substring(0,equals),value=Uri.UnescapeDataString(part.Substring(equals+1));if(key=="sdk")newSdk=value=="1";else if(key=="url")newUrl=value;}lock(stateLock){sdk=newSdk;url=newUrl;lastHeartbeat=Environment.TickCount64;}}string response="HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Private-Network: true\r\nAccess-Control-Allow-Methods: POST, OPTIONS\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";byte[] bytes=System.Text.Encoding.ASCII.GetBytes(response);stream.Write(bytes,0,bytes.Length);}catch(Exception){if(!stopping)Thread.Sleep(50);}}}
        public void Dispose(){stopping=true;listener.Stop();thread.Join(1000);}
    }

    sealed class TrayHost:NativeWindow,IDisposable {
        const int WM_HOTKEY=0x0312,HOTKEY_ID=0x5443,MOD_NOREPEAT=0x4000;
        [DllImport("user32.dll",SetLastError=true)]static extern bool RegisterHotKey(IntPtr h,int id,uint mods,uint key);
        [DllImport("user32.dll")]static extern bool UnregisterHotKey(IntPtr h,int id);
        [DllImport("user32.dll")]static extern bool DestroyIcon(IntPtr icon);
        readonly Action quit;readonly NotifyIcon tray;readonly ToolStripMenuItem toggleItem;volatile bool enabled;
        public bool Enabled=>enabled;
        public TrayHost(Action quit,bool startEnabled){this.quit=quit;enabled=startEnabled;CreateHandle(new CreateParams{Caption="TrackPad CAD hotkey"});
            if(!RegisterHotKey(Handle,HOTKEY_ID,MOD_NOREPEAT,0x77))throw new InvalidOperationException("F8 is already registered by another app.");
            toggleItem=new ToolStripMenuItem("Enable",null,(s,e)=>Toggle());var menu=new ContextMenuStrip();menu.Items.Add(toggleItem);menu.Items.Add("Exit",null,(s,e)=>quit());
            tray=new NotifyIcon{ContextMenuStrip=menu,Visible=true,Text="TrackPad CAD — disabled",Icon=MakeIcon(false)};tray.DoubleClick+=(s,e)=>Toggle();UpdateUi();}
        static Icon MakeIcon(bool on){using var bmp=new Bitmap(32,32);using(var g=Graphics.FromImage(bmp)){g.SmoothingMode=System.Drawing.Drawing2D.SmoothingMode.AntiAlias;g.Clear(Color.Transparent);using var b=new SolidBrush(on?Color.FromArgb(45,190,85):Color.FromArgb(115,115,115));using var p=new Pen(Color.White,2);g.FillEllipse(b,3,3,26,26);g.DrawEllipse(p,3,3,26,26);g.DrawLine(p,10,16,22,16);g.DrawLine(p,16,10,16,22);}IntPtr h=bmp.GetHicon();try{using var temp=Icon.FromHandle(h);return (Icon)temp.Clone();}finally{DestroyIcon(h);}}
        void Toggle(){enabled=!enabled;UpdateUi();System.Media.SystemSounds.Asterisk.Play();}
        void UpdateUi(){toggleItem.Text=enabled?"Disable":"Enable";tray.Text=enabled?"TrackPad CAD — enabled":"TrackPad CAD — disabled";var old=tray.Icon;tray.Icon=MakeIcon(enabled);old?.Dispose();}
        protected override void WndProc(ref Message m){if(m.Msg==WM_HOTKEY&&m.WParam==(IntPtr)HOTKEY_ID)Toggle();base.WndProc(ref m);}
        public void Dispose(){tray.Visible=false;tray.Dispose();UnregisterHotKey(Handle,HOTKEY_ID);DestroyHandle();}
    }

    public static void Run(HMController controller,string exe,string target,int seconds,double sensitivity,bool startEnabled,string[] nativeExecutables,string[] browserUrlPrefixes){
        if(!double.IsFinite(sensitivity)||sensitivity<0.1||sensitivity>50)throw new ArgumentOutOfRangeException(nameof(sensitivity));
        object sync=new object();double[] pending=new double[6];uint buttons=0;long last=0;bool ended=false,quit=false;int routeActive=0;TrayHost host=null;Exception uiError=null;var ready=new ManualResetEventSlim(false);using var webHook=new WebSdkHook(17831);
        var ui=new Thread(()=>{try{using(host=new TrayHost(()=>{quit=true;Application.ExitThread();},startEnabled)){ready.Set();Application.Run();}}catch(Exception e){uiError=e;ready.Set();quit=true;}});ui.SetApartmentState(ApartmentState.STA);ui.IsBackground=true;ui.Start();ready.Wait();if(uiError!=null)throw uiError;
        var timer=Stopwatch.StartNew();var psi=new ProcessStartInfo(exe){UseShellExecute=false,RedirectStandardOutput=true,CreateNoWindow=true};psi.ArgumentList.Add("--capture");psi.ArgumentList.Add("live");psi.ArgumentList.Add(target);psi.ArgumentList.Add("--stream");using var producer=Process.Start(psi)??throw new Exception("Could not launch touchpad capture");
        var reader=new Thread(()=>{try{string line;while((line=producer.StandardOutput.ReadLine())!=null){if(!line.StartsWith("FRAME ")){Console.WriteLine(line);continue;}string[] f=line.Split(' ');if(f.Length!=8)throw new Exception("Malformed capture frame");int[] axes=new int[6];for(int i=0;i<6;i++){axes[i]=int.Parse(f[i+1],CultureInfo.InvariantCulture);if(Math.Abs(axes[i])>350)throw new Exception("Out of range frame");}uint bits=uint.Parse(f[7],CultureInfo.InvariantCulture);lock(sync){if(host.Enabled&&Volatile.Read(ref routeActive)!=0){for(int i=0;i<6;i++)pending[i]+=axes[i];buttons|=bits&1;}else{Array.Clear(pending);buttons=0;}last=timer.ElapsedMilliseconds;}}}catch(Exception e){Console.Error.WriteLine("Capture: "+e.Message);}finally{lock(sync){ended=true;Array.Clear(pending);buttons=0;}}});reader.IsBackground=true;reader.Start();
        ConsoleCancelEventHandler cancel=(s,e)=>{e.Cancel=true;quit=true;Application.Exit();};Console.CancelKeyPress+=cancel;Console.WriteLine("TrackPad CAD is in the tray at "+sensitivity+"x. 3DxWare SDK routing is detected automatically; F8 toggles; Ctrl+C stops.");int group=0;long[] previous={0,0,0};long sent=0;
        using var touchpadOverride=new TouchpadOverride();
        try{while(!quit&&(seconds==0||timer.ElapsedMilliseconds<seconds*1000L)){byte[] report=Encode(group+1);bool stopped;bool inTarget=host.Enabled&&(webHook.IsSdkActive(browserUrlPrefixes)||NativeTargetActive(nativeExecutables));Volatile.Write(ref routeActive,inTarget?1:0);touchpadOverride.Update(inTarget);lock(sync){stopped=ended;long now=timer.ElapsedMilliseconds;bool active=inTarget&&now-last<=80;if(!active){Array.Clear(pending);buttons=0;}if(group<2){double scale=sensitivity*8.0/Math.Max(8,now-previous[group]);int i=group*3;report=Encode(group+1,(int)Math.Round(pending[i]*scale),(int)Math.Round(pending[i+1]*scale),(int)Math.Round(pending[i+2]*scale));pending[i]=pending[i+1]=pending[i+2]=0;}else{report[1]=(byte)buttons;buttons=0;}previous[group]=now;}if(stopped)throw new Exception("Touchpad capture exited; bridge stopped.");controller.SubmitRawExtendedReport(report);sent++;group=(group+1)%3;Thread.Sleep(8);}}
        finally{touchpadOverride.Update(false);Console.CancelKeyPress-=cancel;try{for(int id=1;id<=3;id++){controller.SubmitRawExtendedReport(Encode(id));Thread.Sleep(20);}}finally{quit=true;Application.Exit();if(!producer.HasExited)producer.Kill();producer.WaitForExit(3000);reader.Join(3000);ui.Join(3000);}Console.WriteLine("Stopped; released axes and buttons. Reports submitted: "+sent);}
    }
}
