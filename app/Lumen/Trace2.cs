using System;
using System.Diagnostics;
using System.Text;

namespace Lumen
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
                string v = Environment.GetEnvironmentVariable("LUMEN_DEBUG");
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
                    Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Lumen");
                System.IO.Directory.CreateDirectory(dir);
                return System.IO.Path.Combine(dir, "lumen.log");
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

        /// <summary>Compact exception dump: type, message, inner chain and the first frames.</summary>
        public static void Dump(Exception ex)
        {
            if (ex == null) { return; }
            StringBuilder sb = new StringBuilder();
            Exception cur = ex;
            int depth = 0;
            while (cur != null && depth < 5)
            {
                sb.AppendLine("  [" + depth + "] " + cur.GetType().FullName + ": " + cur.Message);
                string st = cur.StackTrace;
                if (!string.IsNullOrEmpty(st))
                {
                    string[] lines = st.Split(new[] { Environment.NewLine, "\n" }, StringSplitOptions.RemoveEmptyEntries);
                    for (int i = 0; i < lines.Length && i < 6; i++) { sb.AppendLine("      " + lines[i].Trim()); }
                }
                cur = cur.InnerException;
                depth++;
            }
            Write(sb.ToString());
        }

        public static void Step(string msg)
        {
            // Boot milestones are always traced: they are tiny and they are exactly what
            // is needed to diagnose "it closed immediately".
            Write(msg);
        }
    }
}
