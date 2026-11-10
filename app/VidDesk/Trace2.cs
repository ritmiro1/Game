using System;
using System.Diagnostics;

namespace VidDesk
{
    /// <summary>
    /// Writes a plain-text trace next to the user profile. Used to prove how far the
    /// process got even when no managed exception handler fires (loader failures,
    /// stack overflow, COM/AV faults, hard process termination).
    /// </summary>
    internal static class Trace2
    {
        public static bool Enabled
        {
            get
            {
                string v = Environment.GetEnvironmentVariable("VIDDESK_DEBUG");
                return v == "1";
            }
        }

        private static string _path;
        private static readonly object Gate = new object();

        public static string Path
        {
            get
            {
                if (_path == null) { _path = BuildPath(); }
                return _path;
            }
        }

        private static string BuildPath()
        {
            try
            {
                string dir = System.IO.Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VidDesk");
                System.IO.Directory.CreateDirectory(dir);
                return System.IO.Path.Combine(dir, "viddesk.log");
            }
            catch { return null; }
        }

        public static void Write(string msg)
        {
            string path = Path;
            if (path == null) { return; }
            lock (Gate)
            {
                try
                {
                    System.IO.File.AppendAllText(path,
                        DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + "  " + msg + Environment.NewLine);
                }
                catch { }
            }
        }

        public static void Step(string msg)
        {
            // Boot milestones are always traced: they are tiny and they are exactly what
            // is needed to diagnose "it closed immediately".
            Write(msg);
        }
    }
}
