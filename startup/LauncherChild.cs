using System;
using System.IO;
using System.Text;
using System.Threading;
class LauncherChild {
    static int Main(string[] args) {
        using(var output=new StreamWriter(args[0],false,Encoding.UTF8)) {
            output.WriteLine(Environment.CurrentDirectory);
            output.WriteLine(Environment.GetEnvironmentVariable("RSM_FIXTURE_TOKEN"));
            foreach(string arg in args)output.WriteLine(Convert.ToBase64String(Encoding.UTF8.GetBytes(arg)));
        }
        Thread.Sleep(300);
        return 37;
    }
}
