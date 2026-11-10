using System;
using System.IO;
using System.Windows.Forms;
using VidDesk.Native;

namespace VidDesk
{
    /// <summary>Coordinates the desktop surface, the video engine and the current entry.</summary>
    internal sealed class WallpaperService : IDisposable
    {
        private readonly VlcEngine _engine;
        private WallpaperWindow _window;
        private VlcSurface _surface;
        private VideoEntry _entry;
        private bool _mutedOverride;

        public event EventHandler StateChanged;

        public WallpaperService(VlcEngine engine)
        {
            _engine = engine;
        }

        public bool IsActive { get { return _window != null && _surface != null; } }
        public string CurrentFile { get { return _entry == null ? null : _entry.File; } }
        public WallpaperWindow Window { get { return _window; } }

        public void Play(VideoEntry entry, bool silent)
        {
            if (entry == null || !File.Exists(entry.File)) { throw new FileNotFoundException("Видеофайл не найден", entry == null ? "" : entry.File); }
            Stop();

            _entry = entry;
            _window = new WallpaperWindow(false);
            _window.Show();
            _window.AttachToDesktop();
            _surface = _engine.CreatePlayer();
            _surface.BindWindow(_window.SurfaceHandle);
            _surface.Load(entry.File, true, silent || entry.Muted || entry.Volume <= 0);
            ApplyVolume();
            ApplyFit(entry.Fit);
            ApplySpeed(entry.Speed);
            _surface.ApplyAudio(entry.Volume, entry.EqualizerPreset);
            _surface.Play();
            Raise();
        }

        public void Stop()
        {
            if (_surface != null)
            {
                _surface.Dispose();
                _surface = null;
            }
            if (_window != null)
            {
                _window.DetachFromDesktop();
                _window.Hide();
                _window.Dispose();
                _window = null;
            }
            _entry = null;
            Raise();
        }

        public void SetMutedOverride(bool muted)
        {
            _mutedOverride = muted;
            if (_surface != null) { _surface.Muted = muted; }
        }

        public void ApplyVolume()
        {
            if (_surface == null || _entry == null) { return; }
            int v = Math.Max(0, Math.Min(500, _entry.Volume));
            _surface.VolumeRequested = v;
            _surface.ApplyAudio(v, _entry.EqualizerPreset);
            _surface.Muted = _mutedOverride || _entry.Muted || v == 0;
        }

        public void ApplyFit(string mode)
        {
            if (_surface == null) { return; }
            switch (mode)
            {
                case "contain": _surface.AspectRatio = null; break;
                case "stretch": _surface.AspectRatio = "16:9"; break;
                default: _surface.AspectRatio = null; break;
            }
        }

        public void ApplySpeed(int percent)
        {
            if (_surface == null) { return; }
            _surface.Rate = Math.Max(10, percent) / 100f;
        }

        public void ApplyEqualizer(int preset)
        {
            if (_surface != null && _entry != null) { _surface.ApplyAudio(_entry.Volume, preset); }
        }

        public void Reattach()
        {
            if (_window == null) { return; }
            _window.UpdateBoundsToDesktop();
        }

        private void Raise()
        {
            if (StateChanged != null) { StateChanged(this, EventArgs.Empty); }
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
