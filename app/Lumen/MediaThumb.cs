using System;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Globalization;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace Lumen
{
    /// <summary>
    /// Builds small preview cards for library tiles: one frame grabbed with ffmpeg next
    /// to the media runtime, plus duration probing when available. Everything degrades
    /// gracefully to a placeholder when tools are missing.
    /// </summary>
    internal static class MediaThumb
    {
        private static readonly object Gate = new object();
        private static string _ffmpegPath;
        private static bool _resolved;

        public static string FfmpegPath
        {
            get
            {
                lock (Gate)
                {
                    if (!_resolved)
                    {
                        _resolved = true;
                        string local = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ffmpeg.exe");
                        _ffmpegPath = File.Exists(local) ? local : null;
                    }
                    return _ffmpegPath;
                }
            }
        }

        public static void Begin(string file, int width, int height, Action<Bitmap> done)
        {
            Task.Run(() =>
            {
                Bitmap bmp = null;
                try { bmp = Build(file, width, height); }
                catch { bmp = null; }
                if (bmp == null) { bmp = Placeholder(System.IO.Path.GetFileNameWithoutExtension(file), width, height); }
                if (done != null) { done(bmp); }
            });
        }

        public static Bitmap Build(string file, int width, int height)
        {
            Bitmap frame = GrabFrame(file);
            if (frame == null) { return null; }
            Bitmap scaled = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
            using (Graphics g = Graphics.FromImage(scaled))
            {
                g.InterpolationMode = InterpolationMode.HighQualityBicubic;
                g.PixelOffsetMode = PixelOffsetMode.HighQuality;
                double ratio = Math.Max((double)width / frame.Width, (double)height / frame.Height);
                int w = (int)Math.Ceiling(frame.Width * ratio);
                int h = (int)Math.Ceiling(frame.Height * ratio);
                g.DrawImage(frame, (width - w) / 2, (height - h) / 2, w, h);
            }
            frame.Dispose();
            return scaled;
        }

        private static Bitmap GrabFrame(string file)
        {
            string ffmpeg = FfmpegPath;
            if (ffmpeg == null) { return null; }
            try
            {
                lock (Gate)
                {
                    string tmp = Path.Combine(Path.GetTempPath(), "lumen_" + Guid.NewGuid().ToString("N") + ".jpg");
                    var psi = new ProcessStartInfo(ffmpeg,
                        "-hide_banner -loglevel error -y -ss 00:00:01 -i \"" + file + "\" -frames:v 1 -vf scale=480:-2 \"" + tmp + "\"")
                    {
                        CreateNoWindow = true,
                        UseShellExecute = false,
                        RedirectStandardError = true,
                        RedirectStandardOutput = true
                    };
                    using (Process p = Process.Start(psi))
                    {
                        p.WaitForExit(8000);
                    }
                    if (!File.Exists(tmp)) { return null; }
                    using (Image img = Image.FromFile(tmp))
                    {
                        Bitmap copy = new Bitmap(img);
                        File.Delete(tmp);
                        return copy;
                    }
                }
            }
            catch { return null; }
        }

        public static Bitmap Placeholder(string title, int width, int height)
        {
            Bitmap bmp = new Bitmap(width, height, PixelFormat.Format32bppPArgb);
            using (Graphics g = Graphics.FromImage(bmp))
            {
                using (var brush = new LinearGradientBrush(new Rectangle(0, 0, width, height),
                    Color.FromArgb(38, 46, 58), Color.FromArgb(20, 23, 29), 65f))
                {
                    g.FillRectangle(brush, 0, 0, width, height);
                }
                using (var pen = new Pen(Color.FromArgb(70, 82, 96), 1))
                {
                    g.DrawRectangle(pen, 0, 0, width - 1, height - 1);
                }
                using (var icon = new Pen(Color.FromArgb(90, 104, 120), 2f))
                {
                    int cx = width / 2;
                    int cy = height / 2 - 6;
                    g.DrawPolygon(icon, new[]
                    {
                        new Point(cx - 11, cy - 8), new Point(cx + 11, cy - 8),
                        new Point(cx + 11, cy + 8), new Point(cx - 11, cy + 8)
                    });
                    g.DrawLine(icon, cx - 4, cy - 5, cx + 6, cy);
                    g.DrawLine(icon, cx + 6, cy, cx - 4, cy + 5);
                }
                if (!string.IsNullOrEmpty(title))
                {
                    string clipped = title.Length > 26 ? title.Substring(0, 25) + "…" : title;
                    using (var fmt = new StringFormat { Alignment = StringAlignment.Center, Trimming = StringTrimming.EllipsisCharacter })
                    {
                        using (var b = new SolidBrush(Color.FromArgb(150, 162, 176)))
                        {
                            g.DrawString(clipped, Ui.Pal.Small, b, new RectangleF(6, height - 32, width - 12, 26), fmt);
                        }
                    }
                }
            }
            return bmp;
        }

        public static string ProbeDuration(string file)
        {
            string ffprobe = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "ffprobe.exe");
            if (!File.Exists(ffprobe)) { return null; }
            try
            {
                var psi = new ProcessStartInfo(ffprobe,
                    "-v error -show_entries format=duration -of default=nw=1:nk=1 \"" + file + "\"")
                {
                    CreateNoWindow = true,
                    UseShellExecute = false,
                    RedirectStandardOutput = true
                };
                using (Process p = Process.Start(psi))
                {
                    string output = p.StandardOutput.ReadToEnd();
                    p.WaitForExit(6000);
                    double seconds;
                    if (double.TryParse(output.Trim(), NumberStyles.Float, CultureInfo.InvariantCulture, out seconds))
                    {
                        return TimeSpan.FromSeconds(seconds).ToString(seconds >= 3600 ? @"h\:mm\:ss" : @"m\:ss");
                    }
                }
            }
            catch { }
            return null;
        }
    }
}
