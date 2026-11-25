using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;

// Steam launch wrapper: forwards the original game command without a shell.
// Only a temporary, per-process scheduling hint is changed. No injection.
class StartupLauncher {
    const string GameHash = "bb056959c0c6371d4ecf78f84c5d27e2fae93a9d0258830c2a36f33e6a2778eb";
    const int PowerThrottlingClass = 4;
    const int StartupMilliseconds = 50000;
    [StructLayout(LayoutKind.Sequential)]
    struct PowerState { public uint Version, ControlMask, StateMask; }
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool GetProcessInformation(IntPtr process, int kind, ref PowerState state, uint size);
    [DllImport("kernel32.dll", SetLastError=true)]
    static extern bool SetProcessInformation(IntPtr process, int kind, ref PowerState state, uint size);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr CommandLineToArgvW(string command, out int count);
    [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr memory);
    static string logPath;
    static void Log(string message) {
        try { File.AppendAllText(logPath, DateTime.UtcNow.ToString("o") + " " + message + Environment.NewLine); }
        catch { /* Log failure must not prevent an ordinary game launch. */ }
    }
    static PowerState Read(IntPtr process) {
        var state = new PowerState { Version=1 };
        if (!GetProcessInformation(process, PowerThrottlingClass, ref state, 12)) throw new Win32Exception();
        return state;
    }
    static void Write(IntPtr process, PowerState state) {
        if (!SetProcessInformation(process, PowerThrottlingClass, ref state, 12)) throw new Win32Exception();
    }
    static bool Same(PowerState a, PowerState b) {
        return a.ControlMask == b.ControlMask && a.StateMask == b.StateMask;
    }
    // A failed scheduling operation must never shorten Steam's launch lifetime.
    internal static int TrackLifetime(Func<int, bool> wait, Action restore, Func<int> exitCode, Action<string> log) {
        if (!wait(StartupMilliseconds)) {
            try { restore(); }
            catch (Exception e) { log("restore_error " + e.Message); }
        }
        wait(-1);
        return exitCode();
    }
    internal static string GamePath(string command) {
        string path=Path.GetFullPath(command);
        if (!String.Equals(Path.GetFileName(path),"Rocksmith2014.exe",StringComparison.OrdinalIgnoreCase) || !File.Exists(path))
            throw new ArgumentException("Expected Steam's existing Rocksmith2014.exe command");
        return path;
    }
    // Windows CRT quoting, including quotes and trailing backslashes.
    internal static string Quote(string arg) {
        var result = new StringBuilder("\"");
        int slashes=0;
        foreach (char ch in arg) {
            if (ch=='\\') { slashes++; continue; }
            result.Append('\\', ch=='"' ? slashes*2+1 : slashes);
            result.Append(ch); slashes=0;
        }
        result.Append('\\', slashes*2);
        return result.Append('"').ToString();
    }
    static void SelfTest() {
        var cases = new [] { "fixture.exe", "", "a b", "a\"b", @"C:\with spaces\", @"plain\tail", "μ test" };
        int count;
        IntPtr parsed=CommandLineToArgvW(String.Join(" ",cases.Select(Quote)),out count);
        if (parsed==IntPtr.Zero) throw new Win32Exception();
        try {
            if (count!=cases.Length) throw new Exception("Argument count mismatch");
            for (int i=0;i<count;i++)
                if (Marshal.PtrToStringUni(Marshal.ReadIntPtr(parsed,i*IntPtr.Size))!=cases[i])
                    throw new Exception("Argument round-trip mismatch at "+i);
        } finally { LocalFree(parsed); }
        using (var self=Process.GetCurrentProcess()) {
            var original=Read(self.Handle);
            try {
                Write(self.Handle,new PowerState {Version=1,ControlMask=original.ControlMask|1,StateMask=original.StateMask&~1u});
                var actual=Read(self.Handle);
                if ((actual.ControlMask&1)==0 || (actual.StateMask&1)!=0) throw new Exception("HighQoS readback failed");
            } finally { Write(self.Handle,original); }
            if (!Same(original,Read(self.Handle))) throw new Exception("Restore readback failed");
        }
        Log("SELF_TEST_PASS argument round-trip; HighQoS set/read/restore on self");
    }
    static int Main(string[] args) {
        string root=Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"RocksmithModernized","launcher-logs");
        try {
            Directory.CreateDirectory(root);
            logPath=Path.Combine(root,DateTime.UtcNow.ToString("yyyyMMdd-HHmmss-fffffff")+"-"+Process.GetCurrentProcess().Id+".log");
        } catch { logPath=null; }
        Process child=null;
        PowerState original=new PowerState();
        bool changed=false;
        try {
            if (args.Length==1 && args[0]=="--self-test") { SelfTest(); return 0; }
            if (args.Length==0) throw new ArgumentException("Expected Steam's original Rocksmith executable command");
            string game=GamePath(args[0]);
            bool supported=false;
            try {
                using (var sha=SHA256.Create()) using (var file=File.OpenRead(game))
                    supported=BitConverter.ToString(sha.ComputeHash(file)).Replace("-","").ToLowerInvariant()==GameHash;
            } catch (Exception e) { Log("version guard unavailable: "+e.Message); }
            // Steam's environment, original arguments and game working directory are preserved.
            var startInfo=new ProcessStartInfo(game,String.Join(" ",args.Skip(1).Select(Quote))) {
                UseShellExecute=false,WorkingDirectory=Path.GetDirectoryName(game)
            };
            // Avoid competing restorations if the DLL's early hint is enabled.
            startInfo.EnvironmentVariables["RSM_STARTUP_HELPER_ACTIVE"]=supported?"1":"0";
            child=Process.Start(startInfo);
            Log("child_started pid="+child.Id+" supported="+supported+" SteamAppId="+Environment.GetEnvironmentVariable("SteamAppId"));
            if (supported) {
                try {
                    original=Read(child.Handle);
                    Write(child.Handle,new PowerState {Version=1,ControlMask=original.ControlMask|1,StateMask=original.StateMask&~1u});
                    changed=true;
                    var actual=Read(child.Handle);
                    if ((actual.ControlMask&1)==0 || (actual.StateMask&1)!=0) throw new Exception("HighQoS readback mismatch");
                    Log("high_qos_verified original_control="+original.ControlMask+" original_state="+original.StateMask);
                } catch (Exception e) { Log("scheduling hint unavailable: "+e.Message); }
            }
            int result=TrackLifetime(
                milliseconds => { if (milliseconds<0) { child.WaitForExit(); return true; } return child.WaitForExit(milliseconds); },
                () => {
                    if (!changed) return;
                    Write(child.Handle,original);
                    if (!Same(original,Read(child.Handle))) throw new Exception("Restore readback mismatch");
                    changed=false;
                    Log("original_qos_restored_and_verified");
                },
                () => child.ExitCode, Log);
            Log("game_exited code="+child.ExitCode);
            return result;
        } catch (Exception e) {
            Log("launcher_error "+e.ToString());
            return 1;
        } finally {
            if (child!=null) {
                try {
                    if (changed && !child.HasExited) {
                        Write(child.Handle,original);
                        Log("finally_restore_verified="+Same(original,Read(child.Handle)));
                    }
                } catch (Exception e) { Log("restore_error "+e.Message); }
                child.Dispose();
            }
        }
    }
}
