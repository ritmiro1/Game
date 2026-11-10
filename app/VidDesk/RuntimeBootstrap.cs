using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Threading;
using System.Windows.Forms;

namespace VidDesk
{
    /// <summary>
    /// Unpacks the media runtime that ships inside the executable and wires DLL search
    /// paths. Keeps VidDesk a single portable EXE: no loose files next to it.
    /// </summary>
    internal static class RuntimeBootstrap
    {
        private const string ResourceName = "VidDesk.runtime.zip";
        private const string MarkerName = "runtime.stamp";

        public static string RuntimeDir
        {
            get
            {
                return Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "VidDesk", "runtime");
            }
        }

        public static bool HasEmbeddedRuntime
        {
            get
            {
                Assembly asm = Assembly.GetExecutingAssembly();
                foreach (string name in asm.GetManifestResourceNames())
                {
                    if (name == ResourceName) { return true; }
                }
                return false;
            }
        }

        /// <summary>Returns the folder that contains libvlc.dll, or null when none is available.</summary>
        public static string EnsureRuntime(IProgress progress)
        {
            string external = FindExternalRuntime();
            if (external != null) { return external; }

            if (!HasEmbeddedRuntime)
            {
                return null;
            }

            string dir = RuntimeDir;
            string marker = Path.Combine(dir, MarkerName);
            string stamp = Stamp();

            if (File.Exists(Path.Combine(dir, "libvlc.dll")) && File.Exists(marker)
                && File.ReadAllText(marker).Trim() == stamp)
            {
                return dir;
            }

            try
            {
                if (Directory.Exists(dir)) { Directory.Delete(dir, true); }
            }
            catch { }

            Directory.CreateDirectory(dir);
            if (progress != null) { progress.Report("Распаковка медиадвижка…"); }

            string tempZip = Path.Combine(Path.GetTempPath(), "viddesk_runtime_" + Guid.NewGuid().ToString("N") + ".zip");
            using (Stream src = Assembly.GetExecutingAssembly().GetManifestResourceStream(ResourceName))
            {
                if (src == null) { return null; }
                using (FileStream dst = File.Create(tempZip)) { src.CopyTo(dst, 81920); }
            }

            if (progress != null) { progress.Report("Распаковка медиадвижка… 50 %"); }
            try
            {
                ZipFile.ExtractToDirectory(tempZip, dir);
            }
            finally
            {
                try { File.Delete(tempZip); } catch { }
            }

            if (!File.Exists(Path.Combine(dir, "libvlc.dll")))
            {
                // The archive may keep everything under a single folder.
                string[] dirs = Directory.GetDirectories(dir);
                if (dirs.Length == 1 && File.Exists(Path.Combine(dirs[0], "libvlc.dll")))
                {
                    foreach (string f in Directory.GetFiles(dirs[0]))
                    {
                        File.Copy(f, Path.Combine(dir, Path.GetFileName(f)), true);
                    }
                    foreach (string d in Directory.GetDirectories(dirs[0]))
                    {
                        string target = Path.Combine(dir, Path.GetFileName(d));
                        if (!Directory.Exists(target)) { Directory.Move(d, target); }
                    }
                }
            }

            File.WriteAllText(marker, stamp);
            return dir;
        }

        private static string Stamp()
        {
            try { return Assembly.GetExecutingAssembly().GetName().Version.ToString(); }
            catch { return "1"; }
        }

        private static string FindExternalRuntime()
        {
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string[] candidates =
            {
                Path.Combine(baseDir, "libvlc"),
                Path.Combine(baseDir, "libvlc", "win-x64"),
                Path.Combine(baseDir, "libvlc", "win-x86"),
                baseDir
            };
            foreach (string dir in candidates)
            {
                if (File.Exists(Path.Combine(dir, "libvlc.dll"))) { return dir; }
            }
            return null;
        }

        /// <summary>Prepares the process so that plain [DllImport("libvlc")] resolves.</summary>
        public static void RegisterNativeSearchPath(string dir)
        {
            if (string.IsNullOrEmpty(dir)) { return; }
            try { Native.VlcApi.SetDllDirectory(dir); } catch { }

            string path = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
            if (path.IndexOf(dir, StringComparison.OrdinalIgnoreCase) < 0)
            {
                Environment.SetEnvironmentVariable("PATH", dir + ";" + path);
            }

            string plugins = Path.Combine(dir, "plugins");
            if (Directory.Exists(plugins))
            {
                Environment.SetEnvironmentVariable("VLC_PLUGIN_PATH", plugins);
            }
            Environment.SetEnvironmentVariable("VIDDESK_RUNTIME", dir);
        }

        public interface IProgress
        {
            void Report(string text);
        }
    }

    /// <summary>Minimal splash shown while the runtime is unpacked for the first time.</summary>
    internal sealed class Splash : Form, RuntimeBootstrap.IProgress
    {
        private readonly Label _text;

        public Splash()
        {
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = FormStartPosition.CenterScreen;
            BackColor = Ui.Pal.Panel;
            Size = new Size(420, 132);
            ShowInTaskbar = true;
            TopMost = true;

            Label title = new Label
            {
                Text = "VIDDESK",
                Font = new Font("Segoe UI Semibold", 16f),
                ForeColor = Ui.Pal.Text,
                AutoSize = true,
                Location = new Point(24, 22)
            };
            Controls.Add(title);

            _text = new Label
            {
                Text = "Подготовка к запуску…",
                Font = Ui.Pal.Base,
                ForeColor = Ui.Pal.Dim,
                AutoSize = false,
                Location = new Point(26, 66),
                Size = new Size(370, 40)
            };
            Controls.Add(_text);

            Paint += (s, e) =>
            {
                using (var pen = new Pen(Ui.Pal.Accent, 3f))
                {
                    e.Graphics.DrawLine(pen, 0, Height - 2, Width, Height - 2);
                }
            };
        }

        public void Report(string text)
        {
            if (InvokeRequired)
            {
                try { BeginInvoke((Action)(() => _text.Text = text)); } catch { }
                return;
            }
            _text.Text = text;
            Refresh();
        }
    }
}
