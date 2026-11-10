using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace VidDesk.Ui
{
    /// <summary>Main shell: library rail on the left, live-preview and properties on the right.</summary>
    internal sealed class MainWindow : Form
    {
        private readonly ListView _library;
        private readonly Panel _previewHost;
        private readonly Label _previewHint;
        private Label _titleLabel;
        private Label _pathLabel;
        private Slider _volume;
        private Label _volumeValue;
        private Slider _speed;
        private Label _speedValue;
        private ComboBox _fit;
        private ComboBox _eq;
        private CheckBox _muted;
        private CheckBox _playlist;
        private CheckBox _silent;
        private readonly Label _status;

        private readonly Dictionary<string, Bitmap> _thumbs = new Dictionary<string, Bitmap>(StringComparer.OrdinalIgnoreCase);
        private readonly ImageList _tiles = new ImageList { ImageSize = new Size(176, 99), ColorDepth = ColorDepth.Depth32Bit };
        private PreviewSurface _preview;
        private bool _syncing;
        private bool _ready;
        private readonly System.Windows.Forms.Timer _cycle;
        private readonly Random _random = new Random();

        public MainWindow()
        {
            SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer, true);
            Text = "VidDesk — живые обои для рабочего стола";
            BackColor = Pal.Bg;
            ForeColor = Pal.Text;
            Font = Pal.Base;
            MinimumSize = new Size(980, 620);
            Size = new Size(1120, 700);
            StartPosition = FormStartPosition.CenterScreen;
            Icon = AppIcon.Load();
            KeyPreview = true;

            // Deterministic layout: TableLayoutPanel everywhere, so no dock-order surprises.
            TableLayoutPanel root = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 2,
                BackColor = Pal.Bg,
                Margin = Padding.Empty,
                Padding = Padding.Empty
            };
            root.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 58f));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            Controls.Add(root);

            // ---- top bar -------------------------------------------------------
            Panel top = new Panel { Dock = DockStyle.Fill, BackColor = Pal.Panel, Margin = Padding.Empty };
            root.Controls.Add(top, 0, 0);
            top.Paint += (s2, e) => e.Graphics.DrawLine(new Pen(Pal.Line), 0, top.Height - 1, top.Width, top.Height - 1);

            Label brand = new Label
            {
                Text = "VIDDESK",
                Font = new Font("Segoe UI Semibold", 13f),
                ForeColor = Pal.Text,
                AutoSize = true,
                Location = new Point(18, 17)
            };
            top.Controls.Add(brand);

            Label tag = new Label
            {
                Text = "live wallpapers",
                Font = Pal.Small,
                ForeColor = Pal.Dim,
                AutoSize = true,
                Location = new Point(112, 24)
            };
            top.Controls.Add(tag);

            FlatBtn addBtn = new FlatBtn { Text = "+  Добавить видео", Accent = true, Width = 156, Location = new Point(top.Width - 176, 14), Anchor = AnchorStyles.Top | AnchorStyles.Right };
            addBtn.Click += (s2, e) => AddVideos();
            top.Controls.Add(addBtn);

            FlatBtn removeBtn = new FlatBtn { Text = "Удалить", Width = 92, Location = new Point(top.Width - 280, 14), Anchor = AnchorStyles.Top | AnchorStyles.Right, Ghost = true };
            removeBtn.Click += (s2, e) => RemoveSelected();
            top.Controls.Add(removeBtn);

            TableLayoutPanel body = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 2,
                RowCount = 1,
                BackColor = Pal.Bg,
                Margin = Padding.Empty,
                Padding = Padding.Empty
            };
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 268f));
            body.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            body.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            root.Controls.Add(body, 0, 1);

            // ---- left library rail --------------------------------------------
            TableLayoutPanel left = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 3,
                BackColor = Pal.Bg,
                Padding = new Padding(14, 12, 8, 6),
                Margin = Padding.Empty
            };
            left.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            left.RowStyles.Add(new RowStyle(SizeType.Absolute, 24f));
            left.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            left.RowStyles.Add(new RowStyle(SizeType.Absolute, 46f));
            body.Controls.Add(left, 0, 0);

            Label libTitle = new Label { Text = "БИБЛИОТЕКА", Font = Pal.Small, ForeColor = Pal.Dim, Dock = DockStyle.Fill, Margin = Padding.Empty };
            left.Controls.Add(libTitle, 0, 0);

            _library = new ListView
            {
                Dock = DockStyle.Fill,
                View = View.LargeIcon,
                LargeImageList = _tiles,
                BackColor = Pal.Bg,
                ForeColor = Pal.Text,
                BorderStyle = BorderStyle.None,
                MultiSelect = false,
                FullRowSelect = false,
                LabelWrap = true,
                HideSelection = false,
                Margin = Padding.Empty
            };
            _library.SelectedIndexChanged += (s2, e) => SyncFromSelection();
            _library.DoubleClick += (s2, e) => ApplySelected();
            left.Controls.Add(_library, 0, 1);

            _status = new Label
            {
                Dock = DockStyle.Fill,
                ForeColor = Pal.Dim,
                Font = Pal.Small,
                Margin = Padding.Empty,
                Text = "Движок: VLC " + Native.VlcEngine.Version
            };
            left.Controls.Add(_status, 0, 2);

            // ---- right side ----------------------------------------------------
            TableLayoutPanel right = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 4,
                BackColor = Pal.Panel,
                Padding = new Padding(16, 10, 16, 10),
                Margin = Padding.Empty
            };
            right.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            right.RowStyles.Add(new RowStyle(SizeType.Absolute, 22f));
            right.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            right.RowStyles.Add(new RowStyle(SizeType.Absolute, 44f));
            right.RowStyles.Add(new RowStyle(SizeType.Absolute, 244f));
            body.Controls.Add(right, 1, 0);

            Label previewTitle = new Label { Text = "ПРЕДПРОСМОТР", Font = Pal.Small, ForeColor = Pal.Dim, Dock = DockStyle.Fill, Margin = Padding.Empty };
            right.Controls.Add(previewTitle, 0, 0);

            _previewHost = new Panel { Dock = DockStyle.Fill, BackColor = Color.Black, Margin = new Padding(0, 0, 0, 6) };
            right.Controls.Add(_previewHost, 0, 1);

            _previewHint = new Label
            {
                Text = "Выберите видео в библиотеке и нажмите «Применить».\nРолик ляжет на рабочий стол под иконками.",
                TextAlign = ContentAlignment.MiddleCenter,
                Dock = DockStyle.Fill,
                ForeColor = Pal.Dim,
                BackColor = Color.Transparent
            };
            _previewHost.Controls.Add(_previewHint);

            right.Controls.Add(BuildButtons(), 0, 2);

            Panel props = BuildProperties();
            props.Dock = DockStyle.Fill;
            props.Margin = Padding.Empty;
            right.Controls.Add(props, 0, 3);

            _titleLabel = (Label)props.Controls["title"];
            _pathLabel = (Label)props.Controls["path"];
            _volume = (Slider)props.Controls["volume"];
            _volumeValue = (Label)props.Controls["volumeValue"];
            _speed = (Slider)props.Controls["speed"];
            _speedValue = (Label)props.Controls["speedValue"];
            _fit = (ComboBox)props.Controls["fit"];
            _eq = (ComboBox)props.Controls["eq"];
            _muted = (CheckBox)props.Controls["muted"];
            _playlist = (CheckBox)props.Controls["playlist"];
            _silent = (CheckBox)props.Controls["silent"];

            _volume.ValueChanged += (s, e) => VolumeChanged();
            _speed.ValueChanged += (s, e) => SpeedChanged();
            _fit.SelectedIndexChanged += (s, e) => FitChanged();
            _eq.SelectedIndexChanged += (s, e) => EqChanged();
            _muted.CheckedChanged += (s, e) => MuteChanged();
            _playlist.CheckedChanged += (s, e) => PlaylistChanged();
            _silent.CheckedChanged += (s, e) => SilentChanged();

            if (Program.Engine != null)
            {
                _preview = new PreviewSurface(Program.Engine);
            }
            else
            {
                _previewHint.Text = "Медиадвижок не загрузился.\n" + (Program.LastError ?? "Подробности в логе.") 
                    + "\nЛог: " + Trace2.Path;
            }

            LoadLibrary();
            if (Program.Wallpaper != null) { Program.Wallpaper.StateChanged += (s, e) => UpdateStatus(); }

            _cycle = new System.Windows.Forms.Timer { Interval = 1000 };
            _cycle.Tick += (s, e) => CycleTick();
            _cycle.Start();
            _cycleStart = DateTime.UtcNow;

            Resize += (s, e) => LayoutPreview();
            Move += (s, e) => LayoutPreview();
            Shown += (s, e) =>
            {
                Trace2.Step("main: shown");
                _ready = true;
                LayoutPreview();
                UpdateStatus();
                StartPreview(Selected);
            };
            ResizeBegin += (s, e) => { if (_preview != null) _preview.Hide(); };
            ResizeEnd += (s, e) => LayoutPreview();
            Deactivate += (s, e) => { if (_preview != null) _preview.Hide(); };
            Activated += (s, e) => SyncFromSelection();
            VisibleChanged += (s, e) => { if (!Visible && _preview != null) { _preview.Hide(); } };
            FormClosing += OnClosing;
            KeyDown += (s, e) => { if (e.KeyCode == Keys.F5) ApplySelected(); };
        }

        private Panel BuildProperties()
        {
            Panel props = new Panel { BackColor = Pal.Panel };

            _titleLabel = new Label { Name = "title", Text = "Ничего не выбрано", Font = Pal.Title, ForeColor = Pal.Text, Location = new Point(0, 4), Width = 560, AutoEllipsis = true, Height = 30 };
            props.Controls.Add(_titleLabel);

            _pathLabel = new Label { Name = "path", Text = "", Font = Pal.Small, ForeColor = Pal.Dim, Location = new Point(2, 36), Width = 700, AutoEllipsis = true, Height = 18 };
            props.Controls.Add(_pathLabel);

            Label volLbl = new Label { Text = "Громкость", ForeColor = Pal.Dim, Font = Pal.Small, Location = new Point(0, 66), AutoSize = true };
            props.Controls.Add(volLbl);

            Slider vol = new Slider { Name = "volume", Minimum = 0, Maximum = 500, Value = 100, Location = new Point(0, 86), Width = 330 };
            props.Controls.Add(vol);

            Label volVal = new Label { Name = "volumeValue", Text = "100 %", ForeColor = Pal.Accent, Font = Pal.Num, Location = new Point(340, 86), Width = 64, Height = 22, TextAlign = ContentAlignment.MiddleLeft };
            props.Controls.Add(volVal);

            Label spdLbl = new Label { Text = "Скорость", ForeColor = Pal.Dim, Font = Pal.Small, Location = new Point(0, 118), AutoSize = true };
            props.Controls.Add(spdLbl);

            Slider spd = new Slider { Name = "speed", Minimum = 25, Maximum = 200, Value = 100, Location = new Point(0, 138), Width = 330 };
            props.Controls.Add(spd);

            Label spdVal = new Label { Name = "speedValue", Text = "1.00x", ForeColor = Pal.Accent, Font = Pal.Num, Location = new Point(340, 138), Width = 64, Height = 22, TextAlign = ContentAlignment.MiddleLeft };
            props.Controls.Add(spdVal);

            Label fitLbl = new Label { Text = "Кадрирование", ForeColor = Pal.Dim, Font = Pal.Small, Location = new Point(430, 66), AutoSize = true };
            props.Controls.Add(fitLbl);
            ComboBox fit = new ComboBox { Name = "fit", DropDownStyle = ComboBoxStyle.DropDownList, Location = new Point(430, 86), Width = 220, BackColor = Pal.PanelAlt, ForeColor = Pal.Text, FlatStyle = FlatStyle.Flat };
            fit.Items.AddRange(new object[] { "Заполнить экран (cover)", "Вписать целиком (contain)", "Растянуть" });
            fit.SelectedIndex = 0;
            props.Controls.Add(fit);

            Label eqLbl = new Label { Text = "Эквалайзер", ForeColor = Pal.Dim, Font = Pal.Small, Location = new Point(430, 118), AutoSize = true };
            props.Controls.Add(eqLbl);
            ComboBox eq = new ComboBox { Name = "eq", DropDownStyle = ComboBoxStyle.DropDownList, Location = new Point(430, 138), Width = 220, BackColor = Pal.PanelAlt, ForeColor = Pal.Text, FlatStyle = FlatStyle.Flat };
            foreach (string preset in Native.VlcSurface.EqualizerPresetNames()) { eq.Items.Add(preset); }
            eq.SelectedIndex = 0;
            props.Controls.Add(eq);

            CheckBox muted = new CheckBox { Name = "muted", Text = "Без звука", ForeColor = Pal.Text, Location = new Point(680, 66), AutoSize = true, FlatStyle = FlatStyle.Flat };
            props.Controls.Add(muted);

            CheckBox playlist = new CheckBox { Name = "playlist", Text = "В плейлист ротации", ForeColor = Pal.Text, Location = new Point(680, 92), AutoSize = true, FlatStyle = FlatStyle.Flat };
            props.Controls.Add(playlist);

            CheckBox silent = new CheckBox { Name = "silent", Text = "Тихий режим (глушить всё)", ForeColor = Pal.Text, Location = new Point(680, 118), AutoSize = true, FlatStyle = FlatStyle.Flat };
            silent.Checked = Program.State.SilentMode;
            props.Controls.Add(silent);

            CheckBox autoRun = new CheckBox { Text = "Запускать с Windows", ForeColor = Pal.Text, Location = new Point(680, 144), AutoSize = true, FlatStyle = FlatStyle.Flat };
            autoRun.Checked = Program.State.AutoStart;
            autoRun.CheckedChanged += (s, e) =>
            {
                Program.State.AutoStart = autoRun.Checked;
                Program.EnableAutoStart(autoRun.Checked);
                SettingsStore.Save(Program.State);
            };
            props.Controls.Add(autoRun);

            CheckBox rotate = new CheckBox { Text = "Ротация каждые 5 минут", ForeColor = Pal.Text, Location = new Point(680, 170), AutoSize = true, FlatStyle = FlatStyle.Flat };
            rotate.Checked = Program.State.CycleSeconds > 0;
            rotate.CheckedChanged += (s, e) =>
            {
                Program.State.CycleSeconds = rotate.Checked ? 300 : 0;
                _cycleStart = DateTime.UtcNow;
                SettingsStore.Save(Program.State);
            };
            props.Controls.Add(rotate);

            Label hint = new Label
            {
                Text = "Громкость до 500 % (усиление) · F5 — применить · ПКМ по значку в трее — меню",
                ForeColor = Pal.Dim,
                Font = Pal.Small,
                Location = new Point(0, 196),
                AutoSize = true
            };
            props.Controls.Add(hint);

            return props;
        }

        private Panel BuildButtons()
        {
            Panel bar = new Panel { Dock = DockStyle.Fill, BackColor = Pal.Panel, Margin = Padding.Empty };
            FlatBtn apply = new FlatBtn { Text = "Применить на рабочий стол", Accent = true, Width = 208, Location = new Point(0, 6) };
            apply.Click += (s, e) => ApplySelected();
            bar.Controls.Add(apply);

            FlatBtn stop = new FlatBtn { Text = "Остановить", Width = 108, Location = new Point(218, 6), Ghost = true };
            stop.Click += (s, e) =>
            {
                if (Program.Wallpaper != null) { Program.Wallpaper.Stop(); }
                Program.State.WallpaperEnabled = false;
                SettingsStore.Save(Program.State);
            };
            bar.Controls.Add(stop);

            FlatBtn open = new FlatBtn { Text = "Открыть папку", Width = 124, Location = new Point(336, 6), Ghost = true };
            open.Click += (s, e) => OpenDataFolder();
            bar.Controls.Add(open);
            return bar;
        }

        private void LayoutPreview()
        {
            if (_previewHost == null || _previewHost.Width < 50) { return; }
            // 16:9 letterbox inside the host.
            int w = _previewHost.Width;
            int h = (int)(w * 9.0 / 16.0);
            if (h > _previewHost.Height)
            {
                h = _previewHost.Height;
                w = (int)(h * 16.0 / 9.0);
            }
            if (w < 2 || h < 2) { return; }
            _previewHint.Bounds = new Rectangle((_previewHost.Width - w) / 2, (int)((_previewHost.Height - h) * 0.42), w, h);

            try
            {
                if (_preview != null && Visible && WindowState != FormWindowState.Minimized)
                {
                    Rectangle screen = _previewHost.RectangleToScreen(_previewHint.Bounds);
                    _preview.SyncPosition(screen);
                }
            }
            catch (Exception ex)
            {
                Trace2.Write("preview layout failed: " + ex.Message);
            }
        }

        private void StartPreview(VideoEntry entry)
        {
            if (!_ready) { return; }
            if (entry == null || _preview == null) { return; }
            try
            {
                LayoutPreview();
                _previewHint.Text = "";
                Rectangle screen = _previewHost.RectangleToScreen(_previewHint.Bounds);
                _preview.SyncPosition(screen);
                _preview.Show(entry.File, this);
                _preview.SyncPosition(screen);
            }
            catch (Exception ex)
            {
                Trace2.Write("preview failed: " + ex.Message);
            }
        }

        private DateTime _cycleStart;
        private void CycleTick()
        {
            try { CycleTickCore(); }
            catch (Exception ex) { Trace2.Write("rotation failed: " + ex.Message); }
        }

        private void CycleTickCore()
        {
            if (Program.Wallpaper == null) { return; }
            if (!Program.State.WallpaperEnabled || Program.State.CycleSeconds <= 0) { return; }
            if (!Program.Wallpaper.IsActive) { return; }
            if ((DateTime.UtcNow - _cycleStart).TotalSeconds < Program.State.CycleSeconds) { return; }
            _cycleStart = DateTime.UtcNow;

            List<VideoEntry> pool = new List<VideoEntry>();
            foreach (VideoEntry e in Program.State.Library)
            {
                if (e.Playlist && System.IO.File.Exists(e.File)) { pool.Add(e); }
            }
            if (pool.Count == 0) { return; }
            VideoEntry next;
            if (Program.State.Shuffle)
            {
                next = pool[_random.Next(pool.Count)];
            }
            else
            {
                int idx = pool.FindIndex(x => string.Equals(x.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase));
                next = pool[(idx + 1 + pool.Count) % pool.Count];
            }
            try
            {
                Program.State.ActiveFile = next.File;
                Program.Wallpaper.Play(next, Program.State.SilentMode);
                SelectActive();
                SettingsStore.Save(Program.State);
            }
            catch { }
        }

        private void LoadLibrary()
        {
            _library.BeginUpdate();
            _library.Items.Clear();
            _tiles.Images.Clear();
            _thumbs.Clear();
            foreach (VideoEntry entry in Program.State.Library)
            {
                AddTile(entry);
            }
            _library.EndUpdate();
            SelectActive();
        }

        private void AddTile(VideoEntry entry)
        {
            Bitmap thumb;
            string key = entry.File.ToLowerInvariant();
            if (!_thumbs.TryGetValue(key, out thumb))
            {
                thumb = MediaThumb.Placeholder(entry.Display, 176, 99);
                _thumbs[key] = thumb;
            }
            _tiles.Images.Add(key, thumb);
            ListViewItem item = new ListViewItem(entry.Display)
            {
                Tag = entry,
                ImageKey = key,
                ToolTipText = entry.File
            };
            _library.Items.Add(item);

            string file = entry.File;
            string title = entry.Display;
            MediaThumb.Begin(file, 176, 99, bmp =>
            {
                if (IsDisposed) { bmp.Dispose(); return; }
                try
                {
                    BeginInvoke((Action)(() =>
                    {
                        if (IsDisposed) { return; }
                        if (_thumbs.ContainsKey(key)) { _thumbs[key].Dispose(); }
                        _thumbs[key] = bmp;
                        if (!_tiles.Images.ContainsKey(key)) { _tiles.Images.Add(key, bmp); }
                        _library.Invalidate();
                    }));
                }
                catch { }
            });
        }

        private void SelectActive()
        {
            foreach (ListViewItem item in _library.Items)
            {
                VideoEntry e = item.Tag as VideoEntry;
                if (e != null && string.Equals(e.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase))
                {
                    item.Selected = true;
                    item.Focused = true;
                    return;
                }
            }
            if (_library.Items.Count > 0) { _library.Items[0].Selected = true; }
        }

        private VideoEntry Selected
        {
            get
            {
                if (_library.SelectedItems.Count == 0) { return null; }
                return _library.SelectedItems[0].Tag as VideoEntry;
            }
        }

        private void SyncFromSelection()
        {
            VideoEntry e = Selected;
            _syncing = true;
            try
            {
                if (e == null)
                {
                    _titleLabel.Text = "Ничего не выбрано";
                    _pathLabel.Text = "";
                    return;
                }
                _titleLabel.Text = e.Display;
                _pathLabel.Text = e.File;
                _volume.Value = Math.Max(0, Math.Min(500, e.Volume));
                _volumeValue.Text = e.Volume + " %";
                _speed.Value = Math.Max(25, Math.Min(200, e.Speed));
                _speedValue.Text = (e.Speed / 100.0).ToString("0.00") + "x";
                _fit.SelectedIndex = e.Fit == "contain" ? 1 : (e.Fit == "stretch" ? 2 : 0);
                _eq.SelectedIndex = Math.Max(0, Math.Min(_eq.Items.Count - 1, e.EqualizerPreset));
                _muted.Checked = e.Muted;
                _playlist.Checked = e.Playlist;
            }
            finally { _syncing = false; }
            StartPreview(e);
        }

        private void VolumeChanged()
        {
            _volumeValue.Text = _volume.Value + " %";
            VideoEntry e = Selected;
            if (e == null) { return; }
            e.Volume = _volume.Value;
            if (!_syncing && Program.Wallpaper != null && string.Equals(e.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase))
            {
                Program.Wallpaper.ApplyVolume();
            }
            Schedule();
        }

        private void SpeedChanged()
        {
            _speedValue.Text = (_speed.Value / 100.0).ToString("0.00") + "x";
            VideoEntry e = Selected;
            if (e == null) { return; }
            e.Speed = _speed.Value;
            if (!_syncing && Program.Wallpaper != null && string.Equals(e.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase))
            {
                Program.Wallpaper.ApplySpeed(e.Speed);
            }
            Schedule();
        }

        private void FitChanged()
        {
            VideoEntry e = Selected;
            if (e == null || _syncing) { return; }
            e.Fit = _fit.SelectedIndex == 1 ? "contain" : (_fit.SelectedIndex == 2 ? "stretch" : "cover");
            if (Program.Wallpaper != null && string.Equals(e.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase))
            {
                Program.Wallpaper.ApplyFit(e.Fit);
            }
            Schedule();
        }

        private void EqChanged()
        {
            VideoEntry e = Selected;
            if (e == null || _syncing) { return; }
            e.EqualizerPreset = _eq.SelectedIndex;
            if (Program.Wallpaper != null && string.Equals(e.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase))
            {
                Program.Wallpaper.ApplyEqualizer(e.EqualizerPreset);
            }
            Schedule();
        }

        private void MuteChanged()
        {
            VideoEntry e = Selected;
            if (e == null || _syncing) { return; }
            e.Muted = _muted.Checked;
            if (Program.Wallpaper != null && string.Equals(e.File, Program.State.ActiveFile, StringComparison.OrdinalIgnoreCase))
            {
                Program.Wallpaper.ApplyVolume();
            }
            Schedule();
        }

        private void PlaylistChanged()
        {
            VideoEntry e = Selected;
            if (e == null || _syncing) { return; }
            e.Playlist = _playlist.Checked;
            Schedule();
        }

        private void SilentChanged()
        {
            Program.State.SilentMode = _silent.Checked;
            if (Program.Wallpaper != null) { Program.Wallpaper.SetMutedOverride(_silent.Checked); }
            Schedule();
        }

        private void Schedule()
        {
            SettingsStore.Save(Program.State);
        }

        private void AddVideos()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Title = "Выберите видео для живых обоев";
                dlg.Multiselect = true;
                dlg.Filter = "Видео|*.mp4;*.webm;*.mkv;*.mov;*.avi;*.wmv;*.m4v;*.gif|Все файлы|*.*";
                if (dlg.ShowDialog(this) != DialogResult.OK) { return; }
                _library.BeginUpdate();
                foreach (string file in dlg.FileNames)
                {
                    VideoEntry entry = Program.State.AddOrGet(file);
                    bool exists = false;
                    foreach (ListViewItem it in _library.Items)
                    {
                        VideoEntry e = it.Tag as VideoEntry;
                        if (e != null && string.Equals(e.File, entry.File, StringComparison.OrdinalIgnoreCase)) { exists = true; break; }
                    }
                    if (!exists) { AddTile(entry); }
                }
                _library.EndUpdate();
                SettingsStore.Save(Program.State);
                if (_library.SelectedItems.Count == 0 && _library.Items.Count > 0)
                {
                    _library.Items[_library.Items.Count - 1].Selected = true;
                }
            }
        }

        private void RemoveSelected()
        {
            VideoEntry e = Selected;
            if (e == null) { return; }
            if (MessageBox.Show(this, "Убрать «" + e.Display + "» из библиотеки?", "VidDesk",
                    MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) { return; }
            if (Program.Wallpaper != null
                && string.Equals(e.File, Program.Wallpaper.CurrentFile, StringComparison.OrdinalIgnoreCase))
            {
                Program.Wallpaper.Stop();
            }
            Program.State.Remove(e.File);
            SettingsStore.Save(Program.State);
            LoadLibrary();
        }

        private void ApplySelected()
        {
            VideoEntry e = Selected;
            if (e == null)
            {
                MessageBox.Show(this, "Сначала добавьте видео в библиотеку.", "VidDesk",
                    MessageBoxButtons.OK, MessageBoxIcon.Information);
                return;
            }
            if (Program.Wallpaper == null)
            {
                MessageBox.Show(this, "Медиадвижок не загружен, обои запустить нельзя.\n"
                    + (Program.LastError ?? "") + "\nЛог: " + Trace2.Path,
                    "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            try
            {
                Program.State.ActiveFile = e.File;
                Program.State.WallpaperEnabled = true;
                Program.Wallpaper.Play(e, Program.State.SilentMode);
                SettingsStore.Save(Program.State);
                UpdateStatus();
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, ex.Message, "VidDesk", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private void UpdateStatus()
        {
            bool active = Program.Wallpaper != null && Program.Wallpaper.IsActive;
            string s = active
                ? "● Обои запущены: " + Path.GetFileName(Program.Wallpaper.CurrentFile)
                : "○ Обои остановлены";
            _status.Text = s + "\n" + (Program.Engine == null
                ? "Движок не загружен — см. лог"
                : "Движок: VLC " + Native.VlcEngine.Version + " · F5 — применить");
            _status.ForeColor = active ? Pal.Accent : Pal.Dim;
        }

        private void OpenDataFolder()
        {
            Directory.CreateDirectory(SettingsStore.DataFolder);
            try { System.Diagnostics.Process.Start("explorer.exe", SettingsStore.DataFolder); }
            catch { }
        }

        private void OnClosing(object sender, FormClosingEventArgs e)
        {
            if (e.CloseReason == CloseReason.UserClosing)
            {
                // Closing the window keeps wallpapers running; use the tray to quit.
                e.Cancel = true;
                Hide();
            }
            else
            {
                if (_preview != null) { _preview.Dispose(); }
                SettingsStore.Save(Program.State);
            }
        }

        private static readonly Color TileBg = Pal.Bg;
        private static readonly Color TileSelected = Pal.PanelAlt;
    }
}
