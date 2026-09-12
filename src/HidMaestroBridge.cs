using System;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;
using HIDMaestro;

public static class HidMaestroBridge {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
    static bool TargetActive(string target) {
        uint pid;GetWindowThreadProcessId(GetForegroundWindow(),out pid);
        try {using(var p=Process.GetProcessById((int)pid))return (p.ProcessName+".exe").Equals(target,StringComparison.OrdinalIgnoreCase);}catch{return false;}
    }
    public static byte[] Encode(int id,int a=0,int b=0,int c=0) {
        byte[] r=new byte[7];r[0]=(byte)id;int[] v={a,b,c};
        for(int i=0;i<3;i++){short n=(short)Math.Clamp(v[i],-350,350);r[1+i*2]=(byte)n;r[2+i*2]=(byte)(n>>8);}return r;
    }
    public static void Check(){var r=Encode(2,-350,80,350);if(r[0]!=2||r[1]!=0xa2||r[2]!=0xfe||r[3]!=80||r[5]!=0x5e||r[6]!=1)throw new Exception("Report encoding failed");Console.WriteLine("Bridge compiled; signed report encoding verified.");}

    sealed class TrayHost:NativeWindow,IDisposable {
        const int WM_HOTKEY=0x0312,WM_MOUSEWHEEL=0x020A,WM_MOUSEHWHEEL=0x020E,WH_MOUSE_LL=14,HOTKEY_ID=0x5443,MOD_NOREPEAT=0x4000;
        [DllImport("user32.dll",SetLastError=true)]static extern bool RegisterHotKey(IntPtr h,int id,uint mods,uint key);
        [DllImport("user32.dll")]static extern bool UnregisterHotKey(IntPtr h,int id);
        delegate IntPtr HookProc(int code,IntPtr wp,IntPtr lp);
        [DllImport("user32.dll",SetLastError=true)]static extern IntPtr SetWindowsHookEx(int id,HookProc fn,IntPtr module,uint thread);
        [DllImport("user32.dll")]static extern bool UnhookWindowsHookEx(IntPtr hook);
        [DllImport("user32.dll")]static extern IntPtr CallNextHookEx(IntPtr hook,int code,IntPtr wp,IntPtr lp);
        [DllImport("user32.dll")]static extern bool DestroyIcon(IntPtr icon);
        readonly string target;readonly Action quit;readonly NotifyIcon tray;readonly ToolStripMenuItem toggleItem;readonly HookProc hookProc;IntPtr hook;volatile bool enabled;
        public bool Enabled=>enabled;
        public TrayHost(string target,Action quit){this.target=target;this.quit=quit;CreateHandle(new CreateParams{Caption="TrackPad CAD hotkey"});
            if(!RegisterHotKey(Handle,HOTKEY_ID,MOD_NOREPEAT,0x77))throw new InvalidOperationException("F8 is already registered by another app.");
            hookProc=MouseHook;hook=SetWindowsHookEx(WH_MOUSE_LL,hookProc,IntPtr.Zero,0);if(hook==IntPtr.Zero)throw new InvalidOperationException("Could not install gesture suppression hook.");
            toggleItem=new ToolStripMenuItem("Enable",null,(s,e)=>Toggle());var menu=new ContextMenuStrip();menu.Items.Add(toggleItem);menu.Items.Add("Exit",null,(s,e)=>quit());
            tray=new NotifyIcon{ContextMenuStrip=menu,Visible=true,Text="TrackPad CAD — disabled",Icon=MakeIcon(false)};tray.DoubleClick+=(s,e)=>Toggle();UpdateUi();}
        static Icon MakeIcon(bool on){using var bmp=new Bitmap(32,32);using(var g=Graphics.FromImage(bmp)){g.SmoothingMode=System.Drawing.Drawing2D.SmoothingMode.AntiAlias;g.Clear(Color.Transparent);using var b=new SolidBrush(on?Color.FromArgb(45,190,85):Color.FromArgb(115,115,115));using var p=new Pen(Color.White,2);g.FillEllipse(b,3,3,26,26);g.DrawEllipse(p,3,3,26,26);g.DrawLine(p,10,16,22,16);g.DrawLine(p,16,10,16,22);}IntPtr h=bmp.GetHicon();try{using var temp=Icon.FromHandle(h);return (Icon)temp.Clone();}finally{DestroyIcon(h);}}
        void Toggle(){enabled=!enabled;UpdateUi();System.Media.SystemSounds.Asterisk.Play();}
        void UpdateUi(){toggleItem.Text=enabled?"Disable":"Enable";tray.Text=enabled?"TrackPad CAD — enabled":"TrackPad CAD — disabled";var old=tray.Icon;tray.Icon=MakeIcon(enabled);old?.Dispose();}
        IntPtr MouseHook(int code,IntPtr wp,IntPtr lp){if(code>=0&&enabled&&TargetActive(target)&&(wp==(IntPtr)WM_MOUSEWHEEL||wp==(IntPtr)WM_MOUSEHWHEEL))return (IntPtr)1;return CallNextHookEx(hook,code,wp,lp);}
        protected override void WndProc(ref Message m){if(m.Msg==WM_HOTKEY&&m.WParam==(IntPtr)HOTKEY_ID)Toggle();base.WndProc(ref m);}
        public void Dispose(){tray.Visible=false;tray.Dispose();if(hook!=IntPtr.Zero)UnhookWindowsHookEx(hook);UnregisterHotKey(Handle,HOTKEY_ID);DestroyHandle();}
    }

    public static void Run(HMController controller,string exe,string target,int seconds,double sensitivity){
        if(!double.IsFinite(sensitivity)||sensitivity<0.1||sensitivity>50)throw new ArgumentOutOfRangeException(nameof(sensitivity));
        object sync=new object();double[] pending=new double[6];uint buttons=0;long last=0;bool ended=false,quit=false;TrayHost host=null;Exception uiError=null;var ready=new ManualResetEventSlim(false);
        var ui=new Thread(()=>{try{using(host=new TrayHost(target,()=>{quit=true;Application.ExitThread();})){ready.Set();Application.Run();}}catch(Exception e){uiError=e;ready.Set();quit=true;}});ui.SetApartmentState(ApartmentState.STA);ui.IsBackground=true;ui.Start();ready.Wait();if(uiError!=null)throw uiError;
        var timer=Stopwatch.StartNew();var psi=new ProcessStartInfo(exe){UseShellExecute=false,RedirectStandardOutput=true,CreateNoWindow=true};psi.ArgumentList.Add("live");psi.ArgumentList.Add(target);psi.ArgumentList.Add("--stream");using var producer=Process.Start(psi)??throw new Exception("Could not launch touchpad capture");
        var reader=new Thread(()=>{try{string line;while((line=producer.StandardOutput.ReadLine())!=null){if(!line.StartsWith("FRAME ")){Console.WriteLine(line);continue;}string[] f=line.Split(' ');if(f.Length!=8)throw new Exception("Malformed capture frame");int[] axes=new int[6];for(int i=0;i<6;i++){axes[i]=int.Parse(f[i+1],CultureInfo.InvariantCulture);if(Math.Abs(axes[i])>350)throw new Exception("Out of range frame");}uint bits=uint.Parse(f[7],CultureInfo.InvariantCulture);lock(sync){if(host.Enabled&&TargetActive(target)){for(int i=0;i<6;i++)pending[i]+=axes[i];buttons|=bits&1;}else{Array.Clear(pending);buttons=0;}last=timer.ElapsedMilliseconds;}}}catch(Exception e){Console.Error.WriteLine("Capture: "+e.Message);}finally{lock(sync){ended=true;Array.Clear(pending);buttons=0;}}});reader.IsBackground=true;reader.Start();
        ConsoleCancelEventHandler cancel=(s,e)=>{e.Cancel=true;quit=true;Application.Exit();};Console.CancelKeyPress+=cancel;Console.WriteLine("TrackPad CAD is in the tray for "+target+" at "+sensitivity+"x. F8 toggles; Ctrl+C stops.");int group=0;long[] previous={0,0,0};long sent=0;
        try{while(!quit&&(seconds==0||timer.ElapsedMilliseconds<seconds*1000L)){byte[] report=Encode(group+1);bool stopped;lock(sync){stopped=ended;long now=timer.ElapsedMilliseconds;bool active=host.Enabled&&TargetActive(target)&&now-last<=80;if(!active){Array.Clear(pending);buttons=0;}if(group<2){double scale=sensitivity*8.0/Math.Max(8,now-previous[group]);int i=group*3;report=Encode(group+1,(int)Math.Round(pending[i]*scale),(int)Math.Round(pending[i+1]*scale),(int)Math.Round(pending[i+2]*scale));pending[i]=pending[i+1]=pending[i+2]=0;}else{report[1]=(byte)buttons;buttons=0;}previous[group]=now;}if(stopped)throw new Exception("Touchpad capture exited; bridge stopped.");controller.SubmitRawExtendedReport(report);sent++;group=(group+1)%3;Thread.Sleep(8);}}
        finally{Console.CancelKeyPress-=cancel;try{for(int id=1;id<=3;id++){controller.SubmitRawExtendedReport(Encode(id));Thread.Sleep(20);}}finally{quit=true;Application.Exit();if(!producer.HasExited)producer.Kill();producer.WaitForExit(3000);reader.Join(3000);ui.Join(3000);}Console.WriteLine("Stopped; released axes and buttons. Reports submitted: "+sent);}
    }
}
