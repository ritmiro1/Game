using System;

namespace VidDesk
{
    /// <summary>Version data shown in the UI so a build can always be identified.</summary>
    internal static class AppInfo
    {
        public const string Version = "1.2.0";
        public const string Codename = "Ozone";

        public static string Title
        {
            get { return "VidDesk " + Version + " — живые обои для рабочего стола"; }
        }

        public static string BuildStamp
        {
            get
            {
                return VidDesk.BuildStamp.Value;
            }
        }

        public static string Short
        {
            get { return "v" + Version + " (" + BuildStamp + ")"; }
        }
    }
}
