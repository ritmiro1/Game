using System;
using System.Collections.Generic;
using System.Text;

namespace Lumen.Native
{
    /// <summary>
    /// Wallpaper placement for Windows 10/11.
    ///
    /// The reliable technique is the one Explorer itself uses: the window is reparented to
    /// Progman and pushed to the very bottom of Progman's child band with SetWindowPos.
    /// Progman already contains the desktop icon view, and z-order keeps our window behind
    /// the icons. A working WorkerW host is used when it exists (some multi-monitor and
    /// custom-theme setups), Progman otherwise.
    /// </summary>
    internal static class WallpaperAttach
    {
        // ---------------------------------------------------------------- win32

        private delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern IntPtr FindWindow(string cls, string win);

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string win);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool EnumChildWindows(IntPtr parent, EnumWindowsProc callback, IntPtr lParam);

        [System.Runtime.InteropServices.DllImport("user32.dll", SetLastError = true)]
        private static extern IntPtr SetParent(IntPtr child, IntPtr parent);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool MoveWindow(IntPtr hwnd, int x, int y, int w, int h, bool repaint);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool IsWindow(IntPtr hwnd);

        [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
        private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int count);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern IntPtr SendMessageTimeout(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam,
            uint flags, uint timeout, out IntPtr result);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool GetClientRect(IntPtr hwnd, out RECT rect);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern int GetSystemMetrics(int index);

        [System.Runtime.InteropServices.DllImport("user32.dll")]
        private static extern bool SystemParametersInfo(int action, int param, ref RECT rect, int winIni);

        [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
        internal struct RECT
        {
            public int Left, Top, Right, Bottom;
            public int Width { get { return Right - Left; } }
            public int Height { get { return Bottom - Top; } }
        }

        private const uint WM_SPAWN_WORKER = 0x052C;      // undocumented Explorer message
        private const uint SMTO_NORMAL = 0x0000;
        private const uint SWP_NOACTIVATE = 0x0010;
        private const uint SWP_SHOWWINDOW = 0x0040;
        private const uint SWP_NOZORDER = 0x0004;
        private const int SPI_GETWORKAREA = 0x0030;
        private const int SM_XVIRTUALSCREEN = 76;
        private const int SM_YVIRTUALSCREEN = 77;
        private const int SM_CXVIRTUALSCREEN = 78;
        private const int SM_CYVIRTUALSCREEN = 79;

        // ------------------------------------------------------------- discovery

        private static string ClassOf(IntPtr hwnd)
        {
            StringBuilder sb = new StringBuilder(128);
            GetClassName(hwnd, sb, sb.Capacity);
            return sb.ToString();
        }

        /// <summary>Asks Explorer to create a wallpaper host window if it has not yet.</summary>
        internal static void SpawnWorker()
        {
            IntPtr progman = FindWindow("Progman", null);
            if (progman == IntPtr.Zero) { return; }
            IntPtr result;
            // 0x0D / 0x01 is what Explorer reacts to by spawning its WorkerW.
            SendMessageTimeout(progman, WM_SPAWN_WORKER, new IntPtr(0x0D), IntPtr.Zero, SMTO_NORMAL, 1000, out result);
            SendMessageTimeout(progman, WM_SPAWN_WORKER, new IntPtr(0x0D), new IntPtr(1), SMTO_NORMAL, 1000, out result);
        }

        /// <summary>
        /// Finds a top-level WorkerW that has no SHELLDLL_DefView child: that is the empty
        /// host Explorer keeps for wallpaper purposes.
        /// </summary>
        private static IntPtr FindWorkerW()
        {
            IntPtr found = IntPtr.Zero;
            EnumWindows(delegate (IntPtr hwnd, IntPtr param)
            {
                if (ClassOf(hwnd) != "WorkerW") { return true; }
                if (FindWindowEx(hwnd, IntPtr.Zero, "SHELLDLL_DefView", null) != IntPtr.Zero)
                {
                    // This is the icon host; the wallpaper host is its next sibling.
                    return true;
                }
                found = hwnd;
                return false;
            }, IntPtr.Zero);
            return found;
        }

        /// <summary>WorkerW that is the direct sibling of the desktop icon view.</summary>
        private static IntPtr FindSiblingWorkerW()
        {
            IntPtr result = IntPtr.Zero;
            EnumWindows(delegate (IntPtr hwnd, IntPtr param)
            {
                if (ClassOf(hwnd) != "WorkerW") { return true; }
                if (FindWindowEx(hwnd, IntPtr.Zero, "SHELLDLL_DefView", null) == IntPtr.Zero) { return true; }
                return true; // handled below through sibling scan
            }, IntPtr.Zero);

            // Scan the child band of each top-level window for a DefView and read its next
            // sibling, which on Windows 8+ is the wallpaper WorkerW.
            IntPtr progman = FindWindow("Progman", null);
            IntPtr[] roots = { progman, IntPtr.Zero };
            foreach (IntPtr root in roots)
            {
                IntPtr child = IntPtr.Zero;
                while (true)
                {
                    child = FindWindowEx(root, child, null, null);
                    if (child == IntPtr.Zero) { break; }
                    if (FindWindowEx(child, IntPtr.Zero, "SHELLDLL_DefView", null) != IntPtr.Zero)
                    {
                        IntPtr sibling = FindWindowEx(root, child, "WorkerW", null);
                        if (sibling != IntPtr.Zero) { result = sibling; }
                    }
                }
                if (result != IntPtr.Zero) { break; }
            }
            return result;
        }

        internal static IntPtr Progman { get { return FindWindow("Progman", null); } }

        /// <summary>Best available host for the wallpaper window.</summary>
        internal static IntPtr ResolveHost(out string where)
        {
            IntPtr progman = Progman;
            SpawnWorker();

            IntPtr worker = FindWorkerW();
            if (worker == IntPtr.Zero) { worker = FindSiblingWorkerW(); }
            if (worker != IntPtr.Zero)
            {
                where = "WorkerW";
                return worker;
            }
            if (progman != IntPtr.Zero)
            {
                where = "Progman";
                return progman;
            }
            where = "Desktop";
            return IntPtr.Zero;
        }

        // --------------------------------------------------------------- placement

        /// <summary>
        /// Puts the window on the desktop underneath the icons. Returns the host kind used.
        /// </summary>
        internal static string Place(IntPtr hwnd, bool topMostFallback)
        {
            if (hwnd == IntPtr.Zero) { return "none"; }

            string where;
            IntPtr host = ResolveHost(out where);
            RECT screen = VirtualScreen();

            if (host == IntPtr.Zero)
            {
                // Last resort: float exactly over the desktop without stealing focus.
                if (topMostFallback)
                {
                    SetWindowPos(hwnd, new IntPtr(-1) /* HWND_TOPMOST */, screen.Left, screen.Top,
                        screen.Width, screen.Height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                }
                else
                {
                    SetWindowPos(hwnd, new IntPtr(1) /* HWND_BOTTOM */, screen.Left, screen.Top,
                        screen.Width, screen.Height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
                }
                return "floating";
            }

            // Convert to a child window of the host, then push it to the bottom of the band
            // so the icon view stays above it.
            int style = GetWindowLongPtrCompat(hwnd, -16);
            style &= ~unchecked((int)0x80000000);      // remove WS_POPUP
            style |= 0x40000000 | 0x10000000;          // WS_CHILD | WS_VISIBLE
            SetWindowLongPtrCompat(hwnd, -16, style);
            SetParent(hwnd, host);
            SetWindowPos(hwnd, new IntPtr(1) /* HWND_BOTTOM */, screen.Left, screen.Top,
                screen.Width, screen.Height, SWP_SHOWWINDOW | SWP_NOACTIVATE);
            MoveWindow(hwnd, screen.Left, screen.Top, screen.Width, screen.Height, true);
            return where;
        }

        private static int GetWindowLongPtrCompat(IntPtr hwnd, int index)
        {
            return IntPtr.Size == 8
                ? (int)GetWindowLongPtr64(hwnd, index)
                : GetWindowLong32(hwnd, index);
        }

        private static void SetWindowLongPtrCompat(IntPtr hwnd, int index, int value)
        {
            if (IntPtr.Size == 8) { SetWindowLongPtr64(hwnd, index, new IntPtr(value)); }
            else { SetWindowLong32(hwnd, index, value); }
        }

        [System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
        private static extern IntPtr GetWindowLongPtr64(IntPtr hwnd, int index);

        [System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "GetWindowLongW")]
        private static extern int GetWindowLong32(IntPtr hwnd, int index);

        [System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
        private static extern IntPtr SetWindowLongPtr64(IntPtr hwnd, int index, IntPtr value);

        [System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "SetWindowLongW")]
        private static extern int SetWindowLong32(IntPtr hwnd, int index, int value);

        /// <summary>Full virtual desktop rectangle, so every monitor is covered.</summary>
        internal static RECT VirtualScreen()
        {
            RECT r;
            r.Left = GetSystemMetrics(SM_XVIRTUALSCREEN);
            r.Top = GetSystemMetrics(SM_YVIRTUALSCREEN);
            r.Right = r.Left + Math.Max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
            r.Bottom = r.Top + Math.Max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
            return r;
        }

        internal static RECT PrimaryWorkArea()
        {
            RECT r = new RECT();
            SystemParametersInfo(SPI_GETWORKAREA, 0, ref r, 0);
            if (r.Right <= r.Left || r.Bottom <= r.Top) { return VirtualScreen(); }
            return r;
        }

        /// <summary>Unparents the surface and restores a normal top-level window.</summary>
        internal static void Release(IntPtr hwnd)
        {
            if (hwnd == IntPtr.Zero) { return; }
            SetParent(hwnd, IntPtr.Zero);
            int style = GetWindowLongPtrCompat(hwnd, -16);
            style &= ~0x40000000;              // remove WS_CHILD
            style |= unchecked((int)0x80000000);  // restore WS_POPUP
            SetWindowLongPtrCompat(hwnd, -16, style);
        }

        internal static bool WindowAlive(IntPtr hwnd)
        {
            return hwnd != IntPtr.Zero && IsWindow(hwnd);
        }

        /// <summary>True when the window has a non-empty client area on screen.</summary>
        internal static bool HasSize(IntPtr hwnd, out int width, out int height)
        {
            width = height = 0;
            RECT r;
            if (!GetClientRect(hwnd, out r)) { return false; }
            width = r.Width;
            height = r.Height;
            return width > 32 && height > 32;
        }
    }
}
