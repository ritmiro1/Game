using System;
using System.IO;

namespace Lumen
{
    /// <summary>
    /// UI-side coordinator. Talks to the engine helper process over a pipe; never loads
    /// the native video library itself.
    /// </summary>
    internal sealed class WallpaperService : IDisposable
    {
        private readonly EngineClient _client;
        private VideoEntry _entry;
        private bool _mutedOverride;

        public event EventHandler StateChanged;

        public WallpaperService(EngineClient client)
        {
            _client = client;
        }

        public bool IsActive { get { return _entry != null && _client.IsRunning; } }
        public string CurrentFile { get { return _entry == null ? null : _entry.File; } }
        public EngineClient Client { get { return _client; } }

        /// <summary>Makes sure the helper process is alive; returns false with a reason.</summary>
        public bool EnsureEngine()
        {
            if (_client.IsRunning) { return true; }
            bool ok = _client.Start();
            if (!ok) { Trace2.Write("wallpaper: engine start failed: " + _client.LastError); }
            return ok;
        }

        public void Play(VideoEntry entry, bool silent)
        {
            if (entry == null) { throw new ArgumentNullException("entry"); }
            if (!File.Exists(entry.File)) { throw new FileNotFoundException("Видеофайл не найден", entry.File); }
            if (!EnsureEngine())
            {
                throw new InvalidOperationException(
                    "Медиадвижок не запустился: " + (_client.LastError ?? "неизвестная причина")
                    + ". Подробности в логе: " + Trace2.Path);
            }

            _entry = entry;
            string mute = (silent || _mutedOverride || entry.Muted || entry.Volume <= 0) ? "1" : "0";
            string reply = _client.Send("PLAY|" + entry.File + "|" + entry.Volume + "|" + mute + "|"
                                        + entry.Fit + "|" + entry.Speed + "|" + entry.EqualizerPreset);
            if (reply == null)
            {
                _entry = null;
                throw new InvalidOperationException(
                    "Движок не принял видео: " + (_client.LastError ?? "нет ответа"));
            }
            Raise();
        }

        public void Stop()
        {
            if (_client.IsRunning) { _client.Send("STOP"); }
            _entry = null;
            Raise();
        }

        public void SetMutedOverride(bool muted)
        {
            _mutedOverride = muted;
            ApplyVolume();
        }

        public void ApplyVolume()
        {
            if (_entry == null) { return; }
            string mute = (_mutedOverride || _entry.Muted || _entry.Volume <= 0) ? "1" : "0";
            _client.Send("VOL|" + _entry.Volume + "|" + _entry.EqualizerPreset + "|" + mute);
        }

        public void ApplyFit(string mode)
        {
            if (_entry == null) { return; }
            _client.Send("FIT|" + mode);
        }

        public void ApplySpeed(int percent)
        {
            if (_entry == null) { return; }
            _client.Send("SPEED|" + percent);
        }

        public void ApplyEqualizer(int preset)
        {
            if (_entry == null) { return; }
            _client.Send("EQ|" + preset);
        }

        public void Reattach()
        {
            if (_entry == null) { return; }
            // Re-sending PLAY rebuilds the surface, which re-attaches it to the desktop.
            string mute = (_mutedOverride || _entry.Muted || _entry.Volume <= 0) ? "1" : "0";
            _client.Send("PLAY|" + _entry.File + "|" + _entry.Volume + "|" + mute + "|"
                         + _entry.Fit + "|" + _entry.Speed + "|" + _entry.EqualizerPreset);
        }

        private void Raise()
        {
            if (StateChanged != null) { StateChanged(this, EventArgs.Empty); }
        }

        public void Dispose()
        {
            Stop();
            _client.Dispose();
        }
    }
}
