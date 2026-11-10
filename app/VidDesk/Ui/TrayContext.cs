using System;
using System.Drawing;
using System.Windows.Forms;

namespace VidDesk.Ui
{
    /// <summary>Owns the tray icon, the main window lifetime and the desktop watcher.</summary>
    internal sealed class TrayContext : ApplicationContext
    {
        private readonly NotifyIcon _tray;
        private readonly System.Windows.Forms.Timer _watcher;
        private MainWindow _window;

        public TrayContext(string[] args)
        {
            Trace2.Step("shell: tray");
            ContextMenuStrip menu = new ContextMenuStrip();
            try
            {
                _tray = new NotifyIcon
                {
                    Icon = AppIcon.Load(),
                    Text = "VidDesk — живые обои",
                    Visible = true
                };
            }
            catch (Exception ex)
            {
                // Tray registration can fail (locked Explorer, restricted session).
                Trace2.Write("tray failed: " + ex.Message);
                _tray = new NotifyIcon { Icon = SystemIcons.Application, Visible = false };
            }
            _tray.DoubleClick += (s, e) => ShowWindow();
            _tray.MouseClick += (s, e) =>
            {
                if (e.Button == MouseButtons.Left) { ShowWindow(); }
            };

            menu.Items.Add("Открыть VidDesk", null, (s, e) => ShowWindow());
            menu.Items.Add("Остановить обои", null, (s, e) => { if (Program.Wallpaper != null) { Program.Wallpaper.Stop(); } });
            menu.Items.Add(new ToolStripSeparator());
            menu.Items.Add("Выход", null, (s, e) => ExitApp());
            _tray.ContextMenuStrip = menu;

            // Keeps the wallpaper attached when Explorer restarts or resolution changes.
            _watcher = new System.Windows.Forms.Timer { Interval = 5000 };
            _watcher.Tick += (s, e) => Watch();
            _watcher.Start();

            Trace2.Step("shell: restore wallpaper");
            RestoreActiveWallpaper();
            if (!HasFlag(args, "--tray")) { ShowWindow(); }

            MenuItemRestore();
        }

        private void RestoreActiveWallpaper()
        {
            if (Program.Wallpaper == null || Program.State == null) { return; }
            if (!Program.State.WallpaperEnabled) { return; }
            VideoEntry active = Program.State.Active;
            if (active == null) { return; }
            if (Program.Wallpaper == null) { return; }
            try { Program.Wallpaper.Play(active, Program.State.SilentMode); }
            catch (Exception ex) { Trace2.Write("restore wallpaper failed: " + ex.Message); }
        }

        private void MenuItemRestore()
        {
            if (Program.State != null && Program.State.AutoStart) { Program.EnableAutoStart(true); }
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

        public void ShowWindow()
        {
            try
            {
                if (_window == null || _window.IsDisposed)
                {
                    Trace2.Step("shell: building main window");
                    _window = new MainWindow();
                    Trace2.Step("shell: main window built");
                }
                if (_window.WindowState == FormWindowState.Minimized)
                {
                    _window.WindowState = FormWindowState.Normal;
                }
                _window.Show();
                _window.Activate();
            }
            catch (Exception ex)
            {
                Trace2.Write("show window failed: " + ex);
                MessageBox.Show(
                    "Не удалось открыть окно VidDesk:" + Environment.NewLine + ex.Message
                    + Environment.NewLine + Environment.NewLine + "Лог: " + Trace2.Path,
                    "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void Watch()
        {
            if (Program.Wallpaper == null || !Program.Wallpaper.IsActive) { return; }
            WallpaperWindow w = Program.Wallpaper.Window;
            if (w == null || !Native.DesktopHost.WindowAlive(w.SurfaceHandle))
            {
                Program.Wallpaper.Stop();
                if (Program.State.WallpaperEnabled && Program.State.Active != null)
                {
                    try { Program.Wallpaper.Play(Program.State.Active, Program.State.SilentMode); }
                    catch { }
                }
                return;
            }
            w.UpdateBoundsToDesktop();
        }

        private void ExitApp()
        {
            SettingsStore.Save(Program.State);
            if (Program.Wallpaper != null) { Program.Wallpaper.Stop(); }
            _watcher.Stop();
            _tray.Visible = false;
            ExitThread();
        }
    }

    /// <summary>Loads the embedded window icon.</summary>
    internal static class AppIcon
    {
        private static Icon _icon;

        public static Icon Load()
        {
            if (_icon != null) { return _icon; }
            try
            {
                _icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            }
            catch { }
            if (_icon == null)
            {
                Bitmap bmp = new Bitmap(32, 32);
                using (Graphics g = Graphics.FromImage(bmp))
                {
                    g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
                    g.FillRectangle(new SolidBrush(Pal.Panel), 0, 0, 32, 32);
                    g.FillEllipse(new SolidBrush(Pal.Accent), 6, 6, 20, 20);
                }
                _icon = Icon.FromHandle(bmp.GetHicon());
            }
            return _icon;
        }
    }
}
