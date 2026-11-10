using System;
using System.IO;
using System.Threading;
using System.Windows.Forms;
using Microsoft.Win32;
using VidDesk.Native;
using VidDesk.Ui;

namespace VidDesk
{
    internal static class Program
    {
        public static AppState State { get; private set; }
        public static VlcEngine Engine { get; private set; }
        public static WallpaperService Wallpaper { get; private set; }

        private const string MutexName = "Global\\VidDesk.SingleInstance.7d1c";

        [STAThread]
        private static void Main(string[] args)
        {
            bool created;
            using (Mutex mutex = new Mutex(true, MutexName, out created))
            {
                if (!created)
                {
                    MessageBox.Show("VidDesk уже запущен — ищите его значок в области уведомлений.",
                        "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Information);
                    return;
                }

                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
                Application.ThreadException += (s, e) => LogCrash(e.Exception);
                AppDomain.CurrentDomain.UnhandledException += (s, e) => LogCrash(e.ExceptionObject as Exception);

                VlcApi.Load();
                State = SettingsStore.Load();
                Engine = new VlcEngine();
                Wallpaper = new WallpaperService(Engine);

                using (TrayContext context = new TrayContext(args))
                {
                    Application.Run(context);
                }

                SettingsStore.Save(State);
                Wallpaper.Dispose();
                Engine.Dispose();
            }
        }

        private static void LogCrash(Exception ex)
        {
            try
            {
                Directory.CreateDirectory(SettingsStore.DataFolder);
                File.AppendAllText(Path.Combine(SettingsStore.DataFolder, "crash.log"),
                    DateTime.Now + "  " + (ex == null ? "unknown" : ex.ToString()) + Environment.NewLine);
            }
            catch { }
        }

        public static void EnableAutoStart(bool enabled)
        {
            try
            {
                using (RegistryKey key = Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Windows\CurrentVersion\Run", true))
                {
                    if (key == null) { return; }
                    if (enabled)
                    {
                        key.SetValue("VidDesk", "\"" + Application.ExecutablePath + "\" --tray");
                    }
                    else if (key.GetValue("VidDesk") != null)
                    {
                        key.DeleteValue("VidDesk", false);
                    }
                }
            }
            catch { }
        }
    }
}
