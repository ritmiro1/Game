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
        public static string LastError { get; private set; }

        private const string MutexName = "Global\\VidDesk.SingleInstance.7d1c";

        [STAThread]
        private static void Main(string[] args)
        {
            AppDomain.CurrentDomain.UnhandledException += (s, e) =>
                Trace2.Write("FATAL unhandled: " + (e.ExceptionObject as Exception));
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += (s, e) => Fail(e.Exception, "Ошибка интерфейса");

            Trace2.Step("==== VidDesk start, pid " + Process.GetCurrentProcess().Id
                        + ", exe " + Application.ExecutablePath);

            bool created = false;
            Mutex mutex = null;
            try
            {
                mutex = new Mutex(true, MutexName, out created);
            }
            catch (Exception ex)
            {
                Trace2.Write("mutex failed: " + ex.Message);
                created = true;
            }

            if (!created)
            {
                Trace2.Write("another instance is already running");
                MessageBox.Show("VidDesk уже запущен — ищите его значок в области уведомлений.",
                    "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            try
            {
                Boot(args);
            }
            catch (Exception ex)
            {
                Fail(ex, "Не удалось запустить VidDesk");
            }
            finally
            {
                Trace2.Step("==== VidDesk exit");
                if (mutex != null)
                {
                    try { mutex.ReleaseMutex(); } catch { }
                    mutex.Dispose();
                }
            }
        }

        private static void Boot(string[] args)
        {
            Stopwatch sw = Stopwatch.StartNew();
            Splash splash = null;
            try
            {
                Trace2.Step("boot: runtime dir = " + (RuntimeBootstrap.RuntimeDir));

                if (RuntimeBootstrap.HasEmbeddedRuntime)
                {
                    splash = new Splash();
                    splash.Show();
                    splash.Report("Распаковка медиадвижка…");
                    Application.DoEvents();
                }

                Trace2.Step("boot: ensure runtime");
                RuntimePath = RuntimeBootstrap.EnsureRuntime(splash);
                RuntimeBootstrap.RegisterNativeSearchPath(RuntimePath);
                Trace2.Step("boot: runtime = " + (RuntimePath ?? "<none>"));

                State = SettingsStore.Load();
                Trace2.Step("boot: settings loaded, library=" + (State.Library == null ? -1 : State.Library.Count));

                try
                {
                    Engine = new VlcEngine();
                    Wallpaper = new WallpaperService(Engine);
                    Trace2.Step("boot: vlc " + VlcEngine.Version);
                }
                catch (Exception ex)
                {
                    // The UI must still open so the user can see what is wrong.
                    LastError = ex.Message;
                    Trace2.Write("boot: engine failed -> " + ex);
                }

                if (splash != null)
                {
                    splash.Report("Готово (" + (sw.ElapsedMilliseconds / 1000.0).ToString("0.0") + " с)");
                    splash.Close();
                    splash.Dispose();
                    splash = null;
                }

                Trace2.Step("boot: creating shell");
                using (TrayContext context = new TrayContext(args))
                {
                    Trace2.Step("boot: shell ready, message loop");
                    Application.Run(context);
                }

                Trace2.Step("boot: message loop finished");
                SettingsStore.Save(State);
                if (Wallpaper != null) { Wallpaper.Dispose(); }
                if (Engine != null) { Engine.Dispose(); }
            }
            catch (Exception ex)
            {
                if (splash != null && !splash.IsDisposed)
                {
                    try { splash.Close(); } catch { }
                }
                Trace2.Write("boot failed: " + ex);
                ShowFatal(ex);
            }
        }

        private static void Fail(Exception ex, string title)
        {
            Trace2.Write("unhandled: " + ex);
            ShowFatal(ex);
        }

        private static void ShowFatal(Exception ex)
        {
            try
            {
                StringBuilder sb = new StringBuilder();
                sb.AppendLine("VidDesk столкнулся с ошибкой.");
                sb.AppendLine();
                sb.AppendLine(ex == null ? "Неизвестная ошибка." : ex.Message);
                sb.AppendLine();
                sb.AppendLine("Движок: " + (RuntimePath ?? RuntimeBootstrap.RuntimeDir));
                sb.AppendLine("Лог: " + Trace2.Path);
                MessageBox.Show(sb.ToString(), "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            catch { }
        }

        public static string LogPath { get { return Trace2.Path; } }

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
            catch (Exception ex)
            {
                Trace2.Write("autostart failed: " + ex.Message);
            }
        }
    }
}
