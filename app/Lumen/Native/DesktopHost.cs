using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace Lumen.Native
{
    /// <summary>Win32 helpers for embedding a render surface behind desktop icons.</summary>
    internal static class DesktopHost
    {
        private const uint GW_HWNDNEXT = 2;

        [DllImport("user32.dll")]
        private static extern IntPtr FindWindow(string cls, string win);

        [DllImport("user32.dll")]
        private static extern IntPtr FindWindowEx(IntPtr parent, IntPtr after, string cls, string win);

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

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

        internal static void DetachToDesktop(IntPtr hwnd)
        {
            WallpaperAttach.Release(hwnd);
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
    }
}
