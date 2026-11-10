using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;

namespace VidDesk
{
    [DataContract]
    internal sealed class VideoEntry
    {
        [DataMember(Name = "file")] public string File;
        [DataMember(Name = "title")] public string Title;
        [DataMember(Name = "volume")] public int Volume = 100;
        [DataMember(Name = "muted")] public bool Muted;
        [DataMember(Name = "fit")] public string Fit = "cover";
        [DataMember(Name = "speed")] public int Speed = 100;
        [DataMember(Name = "preset")] public int EqualizerPreset;
        [DataMember(Name = "playlist")] public bool Playlist;

        public string Display
        {
            get { return string.IsNullOrEmpty(Title) ? Path.GetFileNameWithoutExtension(File) : Title; }
        }
    }

    [DataContract]
    internal sealed class AppState
    {
        [DataMember(Name = "active")] public string ActiveFile;
        [DataMember(Name = "library")] public List<VideoEntry> Library = new List<VideoEntry>();
        [DataMember(Name = "wallpaperEnabled")] public bool WallpaperEnabled = true;
        [DataMember(Name = "autostart")] public bool AutoStart;
        [DataMember(Name = "startMinimized")] public bool StartMinimized;
        [DataMember(Name = "shuffle")] public bool Shuffle;
        [DataMember(Name = "cycleSeconds")] public int CycleSeconds = 0;
        [DataMember(Name = "silentMode")] public bool SilentMode = true;

        public VideoEntry Find(string file)
        {
            if (string.IsNullOrEmpty(file)) { return null; }
            foreach (VideoEntry e in Library)
            {
                if (string.Equals(e.File, file, StringComparison.OrdinalIgnoreCase)) { return e; }
            }
            return null;
        }

        public VideoEntry Active
        {
            get { return Find(ActiveFile); }
        }

        public VideoEntry AddOrGet(string file)
        {
            VideoEntry existing = Find(file);
            if (existing != null) { return existing; }
            VideoEntry entry = new VideoEntry
            {
                File = file,
                Title = Path.GetFileNameWithoutExtension(file)
            };
            Library.Add(entry);
            return entry;
        }

        public void Remove(string file)
        {
            VideoEntry e = Find(file);
            if (e != null) { Library.Remove(e); }
            if (string.Equals(ActiveFile, file, StringComparison.OrdinalIgnoreCase)) { ActiveFile = null; }
        }
    }

    internal static class SettingsStore
    {
        private static readonly string Dir = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "VidDesk");

        private static string FilePath { get { return Path.Combine(Dir, "library.json"); } }

        public static AppState Load()
        {
            try
            {
                if (!File.Exists(FilePath)) { return new AppState(); }
                using (FileStream fs = File.OpenRead(FilePath))
                {
                    DataContractJsonSerializer ser = new DataContractJsonSerializer(typeof(AppState));
                    AppState state = (AppState)ser.ReadObject(fs);
                    if (state != null)
                    {
                        if (state.Library == null) { state.Library = new List<VideoEntry>(); }
                        return state;
                    }
                }
            }
            catch { }
            return new AppState();
        }

        public static void Save(AppState state)
        {
            try
            {
                Directory.CreateDirectory(Dir);
                using (FileStream fs = File.Create(FilePath))
                {
                    DataContractJsonSerializer ser = new DataContractJsonSerializer(typeof(AppState));
                    ser.WriteObject(fs, state);
                }
            }
            catch { }
        }

        public static string DataFolder { get { return Dir; } }
    }
}
