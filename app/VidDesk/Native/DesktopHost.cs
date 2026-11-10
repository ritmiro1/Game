using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace VidDesk.Native
{
    /// <summary>Win32 helpers for embedding a render surface behind desktop icons.</summary>
    internal static class DesktopHost
    {
        private const uint GW_HWNDNEXT = 2;

        [DllImport("user32.dll")]
        private static extern IntPtr FindWindow(string cls, string win);

        [DllImport("user32.dll")]
        private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string win);

        [DllImport("user32.dll")]
        internal static extern IntPtr SetParent(IntPtr child, IntPtr parent);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern int GetWindowLong(IntPtr hWnd, int index);

        [DllImport("user32.dll", SetLastError = true)]
        internal static extern int SetWindowLong(IntPtr hWnd, int index, int newStyle);

        [DllImport("user32.dll")]
        internal static extern bool MoveWindow(IntPtr hWnd, int x, int y, int w, int h, bool repaint);

        [DllImport("user32.dll")]
        private static extern IntPtr GetWindow(IntPtr hWnd, uint cmd);

        [DllImport("user32.dll")]
        private static extern bool IsWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        private static extern IntPtr GetDesktopWindow();

        [DllImport("user32.dll")]
        private static extern IntPtr GetShellWindow();

        [DllImport("user32.dll")]
        private static extern IntPtr GetForegroundWindow();

        internal const int GWL_STYLE = -16;
        internal const int GWL_EXSTYLE = -20;
        internal const int WS_CHILD = 0x40000000;
        internal const int WS_POPUP = unchecked((int)0x80000000);
        internal const int WS_VISIBLE = 0x10000000;
        internal const int WS_CLIPCHILDREN = 0x02000000;
        internal const int WS_EX_NOACTIVATE = 0x08000000;
        internal const int WS_EX_TOOLWINDOW = 0x00000080;
        internal const int WS_EX_LAYERED = 0x00080000;

        /// <summary>
        /// Root that hosts wallpaper windows: "WorkerW" sitting immediately below the
        /// desktop icon view, or Program Manager as a fallback.
        /// </summary>
        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        /// <summary>
        /// Asks Explorer to spawn the wallpaper host window. Without this request some
        /// Windows builds keep icons and wallpaper in one window.
        /// </summary>
        internal static void RequestWallpaperHost()
        {
            IntPtr progman = FindWindow("Progman", null);
            if (progman == IntPtr.Zero) { return; }
            SendMessage(progman, 0x052C, new IntPtr(0xD), IntPtr.Zero);
            SendMessage(progman, 0x052C, new IntPtr(0xD), new IntPtr(1));
            System.Threading.Thread.Sleep(120);
        }

        internal static IntPtr GetWallpaperParent()
        {
            IntPtr progman = FindWindow("Progman", null);
            IntPtr worker = IntPtr.Zero;
            if (progman != IntPtr.Zero)
            {
                IntPtr candidate = FindWindowEx(progman, IntPtr.Zero, "WorkerW", null);
                while (candidate != IntPtr.Zero && worker == IntPtr.Zero)
                {
                    if (FindWindowEx(candidate, IntPtr.Zero, "SHELLDLL_DefView", null) != IntPtr.Zero)
                    {
                        IntPtr next = GetWindow(candidate, GW_HWNDNEXT);
                        if (next != IntPtr.Zero && IsWorker(next))
                        {
                            worker = next;
                        }
                    }
                    candidate = FindWindowEx(progman, candidate, "WorkerW", null);
                }
            }

            if (worker == IntPtr.Zero)
            {
                worker = FindSiblingWorker();
            }
            if (worker == IntPtr.Zero && progman != IntPtr.Zero)
            {
                worker = progman;
            }
            if (worker == IntPtr.Zero)
            {
                worker = GetDesktopWindow();
            }
            return worker;
        }

        private static IntPtr FindSiblingWorker()
        {
            IntPtr shell = GetShellWindow();
            IntPtr current = shell == IntPtr.Zero ? GetDesktopWindow() : shell;
            IntPtr found = IntPtr.Zero;
            IntPtr child = GetWindow(current, GW_HWNDNEXT);
            while (child != IntPtr.Zero)
            {
                if (IsWorker(child) && FindWindowEx(child, IntPtr.Zero, "SHELLDLL_DefView", null) == IntPtr.Zero)
                {
                    found = child;
                    break;
                }
                child = GetWindow(child, GW_HWNDNEXT);
            }
            return found;
        }

        private static bool IsWorker(IntPtr hwnd)
        {
            StringBuilder sb = new StringBuilder(64);
            GetClassName(hwnd, sb, sb.Capacity);
            return string.Equals(sb.ToString(), "WorkerW", StringComparison.Ordinal);
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassName(IntPtr hWnd, StringBuilder name, int count);

        /// <summary>Turns an owned window into a child of the desktop host.</summary>
        internal static bool AttachAsChild(IntPtr hwnd, IntPtr parent, int x, int y, int w, int h)
        {
            int style = GetWindowLong(hwnd, GWL_STYLE);
            style &= ~WS_POPUP;
            style |= WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN;
            SetWindowLong(hwnd, GWL_STYLE, style);

            int ex = GetWindowLong(hwnd, GWL_EXSTYLE);
            ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
            SetWindowLong(hwnd, GWL_EXSTYLE, ex);

            IntPtr previous = SetParent(hwnd, parent);
            MoveWindow(hwnd, x, y, w, h, true);
            // SetParent returns null on failure, or the old parent on success.
            return previous != IntPtr.Zero || GetWindowLong(hwnd, GWL_STYLE) != 0;
        }

        internal static void DetachToDesktop(IntPtr hwnd)
        {
            SetParent(hwnd, IntPtr.Zero);
            int style = GetWindowLong(hwnd, GWL_STYLE);
            style &= ~WS_CHILD;
            SetWindowLong(hwnd, GWL_STYLE, style);
        }

        internal static void Reposition(IntPtr hwnd, int x, int y, int w, int h)
        {
            MoveWindow(hwnd, x, y, w, h, true);
        }

        internal static bool WindowAlive(IntPtr hwnd)
        {
            return hwnd != IntPtr.Zero && IsWindow(hwnd);
        }

        [DllImport("user32.dll")]
        internal static extern bool SystemParametersInfo(int action, int param, ref RECT rect, int winIni);

        [StructLayout(LayoutKind.Sequential)]
        internal struct RECT
        {
            public int Left, Top, Right, Bottom;
        }

        internal const int SPI_GETWORKAREA = 0x0030;
        internal const int SPI_SETDESKWALLPAPER = 0x0014;

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        internal static extern bool SystemParametersInfo(int action, int param, string param2, int winIni);

        internal static System.Drawing.Rectangle VirtualDesktop()
        {
            return new System.Drawing.Rectangle(
                GetSystemMetrics(76), GetSystemMetrics(77),
                Math.Max(1, GetSystemMetrics(78)), Math.Max(1, GetSystemMetrics(79)));
        }

        [DllImport("user32.dll")]
        private static extern int GetSystemMetrics(int index);
    }
}
