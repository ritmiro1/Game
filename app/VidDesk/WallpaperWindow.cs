using System;
using System.Drawing;
using System.Windows.Forms;
using VidDesk.Native;

namespace VidDesk
{
    /// <summary>
    /// Borderless host window that owns the libvlc video output. Normally reparented
    /// into the desktop wallpaper layer so it sits underneath the icons.
    /// </summary>
    internal sealed class WallpaperWindow : Form
    {
        public IntPtr SurfaceHandle { get { return Handle; } }

        public WallpaperWindow(bool previewMode)
        {
            FormBorderStyle = FormBorderStyle.None;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            BackColor = Color.Black;
            Text = "VidDesk Surface";
            DoubleBuffered = true;
            if (previewMode)
            {
                TopMost = true;
            }
        }

        protected override bool ShowWithoutActivation { get { return true; } }

        protected override CreateParams CreateParams
        {
            get
            {
                CreateParams cp = base.CreateParams;
                cp.ExStyle |= DesktopHost.WS_EX_NOACTIVATE | DesktopHost.WS_EX_TOOLWINDOW;
                cp.Style |= DesktopHost.WS_POPUP;
                return cp;
            }
        }

        public bool Attached { get; private set; }

        public void AttachToDesktop()
        {
            DesktopHost.RequestWallpaperHost();
            IntPtr host = DesktopHost.GetWallpaperParent();
            Rectangle bounds = DesktopHost.VirtualDesktop();
            Attached = DesktopHost.AttachAsChild(Handle, host, bounds.X, bounds.Y, bounds.Width, bounds.Height);
            if (!Attached)
            {
                // Explorer refused the wallpaper layer: fall back to a normal
                // desktop-level window so the user still sees their video.
                Attached = false;
                TopMost = false;
                Bounds = bounds;
            }
        }

        public void DetachFromDesktop()
        {
            if (DesktopHost.WindowAlive(Handle))
            {
                DesktopHost.DetachToDesktop(Handle);
            }
        }

        public void UpdateBoundsToDesktop()
        {
            if (!DesktopHost.WindowAlive(Handle)) { return; }
            Rectangle bounds = DesktopHost.VirtualDesktop();
            DesktopHost.Reposition(Handle, bounds.X, bounds.Y, bounds.Width, bounds.Height);
        }

        protected override void OnPaintBackground(PaintEventArgs e)
        {
            e.Graphics.Clear(Color.Black);
        }
    }
}
