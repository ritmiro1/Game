using System;
using System.Diagnostics;
using System.IO;
using System.Text;
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
        public static string RuntimePath { get; private set; }

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
                Application.ThreadException += (s, e) => Fail(e.Exception, "Ошибка интерфейса");
                AppDomain.CurrentDomain.UnhandledException += (s, e) => Fail(e.ExceptionObject as Exception, "Критическая ошибка");

                try
                {
                    Boot(args);
                }
                catch (Exception ex)
                {
                    Fail(ex, "Не удалось запустить VidDesk");
                }
            }
        }

        private static void Boot(string[] args)
        {
            Stopwatch sw = Stopwatch.StartNew();
            Splash splash = null;
            try
            {
                if (RuntimeBootstrap.HasEmbeddedRuntime)
                {
                    splash = new Splash();
                    splash.Show();
                    splash.Report("Распаковка медиадвижка…");
                    Application.DoEvents();
                }

                RuntimePath = RuntimeBootstrap.EnsureRuntime(splash);
                RuntimeBootstrap.RegisterNativeSearchPath(RuntimePath);
                if (splash != null)
                {
                    splash.Report("Загрузка движка…");
                    Application.DoEvents();
                }

                State = SettingsStore.Load();
                Engine = new VlcEngine();
                Wallpaper = new WallpaperService(Engine);

                if (splash != null)
                {
                    splash.Report("Готово (" + (sw.ElapsedMilliseconds / 1000.0).ToString("0.0") + " с)");
                    splash.Close();
                    splash.Dispose();
                    splash = null;
                }

                using (TrayContext context = new TrayContext(args))
                {
                    Application.Run(context);
                }

                SettingsStore.Save(State);
                Wallpaper.Dispose();
                Engine.Dispose();
            }
            catch (Exception ex)
            {
                if (splash != null && !splash.IsDisposed)
                {
                    try { splash.Close(); } catch { }
                }
                Log("boot", ex);
                ShowFatal(ex);
            }
        }

        private static void Fail(Exception ex, string title)
        {
            Log("unhandled", ex);
            ShowFatal(ex);
        }

        private static void ShowFatal(Exception ex)
        {
            try
            {
                StringBuilder sb = new StringBuilder();
                sb.AppendLine("VidDesk не смог продолжить работу.");
                sb.AppendLine();
                sb.AppendLine(ex == null ? "Неизвестная ошибка." : ex.Message);
                sb.AppendLine();
                sb.AppendLine("Движок: " + (RuntimePath ?? RuntimeBootstrap.RuntimeDir));
                sb.AppendLine("Лог: " + LogPath);
                MessageBox.Show(sb.ToString(), "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch { }
        }

        public static string LogPath
        {
            get
            {
                try
                {
                    Directory.CreateDirectory(SettingsStore.DataFolder);
                    return Path.Combine(SettingsStore.DataFolder, "viddesk.log");
                }
                catch { return "—"; }
            }
        }

        internal static void Log(string tag, Exception ex)
        {
            try
            {
                Directory.CreateDirectory(SettingsStore.DataFolder);
                File.AppendAllText(Path.Combine(SettingsStore.DataFolder, "viddesk.log"),
                    DateTime.Now.ToString("u") + " [" + tag + "] "
                    + (ex == null ? "unknown" : ex.ToString()) + Environment.NewLine);
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
