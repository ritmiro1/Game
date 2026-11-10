using System;

namespace VidDesk
{
    /// <summary>Version data shown in the UI so a build can always be identified.</summary>
    internal static class AppInfo
    {
        public const string Version = "1.4.0";
        public const string Codename = "Ozone";

        public static string Title
        {
            get { return "VidDesk " + Version + " — живые обои для рабочего стола"; }
        }

        private static string _stamp;

        /// <summary>
        /// Build time read from the executable's own PE header — identifies exactly which
        /// binary is running without any build-time code generation.
        /// </summary>
        public static string BuildStamp
        {
            get
            {
                if (_stamp != null) { return _stamp; }
                _stamp = ReadBuildTime() ?? "unknown build";
                return _stamp;
            }
        }

        private static string ReadBuildTime()
        {
            try
            {
                string path = System.Windows.Forms.Application.ExecutablePath;
                using (System.IO.FileStream fs = new System.IO.FileStream(
                    path, System.IO.FileMode.Open, System.IO.FileAccess.Read, System.IO.FileShare.ReadWrite))
                {
                    byte[] head = new byte[1024];
                    if (fs.Read(head, 0, head.Length) < 64) { return null; }
                    int peOffset = BitConverter.ToInt32(head, 0x3C);
                    if (peOffset <= 0 || peOffset + 12 > head.Length) { return null; }
                    if (head[peOffset] != 'P' || head[peOffset + 1] != 'E') { return null; }
                    uint stamp = BitConverter.ToUInt32(head, peOffset + 8);
                    if (stamp == 0) { return null; }
                    DateTime when = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc).AddSeconds(stamp);
                    if (when.Year < 2020 || when > DateTime.UtcNow.AddDays(1)) { return null; }
                    return "build " + when.ToString("yyyy-MM-dd HH:mm") + " UTC";
                }
            }
            catch { return null; }
        }

        public static string Short
        {
            get { return "v" + Version + " (" + BuildStamp + ")"; }
        }
    }
}
