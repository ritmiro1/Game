using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace VidDesk
{
    /// <summary>
    /// Starts the media engine in a separate short-lived process. A native crash inside
    /// libvlc kills only that child, so the shell can report the problem instead of
    /// disappearing together with it.
    /// </summary>
    internal static class EngineProbe
    {
        public const string Flag = "--probe-engine";

        /// <summary>Child-process entry point: touch the engine and report the result.</summary>
        public static int RunChild()
        {
            StringBuilder report = new StringBuilder();
            try
            {
                string dir = RuntimeBootstrap.EnsureRuntime(null);
                RuntimeBootstrap.RegisterNativeSearchPath(dir);
                report.AppendLine("dir=" + (dir ?? "<none>"));
                using (Native.VlcEngine engine = new Native.VlcEngine())
                {
                    report.AppendLine("version=" + Native.VlcEngine.Version);
                    using (Native.VlcSurface surface = engine.CreatePlayer())
                    {
                        report.AppendLine("player=" + (surface.IsValid ? "ok" : "null"));
                    }
                }
                report.AppendLine("result=ok");
            }
            catch (Exception ex)
            {
                report.AppendLine("result=fail");
                report.AppendLine("error=" + ex.GetType().Name + ": " + ex.Message);
            }

            try
            {
                string target = Path.Combine(Path.GetTempPath(), "viddesk-probe-" + Process.GetCurrentProcess().Id + ".txt");
                File.WriteAllText(target, report.ToString());
                Console.WriteLine(target);
            }
            catch { }
            return 0;
        }

        /// <summary>
        /// Runs the probe in a child process. Returns true when the engine initialised
        /// successfully; on failure <paramref name="reason"/> explains what happened.
        /// </summary>
        public static bool Verify(out string reason)
        {
            reason = null;
            string exe = Application.ExecutablePath;
            string outFile = Path.Combine(Path.GetTempPath(), "viddesk-probe-" + Guid.NewGuid().ToString("N") + ".txt");
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo(exe, Flag)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    RedirectStandardOutput = true
                };
                using (Process p = Process.Start(psi))
                {
                    if (p == null) { reason = "не удалось запустить процесс проверки"; return false; }
                    string line = p.StandardOutput.ReadToEnd().Trim();
                    if (!p.WaitForExit(25000))
                    {
                        try { p.Kill(); } catch { }
                        reason = "проверка движка не завершилась за 25 с";
                        Trace2.Write("probe: timeout");
                        return false;
                    }
                    int code = p.ExitCode;
                    Trace2.Write("probe: exit=" + code + " out=" + (line ?? ""));
                    if (line != null && File.Exists(line)) { outFile = line; }
                    if (code != 0)
                    {
                        reason = "процесс проверки движка завершился с кодом 0x" + code.ToString("X8")
                                 + " (нативный сбой в libvlc)";
                        return false;
                    }
                }

                // The child names its report after its own pid, so fall back to globbing.
                if (!File.Exists(outFile))
                {
                    string[] found = Directory.GetFiles(Path.GetTempPath(), "viddesk-probe-*.txt");
                    if (found.Length == 0) { reason = ""; return true; }
                    Array.Sort(found);
                    outFile = found[found.Length - 1];
                }

                string text = File.ReadAllText(outFile);
                Trace2.Write("probe: " + text.Replace(Environment.NewLine, " | "));
                try { File.Delete(outFile); } catch { }
                if (text.IndexOf("result=ok", StringComparison.Ordinal) >= 0) { return true; }

                int idx = text.IndexOf("error=", StringComparison.Ordinal);
                reason = idx >= 0 ? text.Substring(idx + 6).Trim() : "движок не инициализировался";
                return false;
            }
            catch (Exception ex)
            {
                Trace2.Write("probe failed: " + ex);
                reason = ex.Message;
                return false;
            }
        }
    }
}
