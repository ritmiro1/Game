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
            if (HasFlag(args, EngineProbe.Flag))
            {
                Environment.ExitCode = EngineProbe.RunChild();
                return;
            }

            bool safe = HasFlag(args, "--safe");

            AppDomain.CurrentDomain.UnhandledException += (s, e) =>
            {
                Exception ex = e.ExceptionObject as Exception;
                Trace2.Write("FATAL unhandled (terminating=" + e.IsTerminating + "): " + ex);
                if (ex != null)
                {
                    Trace2.Dump(ex);
                    if (e.IsTerminating) { EmergencyBox(ex, "при аварийном завершении"); }
                }
            };
            AppDomain.CurrentDomain.FirstChanceException += (s, e) =>
            {
                string txt = e.Exception == null ? "" : e.Exception.ToString();
                if (txt.IndexOf("libvlc", StringComparison.OrdinalIgnoreCase) >= 0
                    || txt.IndexOf("DllNotFound", StringComparison.OrdinalIgnoreCase) >= 0
                    || txt.IndexOf("EntryPointNotFound", StringComparison.OrdinalIgnoreCase) >= 0
                    || txt.IndexOf("BadImageFormat", StringComparison.OrdinalIgnoreCase) >= 0)
                {
                    Trace2.Write("native fault: " + txt);
                }
            };
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            Application.ThreadException += (s, e) => Fail(e.Exception, "Ошибка интерфейса");

            Trace2.Step("==== VidDesk " + AppInfo.Short + " start, pid " + Process.GetCurrentProcess().Id
                        + ", exe " + Application.ExecutablePath
                        + ", clr " + Environment.Version
                        + ", os " + Environment.OSVersion
                        + ", 64bit " + Environment.Is64BitProcess
                        + ", dir " + Environment.CurrentDirectory);

            if (HasFlag(args, "--selftest"))
            {
                RunSelfTest(args);
                return;
            }


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
                Trace2.Write("another instance is already running, forwarding to it");
                return;
            }

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            try
            {
                Boot(args, safe);
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

        private static void Boot(string[] args, bool safe)
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

                RuntimeBootstrap.CleanLegacyRuntime();

                if (safe)
                {
                    Trace2.Step("boot: SAFE MODE - media engine disabled");
                    RuntimePath = null;
                }
                else
                {
                    Trace2.Step("boot: ensure runtime");
                    try
                    {
                        RuntimePath = RuntimeBootstrap.EnsureRuntime(splash);
                    }
                    catch (Exception ex)
                    {
                        Trace2.Write("boot: runtime unpack failed -> " + ex);
                        RuntimePath = RuntimeBootstrap.RuntimeDir;
                    }
                    RuntimeBootstrap.RegisterNativeSearchPath(RuntimePath);
                    Trace2.Step("boot: runtime = " + (RuntimePath ?? "<none>"));
                }

                State = SettingsStore.Load();
                Trace2.Step("boot: settings loaded, library=" + (State.Library == null ? -1 : State.Library.Count));

                try
                {
                    if (safe) { throw new InvalidOperationException("Безопасный режим: движок отключён."); }

                    // Native crashes in libvlc cannot be caught in-process, so the engine is
                    // first exercised in a child process. If it dies there, this process
                    // survives and shows a readable error instead of vanishing.
                    Trace2.Step("boot: probing engine in child process");
                    if (splash != null) { splash.Report("Проверка медиадвижка…"); Application.DoEvents(); }
                    string probeError;
                    if (!EngineProbe.Verify(out probeError))
                    {
                        throw new InvalidOperationException(
                            "Медиадвижок не запустился: " + (probeError ?? "неизвестная причина"));
                    }

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

        /// <summary>
        /// Entry point used with --selftest: exercises every startup stage and writes a
        /// full report, without depending on the desktop window at all.
        /// </summary>
        private static void RunSelfTest(string[] args)
        {
            System.Text.StringBuilder rep = new System.Text.StringBuilder();
            Action<string> line = delegate(string t) { rep.AppendLine(t); Trace2.Write("selftest: " + t); };
            try
            {
                line("VidDesk selftest " + DateTime.Now.ToString("u"));
                line("exe: " + Application.ExecutablePath);
                line("os: " + Environment.OSVersion + " / 64bit process: " + Environment.Is64BitProcess);
                line("clr: " + Environment.Version);
                line("embedded runtime: " + RuntimeBootstrap.HasEmbeddedRuntime);
                line("runtime dir: " + RuntimeBootstrap.RuntimeDir);
                string rp = RuntimeBootstrap.EnsureRuntime(null);
                line("resolved runtime: " + (rp ?? "<none>"));
                if (rp != null)
                {
                    string[] files = Directory.GetFiles(rp);
                    line("libvlc.dll: " + File.Exists(Path.Combine(rp, "libvlc.dll")));
                    line("files in runtime: " + files.Length);
                }
                RuntimeBootstrap.RegisterNativeSearchPath(rp);
                string probeError;
                line("probe (child process): " + (EngineProbe.Verify(out probeError) ? "ok" : "FAILED - " + probeError));
                try
                {
                    line("libvlc version: " + VlcEngine.Version);
                    using (VlcEngine eng = new VlcEngine())
                    {
                        line("engine created ok");
                    }
                }
                catch (Exception ex)
                {
                    line("ENGINE ERROR: " + ex);
                }
                AppState st = SettingsStore.Load();
                line("settings: library=" + st.Library.Count + " active=" + (st.ActiveFile ?? "<none>"));
                try
                {
                    using (MainWindow w = new MainWindow()) { line("main window constructed ok"); }
                }
                catch (Exception ex)
                {
                    line("WINDOW ERROR: " + ex);
                }
                line("RESULT: " + (rep.ToString().Contains("ERROR") ? "problems found" : "all stages ok"));
            }
            catch (Exception ex)
            {
                line("SELFTEST CRASH: " + ex);
            }

            string path = Path.Combine(Path.GetTempPath(), "viddesk-selftest.txt");
            try { File.WriteAllText(path, rep.ToString()); } catch { }
            string body = rep.ToString();
            if (body.Length > 1400) { body = body.Substring(body.Length - 1400); }
            MessageBox.Show("Результат самопроверки:\r\n\r\n" + body + "\r\n\r\nФайл: " + path,
                "VidDesk — самопроверка", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        private static bool HasFlag(string[] args, string flag)
        {
            if (args == null) { return false; }
            foreach (string a in args)
            {
                if (string.Equals(a, flag, StringComparison.OrdinalIgnoreCase)) { return true; }
            }
            return false;
        }

        /// <summary>Window-less error surface for the last moments of a crashing process.</summary>
        private static void EmergencyBox(Exception ex, string when)
        {
            try
            {
                string msg = "VidDesk аварийно завершился (" + when + ")." + Environment.NewLine
                    + (ex == null ? "" : ex.Message) + Environment.NewLine + Environment.NewLine
                    + "Лог: " + Trace2.Path;
                MessageBox.Show(msg, "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Error,
                    MessageBoxDefaultButton.Button1, MessageBoxOptions.DefaultDesktopOnly);
            }
            catch { }
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
