using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace VidDesk.Native
{
    /// <summary>
    /// Thin managed wrapper over a libvlc instance and media player. Written from
    /// scratch for this application.
    /// </summary>
    internal sealed class VlcEngine : IDisposable
    {
        private IntPtr _instance;
        private bool _disposed;

        public VlcEngine()
        {
            VlcApi.Load();
            _instance = VlcApi.libvlc_new(0, IntPtr.Zero);
            if (_instance == IntPtr.Zero)
            {
                throw new InvalidOperationException(
                    "Не удалось инициализировать libvlc. Проверьте, что папка libvlc находится рядом с VidDesk.exe.");
            }
        }

        public static string Version
        {
            get
            {
                try { return VlcApi.libvlc_get_version(); }
                catch { return "недоступна"; }
            }
        }

        public VlcSurface CreatePlayer()
        {
            if (_disposed) { throw new ObjectDisposedException(nameof(VlcEngine)); }
            IntPtr p = VlcApi.libvlc_media_player_new(_instance);
            return new VlcSurface(p, _instance);
        }

        public void Dispose()
        {
            if (_disposed) { return; }
            _disposed = true;
            if (_instance != IntPtr.Zero)
            {
                VlcApi.libvlc_release(_instance);
                _instance = IntPtr.Zero;
            }
        }
    }

    /// <summary>One video output surface, optionally bound to a native window handle.</summary>
    internal sealed class VlcSurface : IDisposable
    {
        private IntPtr _player;
        private IntPtr _media;
        private IntPtr _equalizer;
        private bool _disposed;

        private readonly IntPtr _instance;

        internal VlcSurface(IntPtr player, IntPtr instance)
        {
            _player = player;
            _instance = instance;
            // A wallpaper must never steal keyboard or mouse focus.
            VlcApi.libvlc_video_set_key_input(_player, 0);
            VlcApi.libvlc_video_set_mouse_input(_player, 0);
        }

        public bool IsValid { get { return _player != IntPtr.Zero; } }

        public void BindWindow(IntPtr hwnd)
        {
            if (_player == IntPtr.Zero) { return; }
            VlcApi.libvlc_media_player_set_hwnd(_player, hwnd);
        }

        public bool Muted
        {
            set { if (_player != IntPtr.Zero) VlcApi.libvlc_audio_set_mute(_player, value ? 1 : 0); }
        }

        /// <summary>Volume in percent, 0..500. Values above 100 are amplified by the mixer.</summary>
        public int Volume
        {
            get { return _player == IntPtr.Zero ? 0 : VlcApi.libvlc_audio_get_volume(_player); }
            set
            {
                if (_player == IntPtr.Zero) { return; }
                VlcApi.libvlc_audio_set_volume(_player, Math.Max(0, Math.Min(500, value)));
            }
        }

        public float Rate
        {
            set { if (_player != IntPtr.Zero) VlcApi.libvlc_media_player_set_rate(_player, Math.Max(0.1f, Math.Min(4f, value))); }
        }

        public string AspectRatio
        {
            set
            {
                if (_player == IntPtr.Zero) { return; }
                VlcApi.libvlc_video_set_aspect_ratio(_player, string.IsNullOrEmpty(value) ? null : value);
            }
        }

        public bool IsPlaying
        {
            get { return _player != IntPtr.Zero && VlcApi.libvlc_media_player_is_playing(_player) != 0; }
        }

        public void Load(string path, bool loop, bool mute)
        {
            if (_player == IntPtr.Zero) { return; }
            ReleaseMedia();
            _media = VlcApi.libvlc_media_new_path(_instance, path);
            if (_media == IntPtr.Zero)
            {
                throw new IOException("Не удалось открыть файл: " + path);
            }
            VlcApi.libvlc_media_add_option(_media, loop ? "input-repeat=65535" : "input-repeat=0");
            VlcApi.libvlc_media_add_option(_media, "no-audio-time-stretch");
            VlcApi.libvlc_media_add_option(_media, "avcodec-hw=any");
            VlcApi.libvlc_media_player_set_media(_player, _media);
            Muted = mute;
        }

        public void Play()
        {
            if (_player != IntPtr.Zero) { VlcApi.libvlc_media_player_play(_player); }
        }

        public void Pause()
        {
            if (_player != IntPtr.Zero) { VlcApi.libvlc_media_player_pause(_player); }
        }

        public void Stop()
        {
            if (_player != IntPtr.Zero) { VlcApi.libvlc_media_player_stop(_player); }
        }

        /// <summary>
        /// Applies volume together with an equalizer preset. libvlc's mixer saturates
        /// around 200 %, so everything above that is delivered as equalizer preamp gain
        /// (up to +20 dB), which lets the slider honestly reach 500 %.
        /// </summary>
        public void ApplyAudio(int volumePercent, int presetIndex)
        {
            if (_player == IntPtr.Zero) { return; }
            int requested = Math.Max(0, Math.Min(500, volumePercent));
            VlcApi.libvlc_audio_set_volume(_player, Math.Min(200, requested));

            float gainDb = 0f;
            if (requested > 200)
            {
                gainDb = (float)(20.0 * Math.Log10(requested / 200.0));
                if (gainDb > 20f) { gainDb = 20f; }
            }

            ReleaseEqualizer();
            if (presetIndex > 0)
            {
                _equalizer = VlcApi.libvlc_audio_equalizer_new_from_preset((uint)(presetIndex - 1));
            }
            else if (gainDb > 0.05f)
            {
                _equalizer = VlcApi.libvlc_audio_equalizer_new();
            }
            else
            {
                return;
            }

            if (_equalizer != IntPtr.Zero)
            {
                if (gainDb > 0.05f) { VlcApi.libvlc_audio_equalizer_set_preamp(_equalizer, gainDb); }
                VlcApi.libvlc_media_player_set_equalizer(_player, _equalizer);
            }
        }

        public void ApplyEqualizerPreset(int presetIndex)
        {
            ApplyAudio(VolumeRequested, presetIndex);
        }

        /// <summary>Last volume the user asked for, kept so the equalizer can be rebuilt.</summary>
        public int VolumeRequested { get; set; }

        public static List<string> EqualizerPresetNames()
        {
            List<string> names = new List<string> { "Без эквалайзера" };
            try
            {
                uint count = VlcApi.libvlc_audio_equalizer_get_preset_count();
                for (uint i = 0; i < count; i++)
                {
                    IntPtr ptr = VlcApi.libvlc_audio_equalizer_get_preset_name(i);
                    names.Add(ptr == IntPtr.Zero ? "Preset " + (i + 1) : Marshal.PtrToStringAnsi(ptr));
                }
            }
            catch { }
            return names;
        }

        private void ReleaseMedia()
        {
            if (_media != IntPtr.Zero)
            {
                VlcApi.libvlc_media_release(_media);
                _media = IntPtr.Zero;
            }
        }

        private void ReleaseEqualizer()
        {
            if (_equalizer != IntPtr.Zero)
            {
                VlcApi.libvlc_audio_equalizer_release(_equalizer);
                _equalizer = IntPtr.Zero;
            }
        }

        public void Dispose()
        {
            if (_disposed) { return; }
            _disposed = true;
            if (_player != IntPtr.Zero)
            {
                VlcApi.libvlc_media_player_stop(_player);
                VlcApi.libvlc_media_player_release(_player);
                _player = IntPtr.Zero;
            }
            ReleaseMedia();
            ReleaseEqualizer();
        }
    }
}
