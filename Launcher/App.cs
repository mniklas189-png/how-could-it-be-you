using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Media.Imaging;
using Avalonia.Themes.Simple;
using Avalonia.Threading;

namespace HowCouldItBeYou.Launcher;

/// <summary>The Avalonia application: one window, painted by <see cref="LauncherView"/>.</summary>
public sealed class App : Application
{
    public override void Initialize()
    {
        Name = "How Could It Be You";
        Styles.Add(new SimpleTheme());
    }

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            var shot = Program.Screenshot;
            var window = new MainWindow(shot?.Preview, shot?.NotesFile);
            desktop.MainWindow = window;
            if (shot is { } s)
            {
                // Give the update check a few seconds, then save what the window shows and quit
                DispatcherTimer.RunOnce(() =>
                {
                    var code = 0;
                    try
                    {
                        var view = window.View;
                        using var bitmap = new RenderTargetBitmap(new PixelSize(LauncherView.DesignWidth, LauncherView.DesignHeight));
                        bitmap.Render(view);
                        bitmap.Save(s.Png);
                    }
                    catch (Exception ex)
                    {
                        Console.Error.WriteLine(ex);
                        code = 1;
                    }
                    desktop.Shutdown(code);
                }, TimeSpan.FromSeconds(6));
            }
        }
        base.OnFrameworkInitializationCompleted();
    }
}
