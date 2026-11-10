using System;
using System.Drawing;
using System.Windows.Forms;
using VidDesk.Native;

namespace VidDesk.Ui
{
    /// <summary>
    /// Silent inline preview that plays the selected clip inside the main window,
    /// using a chromeless surface window tracked to the preview host rectangle.
    /// </summary>
    internal sealed class PreviewForm : Form
    {
        public PreviewForm()
        {
            SetStyle(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint, true);
        }

        protected override bool ShowWithoutActivation { get { return true; } }
    }

    internal sealed class PreviewSurface : IDisposable
    {
        private Form _form;
        private VlcSurface _surface;
        private readonly VlcEngine _engine;
        private string _file;

        public PreviewSurface(VlcEngine engine)
        {
            _engine = engine;
        }

        public void SyncPosition(Rectangle screenRect)
        {
            if (_form == null || _form.IsDisposed) { return; }
            if (_form.Bounds != screenRect) { _form.Bounds = screenRect; }
        }

        public void Show(string file, IWin32Window owner = null)
        {
            if (string.IsNullOrEmpty(file) || !System.IO.File.Exists(file)) { Hide(); return; }
            EnsureForm(owner);
            _form.Visible = true;
            if (_surface == null)
            {
                _surface = _engine.CreatePlayer();
                _surface.BindWindow(_form.Handle);
            }
            if (!string.Equals(_file, file, StringComparison.OrdinalIgnoreCase))
            {
                _file = file;
                _surface.Load(file, true, true);
                _surface.Play();
            }
        }

        private void EnsureForm(IWin32Window owner)
        {
            if (_form != null && !_form.IsDisposed) { return; }
            _form = new PreviewForm
            {
                FormBorderStyle = FormBorderStyle.None,
                ShowInTaskbar = false,
                StartPosition = FormStartPosition.Manual,
                BackColor = Color.Black
            };
            if (owner != null) { _form.Show(owner); } else { _form.Show(); }
        }

        public void Hide()
        {
            if (_surface != null)
            {
                _surface.Dispose();
                _surface = null;
            }
            _file = null;
            if (_form != null && !_form.IsDisposed) { _form.Visible = false; }
        }

        public void Dispose()
        {
            Hide();
            if (_form != null && !_form.IsDisposed) { _form.Dispose(); }
            _form = null;
        }
    }
}
