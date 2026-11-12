using System.Reflection;
using Avalonia.Controls;
using Avalonia.Media;

namespace HowCouldItBeYou.Launcher;

/// <summary>
/// The launcher window: borderless, fixed design size (Avalonia scales it with the monitor's DPI).
/// Everything inside is painted by <see cref="LauncherView"/>.
/// </summary>
public sealed class MainWindow : Window
{
    private bool _closeConfirmed;

    public LauncherView View { get; }

    /// <param name="preview">Only for the screenshot self-test: "update", "install" or "busy" shows that state.</param>
    /// <param name="notesFile">Only for the screenshot self-test: shows this Markdown file instead of the latest release notes.</param>
    public MainWindow(string? preview = null, string? notesFile = null)
    {
        Title = "How Could It Be You";
        Width = LauncherView.DesignWidth;
        Height = LauncherView.DesignHeight;
        CanResize = false;
        WindowStartupLocation = WindowStartupLocation.CenterScreen;
        Background = new SolidColorBrush(LauncherView.Ink);
        if (OperatingSystem.IsWindows())
        {
            // No title bar, but Windows keeps the shadow and (Windows 11) the rounded corners
            ExtendClientAreaToDecorationsHint = true;
            ExtendClientAreaChromeHints = Avalonia.Platform.ExtendClientAreaChromeHints.NoChrome;
            ExtendClientAreaTitleBarHeightHint = 0;
        }
        else
        {
            SystemDecorations = SystemDecorations.None;
        }
        Icon = LoadIcon();

        View = new LauncherView(this, preview, notesFile);
        Content = View;
        Opened += async (_, _) => await View.CheckForUpdatesAsync();
        Closing += OnClosing;
    }

    /// <summary>Closes even while an update runs (after the player confirmed it).</summary>
    public void CloseConfirmed()
    {
        _closeConfirmed = true;
        Close();
    }

    private void OnClosing(object? sender, WindowClosingEventArgs e)
    {
        if (!_closeConfirmed && View.IsInstalling)
        {
            e.Cancel = true;
            View.AskToClose();
            return;
        }
        View.Shutdown();
    }

    private static WindowIcon? LoadIcon()
    {
        // The .ico has every size Windows wants for the taskbar; Linux desktops take the PNG
        var suffix = OperatingSystem.IsWindows() ? ".app.ico" : ".app.png";
        var assembly = Assembly.GetExecutingAssembly();
        var name = assembly.GetManifestResourceNames().FirstOrDefault(n => n.EndsWith(suffix, StringComparison.Ordinal));
        if (name is null)
        {
            return null;
        }
        try
        {
            using var stream = assembly.GetManifestResourceStream(name);
            return stream is null ? null : new WindowIcon(stream);
        }
        catch (Exception)
        {
            return null; // cosmetic only
        }
    }
}
