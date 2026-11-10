using System;
using System.Drawing;
using System.Windows.Forms;
using Lumen.Native;

namespace Lumen
{
    /// <summary>
    /// Borderless host window that owns the libvlc video output and lives on the desktop
    /// wallpaper layer, underneath the icons.
    /// </summary>
    internal sealed class WallpaperWindow : Form
    {
        public IntPtr SurfaceHandle { get { return Handle; } }
        public bool Attached { get; private set; }
        public string HostKind { get; private set; }

        public WallpaperWindow(bool previewMode)
        {
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            BackColor = Color.Black;
            Text = "Lumen Surface";
            if (previewMode) { TopMost = true; }
        }

        protected override bool ShowWithoutActivation { get { return true; } }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= 0x08000000 | 0x00000080;   // NOACTIVATE | TOOLWINDOW
                cp.Style |= unchecked((int)0x80000000);  // start as WS_POPUP
                return cp;
            }
        }

        /// <summary>Puts this window behind the desktop icons and reports where it landed.</summary>
        public void AttachToDesktop()
        {
            HostKind = WallpaperAttach.Place(Handle, false);
            Attached = HostKind == "WorkerW" || HostKind == "Progman";
            Trace2.Write("wall: placed host=" + HostKind + " hwnd=" + Handle.ToString("X"));
        }

        public void DetachFromDesktop()
        {
            if (!WallpaperAttach.WindowAlive(Handle)) { return; }
            DesktopHost.DetachToDesktop(Handle);
        }

        public void UpdateBoundsToDesktop()
        {
            if (!WallpaperAttach.WindowAlive(Handle)) { return; }
            WallpaperAttach.RECT r = WallpaperAttach.VirtualScreen();
            DesktopHost.Reposition(Handle, r.Left, r.Top, r.Width, r.Height);
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            e.Graphics.Clear(Color.Black);
        }
    }
}
