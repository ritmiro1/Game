using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace Lumen
{
    /// <summary>
    /// Hosts the video engine in its own process. The UI process never loads libvlc,
    /// so a native crash inside the decoder can only take down this helper, which the
    /// UI then reports and restarts.
    /// </summary>
    internal static class EngineHost
    {
        public const string Flag = "--engine-host";

        private const string ACK = "OK";
        private const string ERR = "ERR";

        // ------------------------------------------------------------------ child

        /// <summary>Child-process entry point. Returns the process exit code.</summary>
        public static int Run(string[] args)
        {
            string pipeName = args != null && args.Length > 1 ? args[1] : null;
            int exitCode = 1;
            try
            {
                Trace2.Write("engine: child start, pipe=" + (pipeName ?? "<none>"));
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);

                Controller controller = new Controller();
                controller.Start(pipeName);
                exitCode = 0;
            }
            catch (Exception ex)
            {
                Trace2.Write("engine: child failed -> " + ex);
                exitCode = 3;
            }
            return exitCode;
        }

        /// <summary>Wallpaper surface owned by the child process.</summary>
        private sealed class Controller : ApplicationContext
        {
            private Native.VlcEngine _engine;
            private WallpaperWindow _window;
            private Native.VlcSurface _surface;
            private NamedPipeServerStream _pipe;
            private StreamReader _reader;
            private StreamWriter _writer;
            private readonly System.Windows.Forms.Timer _pump = new System.Windows.Forms.Timer { Interval = 250 };
            private volatile bool _quit;

            public void Start(string pipeName)
            {
                _engine = new Native.VlcEngine();
                Trace2.Write("engine: vlc " + Native.VlcEngine.Version);

                _window = new WallpaperWindow(false);
                _window.Show();
                _window.AttachToDesktop();
                Trace2.Write("engine: wall attached=" + _window.Attached);

                _surface = _engine.CreatePlayer();
                _surface.BindWindow(_window.SurfaceHandle);

                _pump.Tick += (s, e) => Pump();
                _pump.Start();

                // Connection is accepted on a worker thread so the UI loop can start.
                ThreadPool.QueueUserWorkItem(delegate { AcceptLoop(pipeName); });
            }

            private void AcceptLoop(string pipeName)
            {
                try
                {
                    if (string.IsNullOrEmpty(pipeName))
                    {
                        Trace2.Write("engine: no pipe, idling");
                        return;
                    }
                    _pipe = new NamedPipeServerStream(pipeName, PipeDirection.InOut, 1,
                        PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
                    Trace2.Write("engine: waiting for ui");
                    _pipe.WaitForConnection();
                    _reader = new StreamReader(_pipe, Encoding.UTF8);
                    _writer = new StreamWriter(_pipe, new UTF8Encoding(false)) { AutoFlush = true };
                    Trace2.Write("engine: ui connected");
                    Reply(ACK + "|ready");

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
                    string[] parts = line.Split('|');
                    string cmd = parts[0];
                    Trace2.Write("engine: cmd " + line);

                    switch (cmd)
                    {
                        case "PLAY":
                            Play(parts[1], parts.Length > 2 ? parts[2] : "100",
                                 parts.Length > 3 && parts[3] == "1",
                                 parts.Length > 4 ? parts[4] : "cover",
                                 parts.Length > 5 ? parts[5] : "100",
                                 parts.Length > 6 ? parts[6] : "0");
                            Reply(ACK);
                            break;
                        case "VOL":
                            if (_surface != null)
                            {
                                _surface.VolumeRequested = int.Parse(parts[1]);
                                _surface.ApplyAudio(int.Parse(parts[1]), int.Parse(parts[2]));
                                _surface.Muted = parts[3] == "1";
                            }
                            Reply(ACK);
                            break;
                        case "FIT":
                            if (_surface != null) { ApplyFit(parts[1]); }
                            Reply(ACK);
                            break;
                        case "SPEED":
                            if (_surface != null) { _surface.Rate = int.Parse(parts[1]) / 100f; }
                            Reply(ACK);
                            break;
                        case "EQ":
                            if (_surface != null) { _surface.ApplyEqualizerPreset(int.Parse(parts[1])); }
                            Reply(ACK);
                            break;
                        case "STOP":
                            if (_surface != null) { _surface.Stop(); }
                            Reply(ACK);
                            break;
                        case "QUIT":
                            Reply(ACK);
                            _quit = true;
                            break;
                        default:
                            Reply(ERR + "|unknown command");
                            break;
                    }
                }
                catch (Exception ex)
                {
                    Trace2.Write("engine: handler error -> " + ex.Message);
                    Reply(ERR + "|" + ex.Message);
                }
            }

            private void Play(string file, string volume, bool muted, string fit, string speed, string eq)
            {
                if (_surface == null) { throw new InvalidOperationException("surface missing"); }
                if (!File.Exists(file)) { throw new FileNotFoundException("video not found", file); }
                _surface.Load(file, true, muted || volume == "0");
                _surface.VolumeRequested = int.Parse(volume);
                _surface.ApplyAudio(int.Parse(volume), int.Parse(eq));
                _surface.Muted = muted || volume == "0";
                ApplyFit(fit);
                _surface.Rate = int.Parse(speed) / 100f;
                _surface.Play();
            }

            private void ApplyFit(string mode)
            {
                _surface.AspectRatio = mode == "stretch" ? "16:9" : null;
            }

            private void Reply(string text)
            {
                if (_writer == null) { return; }
                try { _writer.WriteLine(text); }
                catch (Exception ex) { Trace2.Write("engine: reply failed -> " + ex.Message); }
            }

            private void Pump()
            {
                if (_quit)
                {
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
    }

    /// <summary>UI-side handle to the helper process.</summary>
    internal sealed class EngineClient : IDisposable
    {
        private Process _process;
        private NamedPipeClientStream _pipe;
        private StreamReader _reader;
        private StreamWriter _writer;
        private string _pipeName;

        public bool IsRunning
        {
            get { return _process != null && !_process.HasExited; }
        }

        public string LastError { get; private set; }

        public static string DefaultEqualizerNames()
        {
            // Preset list of the bundled engine, duplicated here so the UI process never
            // loads the native library just to fill a combo box.
            return "Без эквалайзера|Classical|Club|Dance|Full bass|Full bass and treble|Full treble|"
                 + "Headphones|Large Hall|Live|Party|Pop|Reggae|Rock|Ska|Soft|Soft rock|Techno";
        }

        public bool Start()
        {
            Stop();
            try
            {
                _pipeName = "lumen-engine-" + Process.GetCurrentProcess().Id + "-" + Guid.NewGuid().ToString("N").Substring(0, 8);
                ProcessStartInfo psi = new ProcessStartInfo(Application.ExecutablePath, EngineHost.Flag + " " + _pipeName)
                {
                    UseShellExecute = false,
                    CreateNoWindow = true
                };
                _process = Process.Start(psi);
                if (_process == null) { LastError = "не удалось запустить процесс движка"; return false; }

                _pipe = new NamedPipeClientStream(".", _pipeName, PipeDirection.InOut);
                _pipe.Connect(20000);
                _reader = new StreamReader(_pipe, Encoding.UTF8);
                _writer = new StreamWriter(_pipe, new UTF8Encoding(false)) { AutoFlush = true };

                string hello = _reader.ReadLine();
                Trace2.Write("ui: engine hello = " + hello);
                if (hello == null || !hello.StartsWith("OK"))
                {
                    LastError = "движок не ответил";
                    return false;
                }
                return true;
            }
            catch (Exception ex)
            {
                Trace2.Write("ui: engine start failed -> " + ex.Message);
                LastError = ex.Message;
                return false;
            }
        }

        /// <summary>Sends one command; returns the reply or null on failure.</summary>
        public string Send(string command)
        {
            if (!IsRunning || _writer == null) { LastError = "движок не запущен"; return null; }
            try
            {
                _writer.WriteLine(command);
                string reply = _reader.ReadLine();
                if (reply != null && reply.StartsWith("ERR"))
                {
                    LastError = reply;
                    Trace2.Write("ui: command error " + command + " -> " + reply);
                }
                return reply;
            }
            catch (Exception ex)
            {
                LastError = ex.Message;
                Trace2.Write("ui: send failed (" + command + ") -> " + ex.Message);
                return null;
            }
        }

        public void Stop()
        {
            try
            {
                if (IsRunning) { Send("QUIT"); }
            }
            catch { }
            try { if (_pipe != null) { _pipe.Dispose(); } } catch { }
            _pipe = null;
            _reader = null;
            _writer = null;
            try
            {
                if (_process != null && !_process.HasExited)
                {
                    if (!_process.WaitForExit(3000)) { _process.Kill(); }
                }
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
