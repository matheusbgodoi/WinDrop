using Microsoft.Win32;

namespace Winawdl.Shell;

/// <summary>Starting with Windows, in the notification area and with the radio on: the PC
/// is then there for nearby devices without anyone opening anything. It is a choice, and
/// off until made: while the radio is on, the Wi-Fi card is not on Wi-Fi.</summary>
public static class Autostart
{
    /// <summary>The argument of a copy Windows started: no window, radio on.</summary>
    public const string Flag = "--background";
    private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string Name = "WinDrop";

    public static bool Enabled
    {
        get
        {
            using var run = Registry.CurrentUser.OpenSubKey(RunKey);
            return run?.GetValue(Name) is string;
        }
        set
        {
            using var run = Registry.CurrentUser.CreateSubKey(RunKey);
            if (value && Environment.ProcessPath is { } program) run.SetValue(Name, $"\"{program}\" {Flag}");
            else if (!value) run.DeleteValue(Name, throwOnMissingValue: false);
        }
    }
}
