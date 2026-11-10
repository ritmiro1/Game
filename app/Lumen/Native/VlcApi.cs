using System;
using System.IO;
using System.Runtime.InteropServices;

namespace Lumen.Native
{
    /// <summary>
    /// Minimal hand-written libvlc loader. Only the handful of entry points this
    /// application actually needs are declared here, so the binary does not depend
    /// on any managed wrapper library.
    /// </summary>
    internal static class VlcApi
    {
        private const string Lib = "libvlc";

        private static bool _loaded;

        internal static string ResolveRuntimeFolder()
        {
            string env = Environment.GetEnvironmentVariable("LUMEN_RUNTIME");
            if (!string.IsNullOrEmpty(env) && File.Exists(Path.Combine(env, "libvlc.dll")))
            {
                return env;
            }
            string baseDir = AppDomain.CurrentDomain.BaseDirectory;
            string[] candidates =
            {
                Path.Combine(baseDir, "libvlc", "win-x64"),
                Path.Combine(baseDir, "libvlc", "win-x86"),
                Path.Combine(baseDir, "libvlc"),
                baseDir
            };
            foreach (string dir in candidates)
            {
                if (File.Exists(Path.Combine(dir, "libvlc.dll")))
                {
                    return dir;
                }
            }
            return null;
        }

        internal static void Load()
        {
            if (_loaded)
            {
                return;
            }
            string dir = ResolveRuntimeFolder();
            if (dir != null)
            {
                // Let the Windows loader find libvlc.dll and every plugin dependency
                // that sits next to it.
                SetDllDirectory(dir);
                string path = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
                Environment.SetEnvironmentVariable("PATH", dir + ";" + path);
            }
            _loaded = true;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool SetDllDirectory(string lpPathName);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_new(int argc, IntPtr argv);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_new_argv(int argc, IntPtr[] argv);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_release(IntPtr instance);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern string libvlc_get_version();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_media_new_path(IntPtr instance, [MarshalAs(UnmanagedType.LPUTF8Str)] string path);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_media_add_option(IntPtr media, [MarshalAs(UnmanagedType.LPUTF8Str)] string options);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_media_release(IntPtr media);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_media_player_new(IntPtr instance);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_media_player_release(IntPtr player);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_media_player_set_media(IntPtr player, IntPtr media);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_media_player_set_hwnd(IntPtr player, IntPtr drawable);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_media_player_play(IntPtr player);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_media_player_stop(IntPtr player);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_media_player_pause(IntPtr player);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_media_player_is_playing(IntPtr player);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_audio_set_volume(IntPtr player, int volume);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_audio_get_volume(IntPtr player);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_audio_set_mute(IntPtr player, int mute);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_video_get_size(IntPtr player, uint num, out uint width, out uint height);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_errmsg();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_media_player_set_rate(IntPtr player, float rate);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_video_set_aspect_ratio(IntPtr player, [MarshalAs(UnmanagedType.LPUTF8Str)] string aspect);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_video_set_key_input(IntPtr player, int on);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_video_set_mouse_input(IntPtr player, int on);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_audio_equalizer_new();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_audio_equalizer_new_from_preset(uint index);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_audio_equalizer_release(IntPtr eq);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_media_player_set_equalizer(IntPtr player, IntPtr eq);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern uint libvlc_audio_equalizer_get_preset_count();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern IntPtr libvlc_audio_equalizer_get_preset_name(uint index);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern void libvlc_audio_equalizer_set_preamp(IntPtr eq, float preamp);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl, ExactSpelling = true)]
        internal static extern int libvlc_audio_equalizer_set_amp_at_index(IntPtr eq, float amp, uint band);
    }
}
