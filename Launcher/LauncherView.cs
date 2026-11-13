using System.Diagnostics;
using System.Reflection;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Media.Immutable;
using Avalonia.Media.TextFormatting;
using Avalonia.Rendering;
using Avalonia.Threading;

namespace HowCouldItBeYou.Launcher;

/// <summary>
/// Everything inside the launcher window, painted by hand: game artwork as a slowly changing backdrop,
/// title on the left, release notes on a glass card on the right, play/update at the bottom.
/// Laid out in a fixed design size of 1060 x 640 (device-independent pixels).
/// Same on Windows and Linux. Fonts: the title in Roboto Bold Condensed (shipped with the launcher, like in the game's HUD),
/// all other text in Segoe UI where it is installed (Windows), otherwise in Roboto.
/// </summary>
public sealed class LauncherView : Control, ICustomHitTest
{
    public const int DesignWidth = 1060;
    public const int DesignHeight = 640;

    public static readonly Color Ink = Color.FromRgb(8, 9, 12);
    private static readonly Color Bone = Color.FromRgb(236, 228, 212);
    private static readonly Color TextColor = Color.FromRgb(232, 234, 238);
    private static readonly Color Muted = Color.FromRgb(150, 156, 168);
    private static readonly Color Blood = Color.FromRgb(193, 18, 31);
    private static readonly Color BloodBright = Color.FromRgb(226, 38, 50);

    private const double SlideSeconds = 9.0;
    private const double FadeSeconds = 1.6;

    private static readonly Cursor HandCursor = new(StandardCursorType.Hand);

    private enum ButtonStyle { Primary, Light, Glass }

    private sealed record ButtonSpec(string Text, string? Sub, ButtonStyle Style, bool Enabled, Action OnClick);
    private sealed record Zone(string Id, Rect Rect, bool Enabled, Action OnClick);
    private sealed record NoteLine(string Text, int Kind); // 0 = paragraph, 1 = heading, 2 = bullet

    private readonly MainWindow _window;
    private readonly LauncherConfig _config;
    private readonly GitHubUpdater _updater;
    private readonly string? _preview;
    private readonly string? _notesFile;
    private readonly List<Bitmap> _slides = [];
    private readonly List<Zone> _zones = [];
    private readonly DispatcherTimer _timer;
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly FontFamily _uiFamily;
    private readonly FontFamily _titleFamily;
    private readonly string _launcherVersion;

    private UpdateCheckResult? _lastCheck;
    private CancellationTokenSource? _cts;
    // Size of a small update to the latest release (null while unknown or when it has to be the whole zip)
    private CancellationTokenSource? _estimateCts;
    private (string Tag, long? Bytes)? _estimate;
    // The launcher's own update: looked for after each check, put in place in the background, and the
    // launcher restarts into it only when nothing else is going on (no game update, no game started from here)
    private bool _selfUpdateStarted;
    private Version? _launcherReady;
    private bool _gameStarted;
    private bool _updatedNoteShown;
    private bool _busy;
    private bool _installing;
    private bool _askClose;
    private Phrase _status = new("Suche nach Updates ...", "Checking for updates ...");
    private double? _fraction;
    // Where the news card's text comes from; asked again when the language changes
    private Func<(string Title, string Date, string Body)> _notesSource = () => ("", "", Lang.T("Suche nach Updates ...", "Checking for updates ..."));
    private string _notesTitle = "";
    private string _notesDate = "";
    private string _notesBody = "";
    private List<NoteLine> _notes = [];
    private double _notesScroll;
    private double _notesOverflow;
    private Rect _notesRect;
    private string? _hover;
    private string? _pressed;
    private int _slide;
    private double _slideSince;

    public LauncherView(MainWindow window, string? preview, string? notesFile)
    {
        _window = window;
        _preview = preview;
        _notesFile = notesFile;
        _config = LauncherConfig.Load();
        _updater = new GitHubUpdater(_config);
        _uiFamily = PickFamily("Segoe UI");
        // Not Bahnschrift: Avalonia draws that variable font only in its thin default weight
        _titleFamily = Roboto;
        _launcherVersion = (Assembly.GetExecutingAssembly().GetCustomAttribute<AssemblyInformationalVersionAttribute>()?.InformationalVersion ?? "?").Split('+')[0];

        Width = DesignWidth;
        Height = DesignHeight;
        ClipToBounds = true;
        RenderOptions.SetBitmapInterpolationMode(this, BitmapInterpolationMode.HighQuality);

        LoadSlides();
        RebuildNotes();

        _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(33), DispatcherPriority.Background, (_, _) => OnTick());
        _timer.Start();
    }

    public bool IsInstalling => _installing && _cts is not null;

    /// <summary>The whole area takes the mouse, also where nothing is painted.</summary>
    public bool HitTest(Point point) => new Rect(Bounds.Size).Contains(point);

    // ------------------------------------------------------------------------------------------------
    // Setup

    /// <summary>Roboto Regular, Italic, Bold and Bold Condensed, shipped inside the launcher (Assets/Fonts).</summary>
    private static readonly FontFamily Roboto = new("avares://HowCouldItBeYouLauncher/Assets/Fonts#Roboto");

    /// <summary>First installed font of the list; Roboto (shipped with the launcher) if none is.</summary>
    private static FontFamily PickFamily(params string[] names)
    {
        // HC_LAUNCHER_SHIPPED_FONTS=1: look as on Linux (to check that look with --selftest-screenshot on Windows)
        if (Environment.GetEnvironmentVariable("HC_LAUNCHER_SHIPPED_FONTS") == "1")
        {
            return Roboto;
        }
        try
        {
            var installed = FontManager.Current.SystemFonts.Select(f => f.Name).ToHashSet(StringComparer.OrdinalIgnoreCase);
            var name = names.FirstOrDefault(installed.Contains);
            if (name is not null)
            {
                return new FontFamily(name);
            }
        }
        catch (Exception)
        {
            // no font list: take the shipped font
        }
        return Roboto;
    }

    private void LoadSlides()
    {
        var assembly = Assembly.GetExecutingAssembly();
        foreach (var name in assembly.GetManifestResourceNames().Where(n => n.Contains(".Assets.hero_", StringComparison.Ordinal)).OrderBy(n => n, StringComparer.Ordinal))
        {
            try
            {
                using var stream = assembly.GetManifestResourceStream(name);
                if (stream is not null)
                {
                    _slides.Add(new Bitmap(stream));
                }
            }
            catch (Exception)
            {
                // a broken picture must never stop the launcher
            }
        }
    }

    private void OnTick()
    {
        var now = _clock.Elapsed.TotalSeconds;
        var sinceSlide = now - _slideSince;
        var fading = _slides.Count > 1 && sinceSlide > SlideSeconds;
        if (fading && sinceSlide > SlideSeconds + FadeSeconds)
        {
            _slide = (_slide + 1) % _slides.Count;
            _slideSince = now;
        }
        if (fading || _busy)
        {
            InvalidateVisual();
        }
    }

    public void Shutdown()
    {
        _timer.Stop();
        _cts?.Cancel();
        _estimateCts?.Cancel();
        _updater.Dispose();
    }

    // ------------------------------------------------------------------------------------------------
    // Painting

    public override void Render(DrawingContext g)
    {
        _zones.Clear();
        DrawBackdrop(g);
        DrawShade(g);
        DrawChrome(g);
        DrawTitle(g);
        DrawNotes(g);
        DrawActions(g);
        DrawProgress(g);
        if (_askClose)
        {
            DrawCloseQuestion(g);
        }
    }

    private void DrawBackdrop(DrawingContext g)
    {
        g.FillRectangle(Brush(Ink), new Rect(0, 0, DesignWidth, DesignHeight));
        if (_slides.Count == 0)
        {
            return;
        }
        DrawCover(g, _slides[_slide % _slides.Count]);
        var sinceSlide = _clock.Elapsed.TotalSeconds - _slideSince;
        if (_slides.Count > 1 && sinceSlide > SlideSeconds)
        {
            var alpha = Math.Clamp((sinceSlide - SlideSeconds) / FadeSeconds, 0.0, 1.0);
            using (g.PushOpacity(alpha))
            {
                DrawCover(g, _slides[(_slide + 1) % _slides.Count]);
            }
        }
    }

    /// <summary>The picture scaled to cover the whole window, centred.</summary>
    private static void DrawCover(DrawingContext g, Bitmap picture)
    {
        var size = picture.PixelSize;
        var factor = Math.Max((double)DesignWidth / size.Width, (double)DesignHeight / size.Height);
        var w = size.Width * factor;
        var h = size.Height * factor;
        g.DrawImage(picture, new Rect((DesignWidth - w) / 2, (DesignHeight - h) / 2, w, h));
    }

    /// <summary>Dark gradients over the artwork, so text stays readable on any picture.</summary>
    private static void DrawShade(DrawingContext g)
    {
        g.FillRectangle(Gradient(Alpha(240, Ink), Alpha(0, Ink), horizontal: true), new Rect(0, 0, 760, DesignHeight));
        g.FillRectangle(Gradient(Alpha(0, Ink), Alpha(245, Ink), horizontal: false), new Rect(0, DesignHeight - 300, DesignWidth, 300));
        g.FillRectangle(Gradient(Alpha(170, Ink), Alpha(0, Ink), horizontal: false), new Rect(0, 0, DesignWidth, 90));
        g.FillRectangle(Brush(Alpha(40, Ink)), new Rect(0, 0, DesignWidth, DesignHeight));
    }

    private void DrawChrome(DrawingContext g)
    {
        // Small red marker + wordmark
        g.FillRectangle(Brush(Blood), new Rect(40, 31, 3, 14));
        Text("KEPLER-7  //  LAUNCHER", 11, Alpha(170, Muted), FontWeight.Bold, spacing: 2.2).Draw(g, new Point(52, 30));

        DrawLanguageSwitch(g, new Rect(DesignWidth - 104 - 14 - 84, 17, 84, 28));
        DrawWindowButton(g, "min", new Rect(DesignWidth - 104, 14, 44, 34), false, () => _window.WindowState = WindowState.Minimized);
        DrawWindowButton(g, "close", new Rect(DesignWidth - 56, 14, 44, 34), true, _window.Close);
    }

    /// <summary>DE | EN pill next to the window buttons; the active language is filled red.</summary>
    private void DrawLanguageSwitch(DrawingContext g, Rect rect)
    {
        g.DrawRectangle(Brush(White(26)), Stroke(White(60)), rect, rect.Height / 2, rect.Height / 2);
        var half = rect.Width / 2;
        foreach (var (id, label, english) in new[] { ("lang-de", "DE", false), ("lang-en", "EN", true) })
        {
            var segment = new Rect(rect.X + (english ? half : 0), rect.Y, half, rect.Height);
            var active = Lang.English == english;
            var hover = !active && _hover == id;
            if (active || hover)
            {
                var pill = segment.Deflate(3);
                g.DrawRectangle(Brush(active ? Blood : White(40)), null, pill, pill.Height / 2, pill.Height / 2);
            }
            DrawCentered(g, Text(label, 11, active || hover ? Colors.White : Alpha(200, Muted), FontWeight.Bold), segment);
            // The active half isn't clickable, so it shows no hand cursor
            _zones.Add(new Zone(id, segment, !active, () => SetLanguage(english)));
        }
    }

    private void DrawWindowButton(DrawingContext g, string id, Rect rect, bool close, Action onClick)
    {
        var hover = _hover == id;
        if (hover)
        {
            g.DrawRectangle(Brush(close ? Blood : White(40)), null, rect, 8, 8);
        }
        var pen = Stroke(hover ? Colors.White : Alpha(200, Muted), 1.4);
        var c = rect.Center;
        if (close)
        {
            g.DrawLine(pen, new Point(c.X - 5, c.Y - 5), new Point(c.X + 5, c.Y + 5));
            g.DrawLine(pen, new Point(c.X - 5, c.Y + 5), new Point(c.X + 5, c.Y - 5));
        }
        else
        {
            g.DrawLine(pen, new Point(c.X - 6, c.Y + 4), new Point(c.X + 6, c.Y + 4));
        }
        _zones.Add(new Zone(id, rect, true, onClick));
    }

    private void DrawTitle(DrawingContext g)
    {
        Text(Lang.T("SOCIAL-DEDUCTION-HORROR   /   4 BIS 8 SPIELER", "SOCIAL DEDUCTION HORROR   /   4 TO 8 PLAYERS"), 11.5, BloodBright, FontWeight.Bold, spacing: 2.4)
            .Draw(g, new Point(42, 150));

        // 76 px like the design; a wider fallback font (Inter on Linux) is shrunk to keep clear of the news card
        var lines = new[] { ("HOW COULD", 170.0), ("IT BE YOU", 242.0) };
        var size = 76.0;
        var widest = lines.Max(l => Text(l.Item1, size, TextColor, FontWeight.Bold, title: true).WidthIncludingTrailingWhitespace);
        if (widest > 560)
        {
            size *= 560 / widest;
        }
        foreach (var (line, y) in lines)
        {
            Text(line, size, Color.FromArgb(150, 0, 0, 0), FontWeight.Bold, title: true).Draw(g, new Point(40, y + 3));
            Text(line, size, TextColor, FontWeight.Bold, title: true).Draw(g, new Point(38, y));
        }

        Text(Lang.T("Es ist einer von euch.", "It's one of you."), 21, Bone, italic: true).Draw(g, new Point(40, 334));

        Text(Lang.T("Eine Forscher-Crew auf der Raumstation Kepler-7. Einer von euch ist der Wandler: " +
                    "Er sieht aus wie du. Er klingt nicht wie du.",
                    "A research crew on the space station Kepler-7. One of you is the Shifter: " +
                    "It looks like you. It doesn't sound like you."),
             14, Alpha(215, Muted), maxWidth: 430, wrap: true, maxHeight: 70).Draw(g, new Point(42, 380));
    }

    private void DrawNotes(DrawingContext g)
    {
        var card = new Rect(DesignWidth - 40 - 372, 92, 372, 366);
        g.DrawRectangle(Brush(Color.FromArgb(168, 10, 11, 15)), Stroke(White(34)), card, 14, 14);

        Text(Lang.T("NEUIGKEITEN", "NEWS"), 10.5, Muted, FontWeight.Bold, spacing: 2.2).Draw(g, new Point(card.X + 22, card.Y + 20));
        if (_notesDate.Length > 0)
        {
            var date = Text(_notesDate, 10.5, Muted, FontWeight.Bold);
            date.Draw(g, new Point(card.Right - 22 - date.WidthIncludingTrailingWhitespace, card.Y + 20));
        }
        var top = card.Y + 48;
        if (_notesTitle.Length > 0)
        {
            var head = Text(_notesTitle, 17, TextColor, FontWeight.Bold, maxWidth: card.Width - 44, wrap: true);
            head.Draw(g, new Point(card.X + 22, top));
            top += head.Height + 8;
        }
        g.DrawLine(Stroke(White(30)), new Point(card.X + 22, top), new Point(card.Right - 22, top));
        top += 10;

        // Scrollable text
        var inner = new Rect(card.X + 22, top, card.Width - 44, card.Bottom - top - 16);
        _notesRect = inner;
        var y = inner.Y - _notesScroll;
        using (g.PushClip(inner))
        {
            foreach (var line in _notes)
            {
                switch (line.Kind)
                {
                    case 1:
                        y += 6;
                        Text(line.Text.ToUpperInvariant(), 11, BloodBright, FontWeight.Bold, spacing: 1.6).Draw(g, new Point(inner.X, y));
                        y += 22;
                        break;
                    case 2:
                    {
                        var text = Text(line.Text, 13.5, Alpha(225, TextColor), maxWidth: inner.Width - 16, wrap: true);
                        Text("•", 13.5, BloodBright).Draw(g, new Point(inner.X + 2, y));
                        text.Draw(g, new Point(inner.X + 16, y));
                        y += text.Height + 5;
                        break;
                    }
                    default:
                    {
                        var text = Text(line.Text, 13.5, Alpha(225, TextColor), maxWidth: inner.Width, wrap: true);
                        text.Draw(g, new Point(inner.X, y));
                        y += text.Height + 9;
                        break;
                    }
                }
            }
        }
        _notesOverflow = Math.Max(0, y - inner.Bottom);
        if (_notesOverflow + _notesScroll > 0)
        {
            // Slim scroll thumb on the right edge of the card
            var total = inner.Height + _notesScroll + _notesOverflow;
            var thumb = Math.Max(24, inner.Height * inner.Height / total);
            var offset = (inner.Height - thumb) * (_notesScroll / (_notesScroll + _notesOverflow));
            g.DrawRectangle(Brush(White(70)), null, new Rect(card.Right - 10, inner.Y + offset, 3, thumb), 1.5, 1.5);
        }
    }

    private (ButtonSpec Primary, ButtonSpec? Secondary) ComputeButtons()
    {
        var canPlay = !_busy && _updater.FindGameExecutable() is not null;
        if (_installing)
        {
            return (new ButtonSpec(Lang.T("ABBRECHEN", "CANCEL"), Lang.T("Die bisherige Version bleibt erhalten", "Your current version is kept"), ButtonStyle.Glass, true, () => _cts?.Cancel()), null);
        }
        var latest = _lastCheck?.Latest;
        switch (_lastCheck?.State)
        {
            case UpdateState.NotInstalled when latest is not null:
                return (new ButtonSpec(Lang.T("INSTALLIEREN", "INSTALL"), $"{latest.Tag}  ·  {GitHubUpdater.FormatSize(latest.AssetSize)}", ButtonStyle.Primary, !_busy, () => _ = InstallAsync(latest)),
                    new ButtonSpec(Lang.T("Erneut prüfen", "Check again"), null, ButtonStyle.Glass, !_busy, () => _ = CheckForUpdatesAsync()));
            case UpdateState.UpdateAvailable when latest is not null:
                return (new ButtonSpec("UPDATE", $"{_lastCheck!.InstalledVersion} → {latest.Tag}  ·  {UpdateSize(latest)}", ButtonStyle.Light, !_busy, () => _ = InstallAsync(latest)),
                    new ButtonSpec(Lang.T("SPIELEN", "PLAY"), Lang.T("ohne Update", "without updating"), ButtonStyle.Glass, canPlay, Play));
            default:
                return (new ButtonSpec(Lang.T("SPIELEN", "PLAY"), null, ButtonStyle.Primary, canPlay, Play),
                    new ButtonSpec(Lang.T("Nach Updates suchen", "Check for updates"), null, ButtonStyle.Glass, !_busy, () => _ = CheckForUpdatesAsync()));
        }
    }

    /// <summary>Download size for the UPDATE button: only the changes when known, "…" while they are worked out.</summary>
    private string UpdateSize(ReleaseInfo latest)
    {
        if (_estimate is { } estimate && estimate.Tag == latest.Tag)
        {
            return estimate.Bytes is { } bytes ? GitHubUpdater.FormatSize(bytes) : GitHubUpdater.FormatSize(latest.AssetSize);
        }
        // Called while painting: no file access here (UPDATE is only shown when the game is installed)
        return latest.ManifestUrl is not null ? "…" : GitHubUpdater.FormatSize(latest.AssetSize);
    }

    /// <summary>Works out in the background how much a small update downloads (reads the installed files once).</summary>
    private async Task EstimateAsync(ReleaseInfo latest)
    {
        _estimateCts?.Cancel();
        if (!_updater.CanPatch(latest))
        {
            return;
        }
        var cts = _estimateCts = new CancellationTokenSource(TimeSpan.FromMinutes(5));
        long? bytes = null;
        try
        {
            bytes = await _updater.EstimateAsync(latest, cts.Token);
        }
        catch (Exception)
        {
            // cancelled or offline: the button shows the zip size
        }
        if (!cts.IsCancellationRequested || bytes is not null)
        {
            _estimate = (latest.Tag, bytes);
            InvalidateVisual();
        }
    }

    private void DrawActions(DrawingContext g)
    {
        var (primary, secondary) = ComputeButtons();
        var top = DesignHeight - 124.0;
        DrawButton(g, "primary", new Rect(40, top, 250, 66), primary);
        var next = 304.0;
        if (secondary is not null)
        {
            DrawButton(g, "secondary", new Rect(next, top, 214, 66), secondary);
            next += 214 + 14;
        }

        // Status and versions to the right of the buttons
        Text(_status.ToString(), 14, TextColor, maxWidth: DesignWidth - 40 - next - 14, ellipsis: true).Draw(g, new Point(next + 16, top + 9));
        var installed = _updater.GetInstalledVersion() ?? "–";
        var newest = _lastCheck?.Latest?.Tag ?? "?";
        Text(Lang.T($"INSTALLIERT {installed}   ·   NEUESTE {newest}", $"INSTALLED {installed}   ·   LATEST {newest}").ToUpperInvariant(), 11.5, Muted, FontWeight.Bold, spacing: 1.3)
            .Draw(g, new Point(next + 16, top + 40));

        // Footer links
        var x = 44.0;
        x = DrawLink(g, "folder", Lang.T("Spielordner öffnen", "Open game folder"), x, DesignHeight - 41, OpenGameFolder) + 22;
        DrawLink(g, "github", Lang.T("Projekt auf GitHub", "Project on GitHub"), x, DesignHeight - 41, () => OpenUrl($"https://github.com/{_config.Repository}"));
        var version = Text($"Launcher {_launcherVersion}", 12, Alpha(120, Muted));
        version.Draw(g, new Point(DesignWidth - 42 - version.WidthIncludingTrailingWhitespace, DesignHeight - 41));
    }

    private double DrawLink(DrawingContext g, string id, string label, double x, double y, Action onClick)
    {
        var hover = _hover == id;
        var text = Text(label, 12, hover ? Colors.White : Alpha(200, Muted));
        var width = text.WidthIncludingTrailingWhitespace;
        text.Draw(g, new Point(x, y));
        if (hover)
        {
            g.DrawLine(Stroke(White(160)), new Point(x, y + text.Height), new Point(x + width, y + text.Height));
        }
        _zones.Add(new Zone(id, new Rect(x - 4, y - 3, width + 8, text.Height + 6), true, onClick));
        return x + width;
    }

    private void DrawButton(DrawingContext g, string id, Rect rect, ButtonSpec spec)
    {
        var hover = spec.Enabled && _hover == id;
        var pressed = hover && _pressed == id;
        if (pressed)
        {
            rect = rect.Translate(new Vector(0, 1));
        }
        Color fill, edge, textColor;
        switch (spec.Style)
        {
            case ButtonStyle.Primary:
                fill = hover ? BloodBright : Blood;
                edge = White(70);
                textColor = Colors.White;
                break;
            case ButtonStyle.Light:
                fill = hover ? Colors.White : Bone;
                edge = Colors.Transparent;
                textColor = Ink;
                break;
            default:
                fill = White(hover ? (byte)54 : (byte)26);
                edge = White(hover ? (byte)150 : (byte)80);
                textColor = TextColor;
                break;
        }
        if (!spec.Enabled)
        {
            fill = White(22);
            edge = White(30);
            textColor = Alpha(110, Muted);
        }
        if (hover && spec.Style == ButtonStyle.Primary)
        {
            // Soft red glow under the main button
            for (var i = 3; i >= 1; i--)
            {
                g.DrawRectangle(Brush(Alpha(16, BloodBright)), null, rect.Inflate(i * 3), 10 + i * 3, 10 + i * 3);
            }
        }
        g.DrawRectangle(Brush(fill), Stroke(edge), rect, 10, 10);

        var big = spec.Style != ButtonStyle.Glass || spec.Sub is not null;
        var label = big
            ? Text(spec.Text, 24, textColor, FontWeight.Bold, title: true, maxWidth: rect.Width - 12, ellipsis: true, center: true)
            : Text(spec.Text, 14.5, textColor, FontWeight.Bold, maxWidth: rect.Width - 12, ellipsis: true, center: true);
        if (spec.Sub is null)
        {
            DrawCentered(g, label, rect.Deflate(new Thickness(6, 0)));
        }
        else
        {
            DrawCentered(g, label, new Rect(rect.X + 6, rect.Y + 6, rect.Width - 12, 34));
            var sub = Text(spec.Sub, 11.5, Alpha(spec.Enabled ? (byte)200 : (byte)110, textColor), maxWidth: rect.Width - 12, ellipsis: true, center: true);
            DrawCentered(g, sub, new Rect(rect.X + 6, rect.Y + 38, rect.Width - 12, 20));
        }
        _zones.Add(new Zone(id, rect, spec.Enabled, spec.OnClick));
    }

    private void DrawProgress(DrawingContext g)
    {
        // Hairline along the bottom edge: fills while downloading, sweeps while the size is unknown
        var trackY = DesignHeight - 4.0;
        g.FillRectangle(Brush(White(_busy ? (byte)40 : (byte)14)), new Rect(0, trackY, DesignWidth, 4));
        if (!_busy)
        {
            return;
        }
        if (_fraction is { } fraction)
        {
            var width = DesignWidth * Math.Clamp(fraction, 0, 1);
            g.FillRectangle(Gradient(Blood, BloodBright, horizontal: true), new Rect(0, trackY, width, 4));
        }
        else
        {
            var phase = _clock.Elapsed.TotalSeconds % 1.6 / 1.6;
            var width = DesignWidth * 0.28;
            g.FillRectangle(Gradient(Blood, BloodBright, horizontal: true), new Rect(phase * (DesignWidth + width) - width, trackY, width, 4));
        }
    }

    /// <summary>Asked when the window is closed while an update downloads.</summary>
    private void DrawCloseQuestion(DrawingContext g)
    {
        _zones.Clear(); // only the two answers can be clicked
        g.FillRectangle(Brush(Alpha(200, Ink)), new Rect(0, 0, DesignWidth, DesignHeight));
        var card = new Rect((DesignWidth - 460) / 2.0, (DesignHeight - 220) / 2.0, 460, 220);
        g.DrawRectangle(Brush(Color.FromArgb(240, 14, 15, 20)), Stroke(White(40)), card, 14, 14);
        g.FillRectangle(Brush(Blood), new Rect(card.X + 28, card.Y + 31, 3, 16));
        Text(Lang.T("Ein Update läuft noch", "An update is still running"), 19, TextColor, FontWeight.Bold).Draw(g, new Point(card.X + 40, card.Y + 26));
        Text(Lang.T("Wenn du jetzt schließt, wird es abgebrochen. Die bisherige Version bleibt erhalten.",
                    "If you close now, it is cancelled. Your current version is kept."),
             14, Alpha(215, Muted), maxWidth: card.Width - 68, wrap: true).Draw(g, new Point(card.X + 28, card.Y + 66));
        var buttonTop = card.Bottom - 28 - 54;
        DrawButton(g, "close-yes", new Rect(card.X + 28, buttonTop, 194, 54),
            new ButtonSpec(Lang.T("SCHLIESSEN", "CLOSE"), null, ButtonStyle.Primary, true, () =>
            {
                _cts?.Cancel();
                _window.CloseConfirmed();
            }));
        DrawButton(g, "close-no", new Rect(card.Right - 28 - 194, buttonTop, 194, 54),
            new ButtonSpec(Lang.T("Weiter laden", "Keep downloading"), null, ButtonStyle.Glass, true, () =>
            {
                _askClose = false;
                InvalidateVisual();
            }));
    }

    // ------------------------------------------------------------------------------------------------
    // Drawing helpers

    private TextLayout Text(string text, double size, Color color, FontWeight weight = FontWeight.Normal, bool title = false, bool italic = false,
        double maxWidth = double.PositiveInfinity, bool wrap = false, double maxHeight = double.PositiveInfinity, bool ellipsis = false,
        bool center = false, double spacing = 0)
    {
        var face = new Typeface(title ? _titleFamily : _uiFamily, italic ? FontStyle.Italic : FontStyle.Normal, weight,
            title ? FontStretch.Condensed : FontStretch.Normal);
        return new TextLayout(text, face, size, Brush(color),
            textAlignment: center ? TextAlignment.Center : TextAlignment.Left,
            textWrapping: wrap ? TextWrapping.Wrap : TextWrapping.NoWrap,
            textTrimming: ellipsis ? TextTrimming.CharacterEllipsis : TextTrimming.None,
            maxWidth: maxWidth,
            maxHeight: maxHeight,
            // Paragraphs get Segoe UI's line spacing in every font (Roboto's own is tighter)
            lineHeight: wrap ? Math.Round(size * 1.33) : double.NaN,
            letterSpacing: spacing);
    }

    /// <summary>Text centred in the rectangle (a centred layout already fills its width).</summary>
    private static void DrawCentered(DrawingContext g, TextLayout text, Rect rect)
    {
        var width = double.IsFinite(text.MaxWidth) ? text.MaxWidth : text.WidthIncludingTrailingWhitespace;
        text.Draw(g, new Point(rect.X + (rect.Width - width) / 2, rect.Y + (rect.Height - text.Height) / 2));
    }

    private static Color Alpha(byte alpha, Color color) => Color.FromArgb(alpha, color.R, color.G, color.B);

    private static Color White(byte alpha) => Color.FromArgb(alpha, 255, 255, 255);

    private static IImmutableSolidColorBrush Brush(Color color) => new ImmutableSolidColorBrush(color);

    private static ImmutablePen Stroke(Color color, double thickness = 1) => new(Brush(color), thickness);

    private static IBrush Gradient(Color from, Color to, bool horizontal) => new LinearGradientBrush
    {
        StartPoint = new RelativePoint(0, 0, RelativeUnit.Relative),
        EndPoint = horizontal ? new RelativePoint(1, 0, RelativeUnit.Relative) : new RelativePoint(0, 1, RelativeUnit.Relative),
        GradientStops = { new GradientStop(from, 0), new GradientStop(to, 1) },
    };

    // ------------------------------------------------------------------------------------------------
    // Mouse

    private Zone? ZoneAt(Point point)
    {
        for (var i = _zones.Count - 1; i >= 0; i--)
        {
            if (_zones[i].Rect.Contains(point))
            {
                return _zones[i];
            }
        }
        return null;
    }

    protected override void OnPointerMoved(PointerEventArgs e)
    {
        base.OnPointerMoved(e);
        var zone = ZoneAt(e.GetPosition(this));
        var id = zone is { Enabled: true } ? zone.Id : null;
        if (id != _hover)
        {
            _hover = id;
            Cursor = id is null ? Cursor.Default : HandCursor;
            InvalidateVisual();
        }
    }

    protected override void OnPointerExited(PointerEventArgs e)
    {
        base.OnPointerExited(e);
        if (_hover is not null)
        {
            _hover = null;
            _pressed = null;
            Cursor = Cursor.Default;
            InvalidateVisual();
        }
    }

    protected override void OnPointerPressed(PointerPressedEventArgs e)
    {
        base.OnPointerPressed(e);
        var point = e.GetCurrentPoint(this);
        if (!point.Properties.IsLeftButtonPressed)
        {
            return;
        }
        var zone = ZoneAt(point.Position);
        if (zone is { Enabled: true })
        {
            _pressed = zone.Id;
            InvalidateVisual();
        }
        else if (zone is null && !_askClose && !_notesRect.Contains(point.Position))
        {
            // No title bar: the whole free area moves the window
            _window.BeginMoveDrag(e);
        }
    }

    protected override void OnPointerReleased(PointerReleasedEventArgs e)
    {
        base.OnPointerReleased(e);
        var pressed = _pressed;
        _pressed = null;
        InvalidateVisual();
        var zone = ZoneAt(e.GetPosition(this));
        if (e.InitialPressMouseButton == MouseButton.Left && zone is { Enabled: true } && zone.Id == pressed)
        {
            zone.OnClick();
        }
    }

    protected override void OnPointerWheelChanged(PointerWheelEventArgs e)
    {
        base.OnPointerWheelChanged(e);
        if (_notesOverflow <= 0 && _notesScroll <= 0)
        {
            return;
        }
        _notesScroll = Math.Clamp(_notesScroll - e.Delta.Y * 42, 0, _notesScroll + _notesOverflow);
        InvalidateVisual();
    }

    // ------------------------------------------------------------------------------------------------
    // Actions

    private void OpenGameFolder()
    {
        try
        {
            Directory.CreateDirectory(_config.InstallPath);
            // Explorer on Windows, the file manager (xdg-open) on Linux
            Process.Start(new ProcessStartInfo(_config.InstallPath) { UseShellExecute = true });
        }
        catch (Exception)
        {
            _status = new($"Spielordner: {_config.InstallPath}", $"Game folder: {_config.InstallPath}");
            InvalidateVisual();
        }
    }

    private static void OpenUrl(string url)
    {
        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch (Exception)
        {
            // no browser: nothing to do
        }
    }

    private void SetNotes(Func<(string Title, string Date, string Body)> source)
    {
        _notesSource = source;
        _notesScroll = 0;
        RebuildNotes();
    }

    private void RebuildNotes()
    {
        var (title, date, body) = _notesSource();
        if (body != _notesBody)
        {
            _notesScroll = 0;
        }
        _notesTitle = title;
        _notesDate = date;
        _notesBody = body;
        _notes = ParseNotes(body);
    }

    private void SetLanguage(bool english)
    {
        if (Lang.English == english)
        {
            return;
        }
        Lang.Set(english);
        _hover = null;
        Cursor = Cursor.Default;
        RebuildNotes();
        InvalidateVisual();
    }

    /// <summary>Closing while an update runs: ask first (painted over the window).</summary>
    public void AskToClose()
    {
        _askClose = true;
        _hover = null;
        Cursor = Cursor.Default;
        InvalidateVisual();
    }

    private static string ErrorText(Exception ex) => ex is PhraseException phrase ? phrase.Phrase.ToString() : ex.Message;

    /// <summary>
    /// Release notes are German first. An English version can follow under a heading "## English",
    /// optionally with the English title: "## English – Meeting chat and more". German shows the part above
    /// that heading, English the part below it; without an English part both languages show the German text.
    /// </summary>
    private static (string Title, string Body) NotesInLanguage(ReleaseInfo release)
    {
        var lines = release.Notes.Replace("\r", "").Split('\n');
        var marker = Array.FindIndex(lines, line => EnglishHeading(line) is not null);
        if (marker < 0)
        {
            return (release.Name, release.Notes);
        }
        var german = string.Join('\n', lines[..marker]);
        var english = string.Join('\n', lines[(marker + 1)..]);
        if (!Lang.English || string.IsNullOrWhiteSpace(english))
        {
            return (release.Name, german);
        }
        var title = EnglishHeading(lines[marker])!;
        return (title.Length > 0 ? $"{release.Tag} – {title}" : release.Name, english);
    }

    /// <summary>For a heading line "## English" or "**English – Title**": the title after "English" ("" if none). Otherwise null.</summary>
    private static string? EnglishHeading(string raw)
    {
        var line = raw.Trim();
        if (!line.StartsWith('#') && !line.StartsWith("**", StringComparison.Ordinal))
        {
            return null;
        }
        var text = line.TrimStart('#', ' ').Replace("**", "").Trim();
        if (!text.StartsWith("English", StringComparison.OrdinalIgnoreCase) || (text.Length > 7 && !" –—-:".Contains(text[7])))
        {
            return null;
        }
        return text[7..].TrimStart(' ', '–', '—', '-', ':').Trim();
    }

    /// <summary>Release notes are written in simple Markdown: "**Heading**", "- bullet", plain paragraphs.</summary>
    private static List<NoteLine> ParseNotes(string body)
    {
        var result = new List<NoteLine>();
        foreach (var raw in body.Replace("\r", "").Split('\n'))
        {
            var line = raw.Trim();
            if (line.Length == 0 || (line.Length >= 3 && line.All(c => c is '-' or '*' or '_')))
            {
                continue; // blank lines and horizontal rules ("---")
            }
            var kind = 0;
            if (line.StartsWith("- ", StringComparison.Ordinal) || line.StartsWith("* ", StringComparison.Ordinal))
            {
                kind = 2;
                line = line[2..];
            }
            else if (line.StartsWith('#') || (line.StartsWith("**", StringComparison.Ordinal) && line.EndsWith("**", StringComparison.Ordinal) && line.Length > 4))
            {
                kind = 1;
                line = line.TrimStart('#', ' ');
            }
            line = line.Replace("**", "").Replace("`", "").Replace("*", "");
            result.Add(new NoteLine(line, kind));
        }
        return result;
    }

    public async Task CheckForUpdatesAsync()
    {
        if (_busy)
        {
            return;
        }
        SetBusy(true, new("Suche nach Updates ...", "Checking for updates ..."));
        try
        {
            _cts = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            _lastCheck = await _updater.CheckAsync(_cts.Token);
            ApplyPreview();
            var latest = _lastCheck.Latest;
            if (_lastCheck.State == UpdateState.UpdateAvailable && latest is not null && _preview is null && _estimate?.Tag != latest.Tag)
            {
                _ = EstimateAsync(latest);
            }
            if (latest is null)
            {
                var asset = _config.AssetNameContains;
                SetNotes(() => (Lang.T("Noch keine Version", "No version yet"), "", Lang.T(
                    "Auf GitHub gibt es noch keine Version des Spiels für dieses System.\n\nSobald ein Release mit einer Datei \"" + asset + "....zip\" veröffentlicht ist, kannst du es hier installieren.",
                    "There is no version of the game for this system on GitHub yet.\n\nAs soon as a release with a file \"" + asset + "....zip\" is published, you can install it here.")));
            }
            else
            {
                var release = _notesFile is null ? latest : latest with { Notes = File.ReadAllText(_notesFile) };
                SetNotes(() =>
                {
                    var (title, body) = NotesInLanguage(release);
                    return (title, Lang.Date(release.PublishedAt.LocalDateTime), body);
                });
            }
            _status = _lastCheck.State switch
            {
                UpdateState.NotInstalled => new("Das Spiel ist noch nicht installiert.", "The game isn't installed yet."),
                UpdateState.UpdateAvailable => new("Ein Update ist verfügbar.", "An update is available."),
                UpdateState.UpToDate => new("Das Spiel ist auf dem neuesten Stand.", "The game is up to date."),
                _ => new("Keine Version gefunden.", "No version found."),
            };
            if (Program.UpdatedFrom is not null && !_updatedNoteShown)
            {
                _updatedNoteShown = true;
                var version = SelfUpdater.CurrentVersion.ToString(3);
                _status = new($"Launcher auf {version} aktualisiert. {_status.De}", $"Launcher updated to {version}. {_status.En}");
            }
        }
        catch (OperationCanceledException)
        {
            _status = new("Zeitüberschreitung bei der Update-Suche. Bist du online?", "The update check timed out. Are you online?");
        }
        catch (Exception ex)
        {
            _status = new("Update-Suche fehlgeschlagen.", "The update check failed.");
            SetNotes(() => (Lang.T("Keine Verbindung", "No connection"), "", ErrorText(ex)));
        }
        finally
        {
            SetBusy(false, null);
            if (_preview is "busy" or "close")
            {
                _busy = true;
                _installing = true;
                _fraction = 0.42;
                _status = new("Lade herunter: 236.0 MB / 563.0 MB", "Downloading: 236.0 MB / 563.0 MB");
                // "close": the question shown when the window is closed during a download
                _askClose = _preview == "close";
            }
        }
        if (_preview is null && _notesFile is null && !_selfUpdateStarted && _launcherReady is null)
        {
            _ = SelfUpdateAsync();
        }
    }

    /// <summary>Puts a newer launcher from the releases in place of this one (see <see cref="SelfUpdater"/>).</summary>
    private async Task SelfUpdateAsync()
    {
        _selfUpdateStarted = true;
        var before = _status;
        Phrase? shown = null;
        try
        {
            using var cts = new CancellationTokenSource(TimeSpan.FromMinutes(10));
            var update = await _updater.FindLauncherUpdateAsync(cts.Token);
            if (update is null)
            {
                _selfUpdateStarted = false;
                return;
            }
            if (!_busy)
            {
                var version = update.Version.ToString(3);
                _status = (shown = new($"Lade den neuen Launcher {version} ...", $"Downloading the new launcher {version} ...")).Value;
                InvalidateVisual();
            }
            await _updater.DownloadLauncherAsync(update, cts.Token);
            _launcherReady = update.Version;
            RestartIfIdle();
        }
        catch (Exception)
        {
            // Offline, no write access next to the launcher, damaged download: this launcher just keeps running
            // (details in update.log); the next check tries again
            _selfUpdateStarted = false;
            if (shown is { } phrase && _status == phrase)
            {
                _status = before;
                InvalidateVisual();
            }
        }
    }

    /// <summary>Starts the new launcher and closes this one, unless a game update runs or a game was started from here.</summary>
    private void RestartIfIdle()
    {
        if (_launcherReady is not { } ready)
        {
            return;
        }
        var version = ready.ToString(3);
        if (_busy || _installing || _gameStarted)
        {
            if (!_installing)
            {
                _status = new($"Neuer Launcher {version} ist bereit – er startet beim nächsten Öffnen.", $"New launcher {version} is ready – it starts the next time you open it.");
                InvalidateVisual();
            }
            return;
        }
        _status = new($"Starte den neuen Launcher {version} ...", $"Starting the new launcher {version} ...");
        InvalidateVisual();
        DispatcherTimer.RunOnce(() =>
        {
            if (_busy || _installing || _gameStarted)
            {
                RestartIfIdle();
                return;
            }
            try
            {
                SelfUpdater.LaunchPending("--after-self-update", SelfUpdater.CurrentVersion.ToString(3));
            }
            catch (Exception)
            {
                _status = new($"Neuer Launcher {version} ist bereit – er startet beim nächsten Öffnen.", $"New launcher {version} is ready – it starts the next time you open it.");
                _launcherReady = null;
                InvalidateVisual();
                return;
            }
            _window.CloseConfirmed();
        }, TimeSpan.FromMilliseconds(900));
    }

    /// <summary>Screenshot self-test only: pretend a different state than the real one.</summary>
    private void ApplyPreview()
    {
        if (_preview is null || _lastCheck?.Latest is null)
        {
            return;
        }
        _lastCheck = _preview switch
        {
            "update" => new UpdateCheckResult(UpdateState.UpdateAvailable, _lastCheck.Latest, "v0.7.0"),
            "install" => new UpdateCheckResult(UpdateState.NotInstalled, _lastCheck.Latest, null),
            _ => _lastCheck,
        };
    }

    private async Task InstallAsync(ReleaseInfo release)
    {
        if (_busy)
        {
            return;
        }
        _installing = true;
        _estimateCts?.Cancel();
        SetBusy(true, new("Starte Download ...", "Starting download ..."));
        _cts = new CancellationTokenSource();
        var progress = new Progress<(Phrase Status, double? Fraction)>(p =>
        {
            _status = p.Status;
            _fraction = p.Fraction;
            InvalidateVisual();
        });
        try
        {
            var result = await _updater.InstallAsync(release, progress, _cts.Token);
            _lastCheck = new UpdateCheckResult(UpdateState.UpToDate, release, release.Tag);
            _status = result.Patched
                ? new($"{release.Tag} ist installiert (nur {GitHubUpdater.FormatSize(result.Downloaded)} geladen). Viel Spaß – und trau niemandem.",
                      $"{release.Tag} is installed (only {GitHubUpdater.FormatSize(result.Downloaded)} downloaded). Have fun – and trust no one.")
                : new($"{release.Tag} ist installiert. Viel Spaß – und trau niemandem.", $"{release.Tag} is installed. Have fun – and trust no one.");
        }
        catch (OperationCanceledException)
        {
            _status = new("Update abgebrochen. Die bisherige Version bleibt erhalten.", "Update cancelled. Your current version is kept.");
            GitHubUpdater.CleanupLeftovers();
        }
        catch (Exception ex)
        {
            _status = new("Update fehlgeschlagen. Die bisherige Version bleibt erhalten.", "Update failed. Your current version is kept.");
            SetNotes(() => (Lang.T("Update fehlgeschlagen", "Update failed"), "", ErrorText(ex)));
            GitHubUpdater.CleanupLeftovers();
        }
        finally
        {
            _installing = false;
            SetBusy(false, null);
        }
        RestartIfIdle();
    }

    private void Play()
    {
        var exe = _updater.FindGameExecutable();
        if (exe is null)
        {
            _status = new("Das Spiel ist noch nicht installiert.", "The game isn't installed yet.");
            InvalidateVisual();
            return;
        }
        try
        {
            // Linux: the start script runs directly (it must be executable); Windows: as if double-clicked
            GitHubUpdater.MarkExecutables(_config.InstallPath);
            Process.Start(new ProcessStartInfo(exe) { UseShellExecute = OperatingSystem.IsWindows(), WorkingDirectory = Path.GetDirectoryName(exe)! });
            _gameStarted = true;
            _window.WindowState = WindowState.Minimized;
        }
        catch (Exception ex)
        {
            _status = new("Das Spiel ließ sich nicht starten.", "The game could not be started.");
            SetNotes(() => (Lang.T("Start fehlgeschlagen", "Start failed"), "", $"{exe}\n\n{ex.Message}"));
            InvalidateVisual();
        }
    }

    private void SetBusy(bool busy, Phrase? status)
    {
        _busy = busy;
        _fraction = null;
        if (status is { } text)
        {
            _status = text;
        }
        if (!busy)
        {
            _cts?.Dispose();
            _cts = null;
        }
        InvalidateVisual();
    }
}
