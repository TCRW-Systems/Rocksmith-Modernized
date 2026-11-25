using System;
using System.Collections.Generic;
using System.IO;

class LauncherSmoke {
    static void Check(bool condition) { if (!condition) throw new Exception("Fixture assertion failed"); }
    static int Main() {
        var waits=new List<int>();
        int errors=0;
        int code=StartupLauncher.TrackLifetime(
            timeout => { waits.Add(timeout); return timeout<0; },
            () => { throw new IOException("simulated restore failure"); },
            () => 37, message => { Check(message.Contains("simulated restore failure")); errors++; });
        Check(code==37 && errors==1 && waits.Count==2 && waits[0]==50000 && waits[1]==-1);
        waits.Clear();
        code=StartupLauncher.TrackLifetime(timeout => { waits.Add(timeout); return true; },
            () => { throw new Exception("Restore called on an exited process"); }, () => 12, message => {});
        Check(code==12 && waits.Count==2);
        string root=Path.Combine(Path.GetTempPath(),"Rocksmith fixture μ "+Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try {
            string game=Path.Combine(root,"Rocksmith2014.exe");
            File.WriteAllText(game,"fixture");
            Check(StartupLauncher.GamePath(game)==game);
            bool rejected=false;
            try { StartupLauncher.GamePath(Path.Combine(root,"missing.exe")); } catch(ArgumentException) { rejected=true; }
            Check(rejected);
        } finally { Directory.Delete(root,true); }
        Console.WriteLine("PASS: restore failure preserves child wait and exit code; early exit; arbitrary Unicode game path");
        return 0;
    }
}
