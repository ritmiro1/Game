using System;
using System.Drawing;
using System.Windows.Forms;

namespace Lumen.Ui
{
    /// <summary>Dark theme primitives. All drawing is custom, no third-party controls.</summary>
    internal static class Pal
    {
        public static readonly Color Bg = Color.FromArgb(18, 20, 24);
        public static readonly Color Panel = Color.FromArgb(26, 29, 35);
        public static readonly Color PanelAlt = Color.FromArgb(33, 37, 44);
        public static readonly Color Hover = Color.FromArgb(45, 50, 60);
        public static readonly Color Line = Color.FromArgb(52, 58, 68);
        public static readonly Color Text = Color.FromArgb(232, 236, 242);
        public static readonly Color Dim = Color.FromArgb(140, 148, 160);
        public static readonly Color Accent = Color.FromArgb(86, 196, 255);
        public static readonly Color AccentDark = Color.FromArgb(31, 110, 150);
        public static readonly Color Danger = Color.FromArgb(232, 96, 96);

        public static readonly Font Base = new Font("Segoe UI", 9f);
        public static readonly Font Bold = new Font("Segoe UI Semibold", 9.5f);
        public static readonly Font Title = new Font("Segoe UI Semibold", 15f);
        public static readonly Font Small = new Font("Segoe UI", 8f);
        public static readonly Font Num = new Font("Segoe UI Semibold", 10f);
    }

    /// <summary>Flat owner-drawn button with hover and accent variants.</summary>
    internal sealed class FlatBtn : Button
    {
        private bool _hover;
        private bool _down;
        public bool Accent { get; set; }
        public bool Ghost { get; set; }
        public int Radius { get; set; }

        public FlatBtn()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                     | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
            ForeColor = Pal.Text;
            Font = Pal.Bold;
            Cursor = Cursors.Hand;
            Radius = 5;
            Height = 30;
        }

        protected override void OnMouseEnter(EventArgs e) { _hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { _hover = false; _down = false; Invalidate(); base.OnMouseLeave(e); }
        protected override void OnMouseDown(MouseEventArgs e) { _down = true; Invalidate(); base.OnMouseDown(e); }
        protected override void OnMouseUp(MouseEventArgs e) { _down = false; Invalidate(); base.OnMouseUp(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            Color fill;
            Color border;
            if (Accent)
            {
                fill = _down ? Pal.AccentDark : (_hover ? ControlPaint.Light(Pal.Accent, 0.15f) : Pal.Accent);
                border = fill;
            }
            else if (Ghost)
            {
                fill = _hover ? Pal.Hover : Color.Transparent;
                border = Pal.Line;
            }
            else
            {
                fill = _down ? Pal.PanelAlt : (_hover ? Pal.Hover : Pal.Panel);
                border = _hover ? Pal.Accent : Pal.Line;
            }

            Rectangle r = new Rectangle(0, 0, Width - 1, Height - 1);
            using (var path = RoundRect(r, Radius))
            {
                if (fill != Color.Transparent)
                {
                    using (var b = new SolidBrush(fill)) { g.FillPath(b, path); }
                }
                using (var p = new Pen(border)) { g.DrawPath(p, path); }
            }

            Color fg = Accent ? Color.FromArgb(12, 22, 30) : (Enabled ? ForeColor : Pal.Dim);
            TextRenderer.DrawText(g, Text, Font, r, fg,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
        }

        internal static System.Drawing.Drawing2D.GraphicsPath RoundRect(Rectangle r, int radius)
        {
            var path = new System.Drawing.Drawing2D.GraphicsPath();
            if (radius <= 0)
            {
                path.AddRectangle(r);
                return path;
            }
            int d = radius * 2;
            path.AddArc(r.X, r.Y, d, d, 180, 90);
            path.AddArc(r.Right - d, r.Y, d, d, 270, 90);
            path.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
            path.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
            path.CloseFigure();
            return path;
        }
    }

    /// <summary>Dark input box with a thin accent underline on focus.</summary>
    internal sealed class DarkInput : TextBox
    {
        public DarkInput()
        {
            BorderStyle = BorderStyle.None;
            BackColor = Pal.PanelAlt;
            ForeColor = Pal.Text;
            Font = Pal.Base;
        }
    }

    /// <summary>Accent progress bar.</summary>
    internal sealed class Slider : Control
    {
        private int _min;
        private int _max = 100;
        private int _value = 100;
        private bool _drag;

        public event EventHandler ValueChanged;

        public Slider()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint
                     | ControlStyles.OptimizedDoubleBuffer, true);
            Height = 22;
            TabStop = true;
        }

        public int Minimum { get { return _min; } set { _min = value; Invalidate(); } }
        public int Maximum { get { return _max; } set { _max = value; Invalidate(); } }

        public int Value
        {
            get { return _value; }
            set
            {
                int v = Math.Max(_min, Math.Min(_max, value));
                if (v == _value) { return; }
                _value = v;
                Invalidate();
                if (ValueChanged != null) { ValueChanged(this, EventArgs.Empty); }
            }
        }

        private void SetFromX(int x)
        {
            int pad = 8;
            int w = Math.Max(1, Width - pad * 2);
            double ratio = (double)(x - pad) / w;
            ratio = Math.Max(0, Math.Min(1, ratio));
            Value = _min + (int)Math.Round(ratio * (_max - _min));
        }

        protected override void OnMouseDown(MouseEventArgs e)
        {
            _drag = true;
            SetFromX(e.X);
            base.OnMouseDown(e);
        }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            if (_drag) { SetFromX(e.X); }
            base.OnMouseMove(e);
        }

        protected override void OnMouseUp(MouseEventArgs e)
        {
            _drag = false;
            base.OnMouseUp(e);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;
            int pad = 8;
            int cy = Height / 2;
            int trackW = Width - pad * 2;
            float ratio = _max == _min ? 0 : (float)(_value - _min) / (_max - _min);
            int knobX = pad + (int)(trackW * ratio);
            using (var track = new Pen(Pal.PanelAlt, 6f))
            {
                track.StartCap = System.Drawing.Drawing2D.LineCap.Round;
                track.EndCap = System.Drawing.Drawing2D.LineCap.Round;
                g.DrawLine(track, pad, cy, Width - pad, cy);
            }
            using (var fill = new Pen(Pal.Accent, 6f))
            {
                fill.StartCap = System.Drawing.Drawing2D.LineCap.Round;
                fill.EndCap = System.Drawing.Drawing2D.LineCap.Round;
                g.DrawLine(fill, pad, cy, knobX, cy);
            }
            using (var b = new SolidBrush(Color.White))
            {
                g.FillEllipse(b, knobX - 6, cy - 6, 12, 12);
            }
        }
    }
}
