using System;
using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Threading;
using HIDMaestro;

public static class HidMaestroBridge {
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr w, out uint pid);
    [DllImport("user32.dll")] static extern short GetAsyncKeyState(int key);
    static bool Allowed(string target) {
        if ((GetAsyncKeyState(0x77)&0x8000)==0) return false; // hold F8
        uint pid; GetWindowThreadProcessId(GetForegroundWindow(),out pid);
        try { using(var p=Process.GetProcessById((int)pid)) return (p.ProcessName+".exe").Equals(target,StringComparison.OrdinalIgnoreCase); }
        catch { return false; }
    }
    public static byte[] Encode(int id, int a=0,int b=0,int c=0) {
        byte[] r=new byte[7]; r[0]=(byte)id;
        int[] v={a,b,c}; for(int i=0;i<3;i++) { short n=(short)Math.Clamp(v[i],-350,350);r[1+i*2]=(byte)n;r[2+i*2]=(byte)(n>>8); }
        return r;
    }
    public static void Check() {
        var r=Encode(2,-350,80,350);
        if(r[0]!=2 || r[1]!=0xa2 || r[2]!=0xfe || r[3]!=80 || r[5]!=0x5e || r[6]!=1) throw new Exception("Report encoding failed");
        Console.WriteLine("Bridge compiled; signed report encoding verified.");
    }
    public static void Run(HMController controller,string exe,string target,int seconds,double sensitivity) {
        if(!double.IsFinite(sensitivity) || sensitivity<0.1 || sensitivity>50) throw new ArgumentOutOfRangeException(nameof(sensitivity));
        object sync=new object(); double[] pending=new double[6]; uint buttons=0;
        long last=0; bool ended=false,quit=false;
        var timer=Stopwatch.StartNew();
        var psi=new ProcessStartInfo(exe) { UseShellExecute=false,RedirectStandardOutput=true,CreateNoWindow=true };
        psi.ArgumentList.Add("live");psi.ArgumentList.Add(target);psi.ArgumentList.Add("--stream");
        using var producer=Process.Start(psi) ?? throw new Exception("Could not launch touchpad capture");
        var reader=new Thread(()=>{
            try {
                string line;
                while((line=producer.StandardOutput.ReadLine())!=null) {
                    if(!line.StartsWith("FRAME ")) { Console.WriteLine(line);continue; }
                    string[] fields=line.Split(' ');
                    if(fields.Length!=8)throw new Exception("Malformed capture frame");
                    int[] axes=new int[6];
                    for(int i=0;i<6;i++) { axes[i]=int.Parse(fields[i+1],CultureInfo.InvariantCulture);if(Math.Abs(axes[i])>350)throw new Exception("Out of range capture frame"); }
                    uint bits=uint.Parse(fields[7],CultureInfo.InvariantCulture);
                    lock(sync) {
                        if(Allowed(target)) {for(int i=0;i<6;i++)pending[i]+=axes[i];buttons |= bits&1;}
                        else {Array.Clear(pending);buttons=0;}
                        last=timer.ElapsedMilliseconds;
                    }
                }
            } catch(Exception e) {Console.Error.WriteLine("Capture: "+e.Message);}
            finally {lock(sync){ended=true;Array.Clear(pending);buttons=0;}}
        });
        reader.IsBackground=true;reader.Start();
        ConsoleCancelEventHandler cancel=(s,e)=>{e.Cancel=true;quit=true;};Console.CancelKeyPress+=cancel;
        Console.WriteLine("SpaceMouse bridge active for "+target+". Sensitivity: "+sensitivity+"x. Hold F8 to navigate; Ctrl+C stops.");
        int group=0;long[] previous={0,0,0};long sent=0;
        try {
            while(!quit && (seconds==0 || timer.ElapsedMilliseconds<seconds*1000L)) {
                byte[] report=Encode(group+1);bool stopped;
                lock(sync) {
                    stopped=ended;
                    long now=timer.ElapsedMilliseconds;
                    bool enabled=!ended && now-last<=80 && Allowed(target);
                    if(!enabled){Array.Clear(pending);buttons=0;}
                    if(group<2) {
                        // Convert accumulated 8ms rate pulses to the actual interval
                        // between reports of this group. Keep each report ID separate
                        // because HIDMaestro's shared memory stores the latest report.
                        double scale=sensitivity*8.0/Math.Max(8,now-previous[group]);int i=group*3;
                        report=Encode(group+1,(int)Math.Round(pending[i]*scale),(int)Math.Round(pending[i+1]*scale),(int)Math.Round(pending[i+2]*scale));
                        pending[i]=pending[i+1]=pending[i+2]=0;
                    } else {report[1]=(byte)buttons;buttons=0;}
                    previous[group]=now;
                }
                if(stopped)throw new Exception("Touchpad capture exited; bridge stopped.");
                controller.SubmitRawExtendedReport(report);sent++;group=(group+1)%3;Thread.Sleep(8);
            }
        } finally {
            Console.CancelKeyPress-=cancel;
            try {for(int id=1;id<=3;id++){controller.SubmitRawExtendedReport(Encode(id));Thread.Sleep(20);}}
            finally {if(!producer.HasExited)producer.Kill();producer.WaitForExit(3000);reader.Join(3000);}
            Console.WriteLine("Stopped; released axes and buttons. Reports submitted: "+sent);
        }
    }
}
