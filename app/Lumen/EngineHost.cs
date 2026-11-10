using System;
using System.Diagnostics;
using System.IO;
using System.Collections.Generic;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace Lumen
{
    /// <summary>
    /// Runs the video engine in a separate process. The child connects to the UI first and
    /// only then creates libvlc, so the UI always learns what happened instead of waiting
    /// for a timeout. A native crash inside the decoder can only kill this helper.
    /// </summary>
    internal static class EngineHost
    {
        public const string Flag = "--engine-host";

        public static int Run(string[] args)
        {
            string pipeName = args != null && args.Length > 1 ? args[1] : null;
            try
            {
                Trace2.Write("engine: child start, pipe=" + (pipeName ?? "<none>"));
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Controller controller = new Controller();
                controller.Start(pipeName);
                Application.Run(controller);
                Trace2.Write("engine: child loop finished");
                return 0;
            }
            catch (Exception ex)
            {
                Trace2.Write("engine: child failed -> " + ex);
                return 3;
            }
        }

        private sealed class Controller : ApplicationContext
        {
            private Native.VlcEngine _engine;
            private WallpaperWindow _window;
            private Native.VlcSurface _surface;
            private NamedPipeServerStream _pipe;
            private StreamReader _reader;
            private StreamWriter _writer;
            private Control _marshal;
            private readonly System.Windows.Forms.Timer _pump = new System.Windows.Forms.Timer { Interval = 250 };
            private volatile bool _quit;
            private int _ticks;
            private string _lastFile;
            private int _playbackTicks;
            private bool _reportedFailure;

            public void Start(string pipeName)
            {
                // A control on this thread gives us a safe way to run engine work on the
                // UI thread while the pipe reader waits in the background.
                _marshal = new Control();
                _marshal.CreateControl();

                _pump.Tick += (s, e) => Pump();
                _pump.Start();

                ThreadPool.QueueUserWorkItem(delegate { AcceptLoop(pipeName); });
            }

            private void AcceptLoop(string pipeName)
            {
                try
                {
                    if (string.IsNullOrEmpty(pipeName))
                    {
                        Trace2.Write("engine: no pipe given, idling");
                        return;
                    }
                    _pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
                        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
                    Trace2.Write("engine: waiting for ui");
                    if (!_pipe.WaitForConnectionAsync().Wait(30000))
                    {
                        Trace2.Write("engine: ui never connected");
                        _quit = true;
                        return;
                    }
                    _reader = new StreamReader(_pipe, Encoding.UTF8);
                    _writer = new StreamWriter(_pipe, new UTF8Encoding(false)) { AutoFlush = true };
                    Trace2.Write("engine: ui connected");
                    // Handshake happens before any native code is touched.
                    Reply("OK|ready");

                    string line;
                    while (!_quit && (line = _reader.ReadLine()) != null)
                    {
                        Handle(line);
                    }
                }
                catch (Exception ex)
                {
                    Trace2.Write("engine: pipe closed -> " + ex.Message);
                }
                finally
                {
                    _quit = true;
                }
            }

            private void Handle(string line)
            {
                try
                {
                    string[] p = line.Split('|');
                    Trace2.Write("engine: cmd " + (p.Length > 1 ? p[0] + " (" + Path.GetFileName(p[1]) + ")" : line));

                    switch (p[0])
                    {
                        case "PLAY":
                            RunOnUi(delegate
                            {
                                EnsureSurface();
                                Play(p[1], Get(p, 2, "100"), Get(p, 3, "0") == "1",
                                     Get(p, 4, "cover"), Get(p, 5, "100"), Get(p, 6, "0"));
                            });
                            Reply("OK");
                            break;
                        case "VOL":
                            RunOnUi(delegate
                            {
                                if (_surface == null) { return; }
                                int vol = int.Parse(p[1]);
                                _surface.VolumeRequested = vol;
                                _surface.ApplyAudio(vol, int.Parse(p[2]));
                                _surface.Muted = p[3] == "1";
                            });
                            Reply("OK");
                            break;
                        case "FIT":
                            RunOnUi(delegate
                            {
                                if (_surface != null) { _surface.AspectRatio = p[1] == "stretch" ? "16:9" : null; }
                            });
                            Reply("OK");
                            break;
                        case "SPEED":
                            RunOnUi(delegate { if (_surface != null) { _surface.Rate = int.Parse(p[1]) / 100f; } });
                            Reply("OK");
                            break;
                        case "EQ":
                            RunOnUi(delegate { if (_surface != null) { _surface.ApplyEqualizerPreset(int.Parse(p[1])); } });
                            Reply("OK");
                            break;
                        case "STOP":
                            RunOnUi(delegate { if (_surface != null) { _surface.Stop(); } });
                            Reply("OK");
                            break;
                        case "QUIT":
                            Reply("OK");
                            _quit = true;
                            break;
                        default:
                            Reply("ERR|unknown command");
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Trace2.Write("engine: handler error -> " + ex.Message);
                    Reply("ERR|" + ex.Message.Replace('\n', ' ').Replace('\r', ' '));
                }
            }

            private static string Get(string[] parts, int index, string fallback)
            {
                return parts.Length > index && !string.IsNullOrEmpty(parts[index]) ? parts[index] : fallback;
            }

            private void RunOnUi(Action action)
            {
                if (_marshal.InvokeRequired)
                {
                    _marshal.Invoke(action);
                }
                else
                {
                    action();
                }
            }

            /// <summary>
            /// Verifies that decoding actually produced frames and reports problems back to
            /// the UI, so "applied" never means "silently nothing".
            /// </summary>
            private void CheckPlayback()
            {
                if (_surface == null || _lastFile == null || _reportedFailure) { return; }
                _playbackTicks++;
                if (_playbackTicks % 8 != 0) { return; }

                uint w, h;
                _surface.GetVideoSize(out w, out h);
                bool playing = _surface.IsPlaying;
                string host = _window == null ? "none" : _window.HostKind;

                if (w > 0 && h > 0 && playing)
                {
                    Reply("STATUS|playing|" + w + "x" + h + "|" + host);
                    _reportedFailure = true;   // report success once
                    return;
                }

                if (_playbackTicks < 32) { return; }   // give the decoder a few seconds

                string error = _surface.LastError;
                Reply("STATUS|failed|" + (error == null ? "нет кадров" : error.Replace('|', ' ').Replace('\n', ' '))
                      + "|" + host + "|" + (playing ? "playing" : "stopped"));
                _reportedFailure = true;
            }

            /// <summary>Engine and desktop surface are created on the first request only.</summary>
            private void EnsureSurface()
            {
                if (_surface != null) { return; }

                Trace2.Write("engine: creating libvlc instance");
                _engine = new Native.VlcEngine();
                Trace2.Write("engine: vlc " + Native.VlcEngine.Version);

                _window = new WallpaperWindow(false);
                _window.Show();
                _window.AttachToDesktop();
                Trace2.Write("engine: surface attached=" + _window.Attached);

                _surface = _engine.CreatePlayer();
                _surface.BindWindow(_window.SurfaceHandle);
            }

            private void Play(string file, string volume, bool muted, string fit, string speed, string eq)
            {
                if (!File.Exists(file)) { throw new FileNotFoundException("файл не найден", file); }
                if (!string.Equals(_lastFile, file, StringComparison.OrdinalIgnoreCase))
                {
                    Trace2.Write("engine: load " + file);
                    _surface.Load(file, true, muted || volume == "0");
                    _lastFile = file;
                    Trace2.Write("engine: loaded");
                }
                _surface.VolumeRequested = int.Parse(volume);
                _surface.ApplyAudio(int.Parse(volume), int.Parse(eq));
                _surface.Muted = muted || volume == "0";
                _surface.AspectRatio = fit == "stretch" ? "16:9" : null;
                _surface.Rate = int.Parse(speed) / 100f;
                _surface.Play();
                Trace2.Write("engine: play issued");
            }

            private void Reply(string text)
            {
                if (_writer == null) { return; }
                try { _writer.WriteLine(text); }
                catch (Exception ex) { Trace2.Write("engine: reply failed -> " + ex.Message); }
            }

            private void Pump()
            {
                _ticks++;
                CheckPlayback();
                if (_ticks % 20 == 0)
                {
                    Trace2.Write("engine: alive tick " + _ticks + ", playing=" + (_surface != null && _surface.IsPlaying));
                }
                if (!_quit) { return; }

                _pump.Stop();
                if (_surface != null) { _surface.Dispose(); _surface = null; }
                if (_engine != null) { _engine.Dispose(); _engine = null; }
                if (_window != null)
                {
                    _window.DetachFromDesktop();
                    _window.Hide();
                    _window.Dispose();
                    _window = null;
                }
                ExitThread();
            }
        }
    }

    /// <summary>One playback report coming from the engine process.</summary>
    internal sealed class EngineStatus : EventArgs
    {
        public string Kind;
        public string Detail;
        public string Host;
        public string Extra;
    }

    /// <summary>UI-side handle to the engine helper process.</summary>
    internal sealed class EngineClient : IDisposable
    {
        private Process _process;
        private NamedPipeClientStream _pipe;
        private StreamReader _reader;
        private StreamWriter _writer;
        private string _pipeName;
        private readonly object _io = new object();
        private volatile string _lastError;
        private Thread _reader_thread;

        /// <summary>Raised for unsolicited STATUS lines from the engine process.</summary>
        public event EventHandler<EngineStatus> Status;

        internal void RaiseStatus(string line)
        {
            string[] p = line.Split('|');
            EngineStatus status = new EngineStatus
            {
                Kind = p.Length > 1 ? p[1] : "unknown",
                Detail = p.Length > 2 ? p[2] : "",
                Host = p.Length > 3 ? p[3] : "",
                Extra = p.Length > 4 ? p[4] : ""
            };
            EventHandler<EngineStatus> handler = Status;
            if (handler != null) { try { handler(this, status); } catch { } }
        }

        /// <summary>
        /// Background reader: keeps draining the pipe so unsolicited status messages are
        /// delivered even when the UI is not sending commands.
        /// </summary>
        private void StartReader()
        {
            _reader_thread = new Thread(delegate ()
            {
                try
                {
                    while (true)
                    {
                        string line = _reader.ReadLine();
                        if (line == null) { break; }
                        if (line.StartsWith("STATUS")) { RaiseStatus(line); }
                        else if (line.StartsWith("ERR")) { _lastError = line.Substring(4); Trace2.Write("ui: engine error " + line); }
                        else
                        {
                            lock (_io) { _replies.Enqueue(line); Monitor.PulseAll(_io); }
                        }
                    }
                }
                catch { }
            });
            _reader_thread.IsBackground = true;
            _reader_thread.Start();
        }

        private readonly Queue<string> _replies = new Queue<string>();

        public bool IsRunning
        {
            get
            {
                try { return _process != null && !_process.HasExited; }
                catch { return false; }
            }
        }

        public string LastError
        {
            get { return _lastError; }
            private set { _lastError = value; }
        }

        /// <summary>Preset list of the bundled engine as plain text (no native calls).</summary>
        public static string DefaultEqualizerNames()
        {
            return "Без эквалайзера|Classical|Club|Dance|Full bass|Full bass and treble|Full treble|"
                 + "Headphones|Large Hall|Live|Party|Pop|Reggae|Rock|Ska|Soft|Soft rock|Techno";
        }

        public bool Start()
        {
            Stop();
            try
            {
                _pipeName = "lumen-" + Process.GetCurrentProcess().Id + "-" + Guid.NewGuid().ToString("N").Substring(0, 8);
                ProcessStartInfo psi = new ProcessStartInfo(Application.ExecutablePath, EngineHost.Flag + " " + _pipeName)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true,
                    WorkingDirectory = AppDomain.CurrentDomain.BaseDirectory
                };
                _process = Process.Start(psi);
                if (_process == null) { LastError = "не удалось запустить процесс движка"; return false; }

                _pipe = new NamedPipeClientStream(".", _pipeName, PipeDirection.InOut);
                try
                {
                    _pipe.Connect(12000);
                }
                catch (TimeoutException)
                {
                    LastError = IsRunning
                        ? "движок не подключился"
                        : "процесс движка завершился при запуске";
                    Trace2.Write("ui: " + LastError);
                    return false;
                }

                _reader = new StreamReader(_pipe, Encoding.UTF8);
                _writer = new StreamWriter(_pipe, new UTF8Encoding(false)) { AutoFlush = true };

                string hello = _reader.ReadLine();
                Trace2.Write("ui: engine hello = " + hello);
                if (hello == null || !hello.StartsWith("OK"))
                {
                    LastError = "движок не ответил на рукопожатие";
                    return false;
                }
                LastError = null;
                StartReader();
                return true;
            }
            catch (Exception ex)
            {
                Trace2.Write("ui: engine start failed -> " + ex.Message);
                LastError = ex.Message;
                return false;
            }
        }

        /// <summary>Queued command: the UI thread never waits for the engine.</summary>
        public void Post(string command)
        {
            if (_writer == null) { return; }
            ThreadPool.QueueUserWorkItem(delegate
            {
                try
                {
                    lock (_io)
                    {
                        if (_writer == null) { return; }
                        _writer.WriteLine(command);
                    }
                }
                catch (Exception ex)
                {
                    LastError = ex.Message;
                    Trace2.Write("ui: post failed -> " + ex.Message);
                }
            });
        }

        /// <summary>Blocking request/response, for diagnostics only.</summary>
        public string Send(string command)
        {
            if (!IsRunning || _writer == null) { LastError = "движок не запущен"; return null; }
            try
            {
                lock (_io)
                {
                    _replies.Clear();
                    _writer.WriteLine(command);
                    DateTime deadline = DateTime.UtcNow.AddSeconds(20);
                    while (_replies.Count == 0 && DateTime.UtcNow < deadline)
                    {
                        Monitor.Wait(_io, 250);
                    }
                    if (_replies.Count == 0) { LastError = "нет ответа движка"; return null; }
                    return _replies.Dequeue();
                }
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                return null;
            }
        }

        public void Stop()
        {
            try { if (IsRunning) { Post("QUIT"); } } catch { }
            try { if (_pipe != null) { _pipe.Dispose(); } } catch { }
            _pipe = null;
            _reader = null;
            _writer = null;
            try
            {
                if (_process != null && !_process.HasExited && !_process.WaitForExit(2000)) { _process.Kill(); }
            }
            catch { }
            try { if (_process != null) { _process.Dispose(); } } catch { }
            _process = null;
        }

        public void Dispose()
        {
            Stop();
        }
    }
}
