using System.Diagnostics;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.Drawing.Text;
using System.Reflection;
using System.Runtime.InteropServices;

namespace HowCouldItBeYou.Launcher;

/// <summary>
/// Launcher window. Borderless and painted completely by hand: game artwork as a slowly changing backdrop,
/// title on the left, release notes on a glass card on the right, play/update at the bottom.
/// Everything is laid out in a fixed design size and scaled with the monitor's DPI.
/// </summary>
public sealed class MainForm : Form
{
    private const int DesignWidth = 1060;
    private const int DesignHeight = 640;

    private static readonly Color Ink = Color.FromArgb(8, 9, 12);
    private static readonly Color Bone = Color.FromArgb(236, 228, 212);
    private static readonly Color TextColor = Color.FromArgb(232, 234, 238);
    private static readonly Color Muted = Color.FromArgb(150, 156, 168);
    private static readonly Color Blood = Color.FromArgb(193, 18, 31);
    private static readonly Color BloodBright = Color.FromArgb(226, 38, 50);

    private enum ButtonStyle { Primary, Light, Glass }

    private sealed record ButtonSpec(string Text, string? Sub, ButtonStyle Style, bool Enabled, Action OnClick);
    private sealed record Zone(string Id, RectangleF Rect, bool Enabled, Action OnClick);
    private sealed record NoteLine(string Text, int Kind); // 0 = paragraph, 1 = heading, 2 = bullet

    private readonly LauncherConfig _config;
    private readonly GitHubUpdater _updater;
    private readonly string? _preview;
    private readonly List<Bitmap> _slides = [];
    private readonly List<Bitmap?> _slideCache = [];
    private readonly List<Zone> _zones = [];
    private readonly System.Windows.Forms.Timer _timer = new() { Interval = 33 };
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly string _titleFamily;

    private UpdateCheckResult? _lastCheck;
    private CancellationTokenSource? _cts;
    private bool _busy;
    private bool _installing;
    private Phrase _status = new("Suche nach Updates ...", "Checking for updates ...");
    private double? _fraction;
    // Where the news card's text comes from; asked again when the language changes
    private Func<(string Title, string Date, string Body)> _notesSource = () => ("", "", Lang.T("Suche nach Updates ...", "Checking for updates ..."));
    private string _notesTitle = "";
    private string _notesDate = "";
    private string _notesBody = "";
    private List<NoteLine> _notes = [];
    private float _notesScroll;
    private float _notesOverflow;
    private RectangleF _notesRect;
    private string? _hover;
    private string? _pressed;
    private int _slide;
    private double _slideSince;
    private float _scale = 1f;

    /// <param name="preview">Only for the screenshot self-test: "update", "install" or "busy" shows that state.</param>
    public MainForm(string? preview = null)
    {
        _preview = preview;
        _config = LauncherConfig.Load();
        _updater = new GitHubUpdater(_config);
        _titleFamily = FirstInstalledFont("Bahnschrift", "Segoe UI Black", "Segoe UI");

        Text = "How Could It Be You";
        // Same icon as the exe file, for the taskbar and Alt+Tab
        var iconName = Assembly.GetExecutingAssembly().GetManifestResourceNames().FirstOrDefault(n => n.EndsWith(".app.ico", StringComparison.Ordinal));
        if (iconName is not null)
        {
            using var iconStream = Assembly.GetExecutingAssembly().GetManifestResourceStream(iconName);
            if (iconStream is not null)
            {
                Icon = new Icon(iconStream);
            }
        }
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.CenterScreen;
        AutoScaleMode = AutoScaleMode.None;
        BackColor = Ink;
        DoubleBuffered = true;
        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.UserPaint | ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);

        LoadSlides();
        RebuildNotes();
        ApplyScale();

        _timer.Tick += (_, _) => OnTick();
        _timer.Start();
        Shown += async (_, _) => await CheckForUpdatesAsync();
        FormClosing += OnFormClosing;
    }

    // ------------------------------------------------------------------------------------------------
    // Window

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.Style |= 0x00020000;      // WS_MINIMIZEBOX: minimise from the taskbar although there is no title bar
            cp.ClassStyle |= 0x00020000; // CS_DROPSHADOW
            return cp;
        }
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

    [DllImport("user32.dll")]
    private static extern bool ReleaseCapture();

    [DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam);

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        try
        {
            var round = 2; // DWMWCP_ROUND (Windows 11); older systems just keep square corners
            DwmSetWindowAttribute(Handle, 33, ref round, sizeof(int));
        }
        catch (Exception)
        {
            // cosmetic only
        }
    }

    protected override void OnDpiChanged(DpiChangedEventArgs e)
    {
        base.OnDpiChanged(e);
        ApplyScale();
    }

    private void ApplyScale()
    {
        _scale = Math.Max(1f, DeviceDpi / 96f);
        ClientSize = new Size((int)Math.Round(DesignWidth * _scale), (int)Math.Round(DesignHeight * _scale));
        for (var i = 0; i < _slideCache.Count; i++)
        {
            _slideCache[i]?.Dispose();
            _slideCache[i] = null;
        }
        Invalidate();
    }

    private static string FirstInstalledFont(params string[] names)
    {
        using var fonts = new InstalledFontCollection();
        var installed = fonts.Families.Select(f => f.Name).ToHashSet(StringComparer.OrdinalIgnoreCase);
        return names.FirstOrDefault(installed.Contains) ?? "Segoe UI";
    }

    private void LoadSlides()
    {
        var assembly = Assembly.GetExecutingAssembly();
        foreach (var name in assembly.GetManifestResourceNames().Where(n => n.Contains(".Assets.hero_", StringComparison.Ordinal)).OrderBy(n => n, StringComparer.Ordinal))
        {
            try
            {
                using var stream = assembly.GetManifestResourceStream(name);
                if (stream is null)
                {
                    continue;
                }
                using var image = Image.FromStream(stream);
                _slides.Add(new Bitmap(image));
                _slideCache.Add(null);
            }
            catch (Exception)
            {
                // a broken picture must never stop the launcher
            }
        }
    }

    /// <summary>The picture scaled to cover the whole window, kept until the window size changes.</summary>
    private Bitmap? SlideFor(int index)
    {
        if (_slides.Count == 0)
        {
            return null;
        }
        index %= _slides.Count;
        if (_slideCache[index] is { } cached)
        {
            return cached;
        }
        var source = _slides[index];
        var target = new Bitmap(ClientSize.Width, ClientSize.Height, PixelFormat.Format32bppPArgb);
        using (var g = Graphics.FromImage(target))
        {
            g.InterpolationMode = InterpolationMode.HighQualityBicubic;
            var factor = Math.Max((float)target.Width / source.Width, (float)target.Height / source.Height);
            var w = source.Width * factor;
            var h = source.Height * factor;
            g.DrawImage(source, (target.Width - w) / 2f, (target.Height - h) / 2f, w, h);
        }
        _slideCache[index] = target;
        return target;
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
            Invalidate();
        }
    }

    private const double SlideSeconds = 9.0;
    private const double FadeSeconds = 1.6;

    // ------------------------------------------------------------------------------------------------
    // Painting

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        _zones.Clear();
        DrawBackdrop(g);

        g.ScaleTransform(_scale, _scale);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;
        g.PixelOffsetMode = PixelOffsetMode.HighQuality;

        DrawShade(g);
        DrawChrome(g);
        DrawTitle(g);
        DrawNotes(g);
        DrawActions(g);
        DrawProgress(g);
    }

    private void DrawBackdrop(Graphics g)
    {
        g.Clear(Ink);
        if (SlideFor(_slide) is { } current)
        {
            g.DrawImageUnscaled(current, 0, 0);
        }
        var sinceSlide = _clock.Elapsed.TotalSeconds - _slideSince;
        if (_slides.Count > 1 && sinceSlide > SlideSeconds && SlideFor(_slide + 1) is { } next)
        {
            var alpha = (float)Math.Clamp((sinceSlide - SlideSeconds) / FadeSeconds, 0.0, 1.0);
            using var attributes = new ImageAttributes();
            attributes.SetColorMatrix(new ColorMatrix { Matrix33 = alpha });
            g.DrawImage(next, new Rectangle(0, 0, next.Width, next.Height), 0, 0, next.Width, next.Height, GraphicsUnit.Pixel, attributes);
        }
    }

    /// <summary>Dark gradients over the artwork, so text stays readable on any picture.</summary>
    private static void DrawShade(Graphics g)
    {
        var all = new RectangleF(0, 0, DesignWidth, DesignHeight);
        using (var left = new LinearGradientBrush(new RectangleF(-1, 0, 761, DesignHeight), Color.FromArgb(240, Ink), Color.FromArgb(0, Ink), LinearGradientMode.Horizontal))
        {
            g.FillRectangle(left, 0, 0, 760, DesignHeight);
        }
        using (var bottom = new LinearGradientBrush(new RectangleF(0, DesignHeight - 301, DesignWidth, 302), Color.FromArgb(0, Ink), Color.FromArgb(245, Ink), LinearGradientMode.Vertical))
        {
            g.FillRectangle(bottom, 0, DesignHeight - 300, DesignWidth, 300);
        }
        using (var top = new LinearGradientBrush(new RectangleF(0, -1, DesignWidth, 92), Color.FromArgb(170, Ink), Color.FromArgb(0, Ink), LinearGradientMode.Vertical))
        {
            g.FillRectangle(top, 0, 0, DesignWidth, 90);
        }
        using var veil = new SolidBrush(Color.FromArgb(40, Ink));
        g.FillRectangle(veil, all);
    }

    private void DrawChrome(Graphics g)
    {
        using var small = MakeFont("Segoe UI", 11f, FontStyle.Bold);
        using var muted = new SolidBrush(Color.FromArgb(170, Muted));
        // Small red marker + wordmark
        using (var blood = new SolidBrush(Blood))
        {
            g.FillRectangle(blood, 40, 31, 3, 14);
        }
        DrawSpaced(g, "KEPLER-7  //  LAUNCHER", small, muted, 52, 30, 2.2f);

        DrawLanguageSwitch(g, new RectangleF(DesignWidth - 104 - 14 - 84, 17, 84, 28));
        DrawWindowButton(g, "min", new RectangleF(DesignWidth - 104, 14, 44, 34), false, () => WindowState = FormWindowState.Minimized);
        DrawWindowButton(g, "close", new RectangleF(DesignWidth - 56, 14, 44, 34), true, Close);
    }

    /// <summary>DE | EN pill next to the window buttons; the active language is filled red.</summary>
    private void DrawLanguageSwitch(Graphics g, RectangleF rect)
    {
        using (var path = Rounded(rect, rect.Height / 2f))
        {
            using var fill = new SolidBrush(Color.FromArgb(26, 255, 255, 255));
            using var edge = new Pen(Color.FromArgb(60, 255, 255, 255), 1f);
            g.FillPath(fill, path);
            g.DrawPath(edge, path);
        }
        using var font = MakeFont("Segoe UI", 11f, FontStyle.Bold);
        using var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        var half = rect.Width / 2f;
        foreach (var (id, label, english) in new[] { ("lang-de", "DE", false), ("lang-en", "EN", true) })
        {
            var segment = new RectangleF(rect.X + (english ? half : 0), rect.Y, half, rect.Height);
            var active = Lang.English == english;
            var hover = !active && _hover == id;
            if (active || hover)
            {
                var pill = segment;
                pill.Inflate(-3, -3);
                using var path = Rounded(pill, pill.Height / 2f);
                using var back = new SolidBrush(active ? Blood : Color.FromArgb(40, 255, 255, 255));
                g.FillPath(back, path);
            }
            using var brush = new SolidBrush(active || hover ? Color.White : Color.FromArgb(200, Muted));
            g.DrawString(label, font, brush, segment, center);
            // The active half isn't clickable, so it shows no hand cursor
            _zones.Add(new Zone(id, segment, !active, () => SetLanguage(english)));
        }
    }

    private void DrawWindowButton(Graphics g, string id, RectangleF rect, bool close, Action onClick)
    {
        var hover = _hover == id;
        if (hover)
        {
            using var back = new SolidBrush(close ? Blood : Color.FromArgb(40, 255, 255, 255));
            using var path = Rounded(rect, 8);
            g.FillPath(back, path);
        }
        using var pen = new Pen(hover ? Color.White : Color.FromArgb(200, Muted), 1.4f);
        var cx = rect.X + rect.Width / 2f;
        var cy = rect.Y + rect.Height / 2f;
        if (close)
        {
            g.DrawLine(pen, cx - 5, cy - 5, cx + 5, cy + 5);
            g.DrawLine(pen, cx - 5, cy + 5, cx + 5, cy - 5);
        }
        else
        {
            g.DrawLine(pen, cx - 6, cy + 4, cx + 6, cy + 4);
        }
        _zones.Add(new Zone(id, rect, true, onClick));
    }

    private void DrawTitle(Graphics g)
    {
        using var tag = MakeFont("Segoe UI", 11.5f, FontStyle.Bold);
        using var blood = new SolidBrush(BloodBright);
        DrawSpaced(g, Lang.T("SOCIAL-DEDUCTION-HORROR   /   4 BIS 8 SPIELER", "SOCIAL DEDUCTION HORROR   /   4 TO 8 PLAYERS"), tag, blood, 42, 150, 2.4f);

        using var title = MakeFont(_titleFamily, 76f, FontStyle.Bold);
        using var white = new SolidBrush(TextColor);
        using var shadow = new SolidBrush(Color.FromArgb(150, 0, 0, 0));
        var format = StringFormat.GenericTypographic;
        foreach (var (line, y) in new[] { ("HOW COULD", 172f), ("IT BE YOU", 244f) })
        {
            g.DrawString(line, title, shadow, 40, y + 3, format);
            g.DrawString(line, title, white, 38, y, format);
        }

        using var claim = MakeFont("Segoe UI", 21f, FontStyle.Italic);
        using var bone = new SolidBrush(Bone);
        g.DrawString(Lang.T("Es ist einer von euch.", "It's one of you."), claim, bone, 40, 336, format);

        using var body = MakeFont("Segoe UI", 14f, FontStyle.Regular);
        using var muted = new SolidBrush(Color.FromArgb(215, Muted));
        g.DrawString(Lang.T("Eine Forscher-Crew auf der Raumstation Kepler-7. Einer von euch ist der Wandler: " +
                            "Er sieht aus wie du. Er klingt nicht wie du.",
                            "A research crew on the space station Kepler-7. One of you is the Shifter: " +
                            "It looks like you. It doesn't sound like you."), body, muted, new RectangleF(40, 378, 430, 70));
    }

    private void DrawNotes(Graphics g)
    {
        var card = new RectangleF(DesignWidth - 40 - 372, 92, 372, 366);
        using (var path = Rounded(card, 14))
        {
            using var fill = new SolidBrush(Color.FromArgb(168, 10, 11, 15));
            using var edge = new Pen(Color.FromArgb(34, 255, 255, 255), 1f);
            g.FillPath(fill, path);
            g.DrawPath(edge, path);
        }
        using var small = MakeFont("Segoe UI", 10.5f, FontStyle.Bold);
        using var muted = new SolidBrush(Muted);
        using var white = new SolidBrush(TextColor);
        using var blood = new SolidBrush(BloodBright);
        DrawSpaced(g, Lang.T("NEUIGKEITEN", "NEWS"), small, muted, card.X + 22, card.Y + 20, 2.2f);
        if (_notesDate.Length > 0)
        {
            var width = g.MeasureString(_notesDate, small).Width;
            g.DrawString(_notesDate, small, muted, card.Right - 20 - width, card.Y + 19);
        }
        var top = card.Y + 48;
        if (_notesTitle.Length > 0)
        {
            using var head = MakeFont("Segoe UI", 17f, FontStyle.Bold);
            var size = g.MeasureString(_notesTitle, head, (int)card.Width - 44);
            g.DrawString(_notesTitle, head, white, new RectangleF(card.X + 22, top, card.Width - 44, size.Height));
            top += size.Height + 8;
        }
        using (var rule = new Pen(Color.FromArgb(30, 255, 255, 255)))
        {
            g.DrawLine(rule, card.X + 22, top, card.Right - 22, top);
        }
        top += 10;

        // Scrollable text
        var inner = new RectangleF(card.X + 22, top, card.Width - 44, card.Bottom - top - 16);
        _notesRect = inner;
        using var body = MakeFont("Segoe UI", 13.5f, FontStyle.Regular);
        using var heading = MakeFont("Segoe UI", 11f, FontStyle.Bold);
        using var text = new SolidBrush(Color.FromArgb(225, TextColor));
        var state = g.Save();
        g.SetClip(inner);
        var y = inner.Y - _notesScroll;
        foreach (var line in _notes)
        {
            switch (line.Kind)
            {
                case 1:
                    y += 6;
                    DrawSpaced(g, line.Text.ToUpperInvariant(), heading, blood, inner.X, y, 1.6f);
                    y += 22;
                    break;
                case 2:
                {
                    var size = g.MeasureString(line.Text, body, (int)inner.Width - 16);
                    g.DrawString("•", body, blood, inner.X, y);
                    g.DrawString(line.Text, body, text, new RectangleF(inner.X + 16, y, inner.Width - 16, size.Height + 2));
                    y += size.Height + 5;
                    break;
                }
                default:
                {
                    var size = g.MeasureString(line.Text, body, (int)inner.Width);
                    g.DrawString(line.Text, body, text, new RectangleF(inner.X, y, inner.Width, size.Height + 2));
                    y += size.Height + 9;
                    break;
                }
            }
        }
        g.Restore(state);
        _notesOverflow = Math.Max(0, y - inner.Bottom);
        if (_notesOverflow + _notesScroll > 0)
        {
            // Slim scroll thumb on the right edge of the card
            var total = inner.Height + _notesScroll + _notesOverflow;
            var thumb = Math.Max(24, inner.Height * inner.Height / total);
            var offset = (inner.Height - thumb) * (_notesScroll / (_notesScroll + _notesOverflow));
            using var bar = new SolidBrush(Color.FromArgb(70, 255, 255, 255));
            using var path = Rounded(new RectangleF(card.Right - 10, inner.Y + offset, 3, thumb), 1.5f);
            g.FillPath(bar, path);
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
                return (new ButtonSpec("UPDATE", $"{_lastCheck!.InstalledVersion} → {latest.Tag}  ·  {GitHubUpdater.FormatSize(latest.AssetSize)}", ButtonStyle.Light, !_busy, () => _ = InstallAsync(latest)),
                    new ButtonSpec(Lang.T("SPIELEN", "PLAY"), Lang.T("ohne Update", "without updating"), ButtonStyle.Glass, canPlay, Play));
            default:
                return (new ButtonSpec(Lang.T("SPIELEN", "PLAY"), null, ButtonStyle.Primary, canPlay, Play),
                    new ButtonSpec(Lang.T("Nach Updates suchen", "Check for updates"), null, ButtonStyle.Glass, !_busy, () => _ = CheckForUpdatesAsync()));
        }
    }

    private void DrawActions(Graphics g)
    {
        var (primary, secondary) = ComputeButtons();
        var top = DesignHeight - 124f;
        DrawButton(g, "primary", new RectangleF(40, top, 250, 66), primary);
        float next = 304;
        if (secondary is not null)
        {
            DrawButton(g, "secondary", new RectangleF(next, top, 214, 66), secondary);
            next += 214 + 14;
        }

        // Status and versions to the right of the buttons
        using var statusFont = MakeFont("Segoe UI", 14f, FontStyle.Regular);
        using var chipFont = MakeFont("Segoe UI", 11.5f, FontStyle.Bold);
        using var white = new SolidBrush(TextColor);
        using var muted = new SolidBrush(Muted);
        var statusRect = new RectangleF(next + 14, top + 8, DesignWidth - 40 - next - 14, 24);
        using (var format = new StringFormat { Trimming = StringTrimming.EllipsisCharacter, FormatFlags = StringFormatFlags.NoWrap })
        {
            g.DrawString(_status.ToString(), statusFont, white, statusRect, format);
        }
        var installed = _updater.GetInstalledVersion() ?? "–";
        var newest = _lastCheck?.Latest?.Tag ?? "?";
        DrawSpaced(g, Lang.T($"INSTALLIERT {installed}   ·   NEUESTE {newest}", $"INSTALLED {installed}   ·   LATEST {newest}").ToUpperInvariant(), chipFont, muted, next + 16, top + 40, 1.3f);

        // Footer links
        using var link = MakeFont("Segoe UI", 12f, FontStyle.Regular);
        var x = 42f;
        x = DrawLink(g, "folder", Lang.T("Spielordner öffnen", "Open game folder"), link, x, DesignHeight - 42, OpenGameFolder) + 22;
        x = DrawLink(g, "github", Lang.T("Projekt auf GitHub", "Project on GitHub"), link, x, DesignHeight - 42, () => OpenUrl($"https://github.com/{_config.Repository}")) + 22;
        var version = $"Launcher {Application.ProductVersion.Split('+')[0]}";
        var width = g.MeasureString(version, link).Width;
        using var faint = new SolidBrush(Color.FromArgb(120, Muted));
        g.DrawString(version, link, faint, DesignWidth - 40 - width, DesignHeight - 42);
    }

    private float DrawLink(Graphics g, string id, string text, Font font, float x, float y, Action onClick)
    {
        var size = g.MeasureString(text, font);
        var rect = new RectangleF(x - 2, y - 2, size.Width + 4, size.Height + 4);
        var hover = _hover == id;
        using var brush = new SolidBrush(hover ? Color.White : Color.FromArgb(200, Muted));
        g.DrawString(text, font, brush, x, y);
        if (hover)
        {
            using var pen = new Pen(Color.FromArgb(160, 255, 255, 255));
            g.DrawLine(pen, x + 2, y + size.Height - 1, x + size.Width - 3, y + size.Height - 1);
        }
        _zones.Add(new Zone(id, rect, true, onClick));
        return x + size.Width;
    }

    private void DrawButton(Graphics g, string id, RectangleF rect, ButtonSpec spec)
    {
        var hover = spec.Enabled && _hover == id;
        var pressed = hover && _pressed == id;
        if (pressed)
        {
            rect.Offset(0, 1);
        }
        Color fill, edge, textColor;
        switch (spec.Style)
        {
            case ButtonStyle.Primary:
                fill = hover ? BloodBright : Blood;
                edge = Color.FromArgb(70, 255, 255, 255);
                textColor = Color.White;
                break;
            case ButtonStyle.Light:
                fill = hover ? Color.White : Bone;
                edge = Color.FromArgb(0, 0, 0, 0);
                textColor = Ink;
                break;
            default:
                fill = Color.FromArgb(hover ? 54 : 26, 255, 255, 255);
                edge = Color.FromArgb(hover ? 150 : 80, 255, 255, 255);
                textColor = TextColor;
                break;
        }
        if (!spec.Enabled)
        {
            fill = Color.FromArgb(22, 255, 255, 255);
            edge = Color.FromArgb(30, 255, 255, 255);
            textColor = Color.FromArgb(110, Muted);
        }
        using var path = Rounded(rect, 10);
        if (hover && spec.Style == ButtonStyle.Primary)
        {
            // Soft red glow under the main button
            for (var i = 3; i >= 1; i--)
            {
                var halo = rect;
                halo.Inflate(i * 3, i * 3);
                using var glowPath = Rounded(halo, 10 + i * 3);
                using var glow = new SolidBrush(Color.FromArgb(16, BloodBright));
                g.FillPath(glow, glowPath);
            }
        }
        using (var brush = new SolidBrush(fill))
        {
            g.FillPath(brush, path);
        }
        using (var pen = new Pen(edge, 1f))
        {
            g.DrawPath(pen, path);
        }
        var big = spec.Style != ButtonStyle.Glass || spec.Sub is not null;
        using var font = big ? MakeFont(_titleFamily, 24f, FontStyle.Bold) : MakeFont("Segoe UI", 14.5f, FontStyle.Bold);
        using var subFont = MakeFont("Segoe UI", 11.5f, FontStyle.Regular);
        using var textBrush = new SolidBrush(textColor);
        using var center = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center, FormatFlags = StringFormatFlags.NoWrap, Trimming = StringTrimming.EllipsisCharacter };
        if (spec.Sub is null)
        {
            g.DrawString(spec.Text, font, textBrush, rect, center);
        }
        else
        {
            g.DrawString(spec.Text, font, textBrush, new RectangleF(rect.X, rect.Y + 6, rect.Width, 34), center);
            using var subBrush = new SolidBrush(Color.FromArgb(spec.Enabled ? 200 : 110, textColor));
            g.DrawString(spec.Sub, subFont, subBrush, new RectangleF(rect.X + 6, rect.Y + 38, rect.Width - 12, 20), center);
        }
        _zones.Add(new Zone(id, rect, spec.Enabled, spec.OnClick));
    }

    private void DrawProgress(Graphics g)
    {
        // Hairline along the bottom edge: fills while downloading, sweeps while the size is unknown
        var track = new RectangleF(0, DesignHeight - 4, DesignWidth, 4);
        using var back = new SolidBrush(Color.FromArgb(_busy ? 40 : 14, 255, 255, 255));
        g.FillRectangle(back, track);
        if (!_busy)
        {
            return;
        }
        using var fill = new LinearGradientBrush(new RectangleF(-1, 0, DesignWidth + 2, 4), Blood, BloodBright, LinearGradientMode.Horizontal);
        if (_fraction is { } fraction)
        {
            g.FillRectangle(fill, 0, track.Y, (float)(DesignWidth * Math.Clamp(fraction, 0, 1)), 4);
        }
        else
        {
            var phase = (float)(_clock.Elapsed.TotalSeconds % 1.6 / 1.6);
            var width = DesignWidth * 0.28f;
            g.FillRectangle(fill, phase * (DesignWidth + width) - width, track.Y, width, 4);
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Drawing helpers

    private static Font MakeFont(string family, float pixels, FontStyle style) => new(family, pixels, style, GraphicsUnit.Pixel);

    private static GraphicsPath Rounded(RectangleF rect, float radius)
    {
        var d = radius * 2;
        var path = new GraphicsPath();
        path.AddArc(rect.X, rect.Y, d, d, 180, 90);
        path.AddArc(rect.Right - d, rect.Y, d, d, 270, 90);
        path.AddArc(rect.Right - d, rect.Bottom - d, d, d, 0, 90);
        path.AddArc(rect.X, rect.Bottom - d, d, d, 90, 90);
        path.CloseFigure();
        return path;
    }

    /// <summary>Text with extra space between the letters (small labels).</summary>
    private static void DrawSpaced(Graphics g, string text, Font font, Brush brush, float x, float y, float spacing)
    {
        var format = StringFormat.GenericTypographic;
        format.FormatFlags |= StringFormatFlags.MeasureTrailingSpaces;
        foreach (var ch in text)
        {
            var s = ch.ToString();
            g.DrawString(s, font, brush, x, y, format);
            x += g.MeasureString(s, font, 1000, format).Width + spacing;
        }
    }

    // ------------------------------------------------------------------------------------------------
    // Mouse

    private PointF ToDesign(Point p) => new(p.X / _scale, p.Y / _scale);

    private Zone? ZoneAt(Point p)
    {
        var point = ToDesign(p);
        for (var i = _zones.Count - 1; i >= 0; i--)
        {
            if (_zones[i].Rect.Contains(point))
            {
                return _zones[i];
            }
        }
        return null;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        var zone = ZoneAt(e.Location);
        var id = zone is { Enabled: true } ? zone.Id : null;
        if (id != _hover)
        {
            _hover = id;
            Cursor = id is null ? Cursors.Default : Cursors.Hand;
            Invalidate();
        }
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        base.OnMouseLeave(e);
        if (_hover is not null)
        {
            _hover = null;
            _pressed = null;
            Cursor = Cursors.Default;
            Invalidate();
        }
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left)
        {
            return;
        }
        var zone = ZoneAt(e.Location);
        if (zone is { Enabled: true })
        {
            _pressed = zone.Id;
            Invalidate();
        }
        else if (zone is null && !_notesRect.Contains(ToDesign(e.Location)))
        {
            // No title bar: the whole free area moves the window
            ReleaseCapture();
            SendMessage(Handle, 0x00A1, 2, IntPtr.Zero); // WM_NCLBUTTONDOWN, HTCAPTION
        }
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        var pressed = _pressed;
        _pressed = null;
        Invalidate();
        var zone = ZoneAt(e.Location);
        if (e.Button == MouseButtons.Left && zone is { Enabled: true } && zone.Id == pressed)
        {
            zone.OnClick();
        }
    }

    protected override void OnMouseWheel(MouseEventArgs e)
    {
        base.OnMouseWheel(e);
        if (_notesOverflow <= 0 && _notesScroll <= 0)
        {
            return;
        }
        _notesScroll = Math.Clamp(_notesScroll - e.Delta / 120f * 42f, 0, _notesScroll + _notesOverflow);
        Invalidate();
    }

    // ------------------------------------------------------------------------------------------------
    // Actions

    private void OpenGameFolder()
    {
        Directory.CreateDirectory(_config.InstallPath);
        Process.Start(new ProcessStartInfo("explorer.exe", $"\"{_config.InstallPath}\"") { UseShellExecute = true });
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
        Cursor = Cursors.Default;
        RebuildNotes();
        Invalidate();
    }

    private static string ErrorText(Exception ex) => ex is PhraseException phrase ? phrase.Phrase.ToString() : ex.Message;

    /// <summary>Release notes are written in simple Markdown: "**Heading**", "- bullet", plain paragraphs.</summary>
    private static List<NoteLine> ParseNotes(string body)
    {
        var result = new List<NoteLine>();
        foreach (var raw in body.Replace("\r", "").Split('\n'))
        {
            var line = raw.Trim();
            if (line.Length == 0)
            {
                continue;
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

    private async Task CheckForUpdatesAsync()
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
            if (latest is null)
            {
                var asset = _config.AssetNameContains;
                SetNotes(() => (Lang.T("Noch keine Version", "No version yet"), "", Lang.T(
                    "Auf GitHub gibt es noch keine Version des Spiels.\n\nSobald ein Release mit einer Datei \"" + asset + "....zip\" veröffentlicht ist, kannst du es hier installieren.",
                    "There is no version of the game on GitHub yet.\n\nAs soon as a release with a file \"" + asset + "....zip\" is published, you can install it here.")));
            }
            else
            {
                // Release notes are written once, in German; only the date follows the language
                SetNotes(() => (latest.Name, Lang.Date(latest.PublishedAt.LocalDateTime), latest.Notes));
            }
            _status = _lastCheck.State switch
            {
                UpdateState.NotInstalled => new("Das Spiel ist noch nicht installiert.", "The game isn't installed yet."),
                UpdateState.UpdateAvailable => new("Ein Update ist verfügbar.", "An update is available."),
                UpdateState.UpToDate => new("Das Spiel ist auf dem neuesten Stand.", "The game is up to date."),
                _ => new("Keine Version gefunden.", "No version found."),
            };
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
            if (_preview == "busy")
            {
                _busy = true;
                _installing = true;
                _fraction = 0.42;
                _status = new("Lade herunter: 236.0 MB / 563.0 MB", "Downloading: 236.0 MB / 563.0 MB");
            }
        }
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
        SetBusy(true, new("Starte Download ...", "Starting download ..."));
        _cts = new CancellationTokenSource();
        var progress = new Progress<(Phrase Status, double? Fraction)>(p =>
        {
            _status = p.Status;
            _fraction = p.Fraction;
            Invalidate();
        });
        try
        {
            await _updater.InstallAsync(release, progress, _cts.Token);
            _lastCheck = new UpdateCheckResult(UpdateState.UpToDate, release, release.Tag);
            _status = new($"{release.Tag} ist installiert. Viel Spaß – und trau niemandem.", $"{release.Tag} is installed. Have fun – and trust no one.");
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
    }

    private void Play()
    {
        var exe = _updater.FindGameExecutable();
        if (exe is null)
        {
            _status = new("Das Spiel ist noch nicht installiert.", "The game isn't installed yet.");
            Invalidate();
            return;
        }
        Process.Start(new ProcessStartInfo(exe) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(exe)! });
        WindowState = FormWindowState.Minimized;
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
        Invalidate();
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_installing && _cts is not null)
        {
            var answer = MessageBox.Show(this, Lang.T("Ein Update läuft noch. Abbrechen und schließen?", "An update is still running. Cancel it and close?"), Text, MessageBoxButtons.YesNo);
            if (answer != DialogResult.Yes)
            {
                e.Cancel = true;
                return;
            }
            _cts.Cancel();
        }
        _timer.Stop();
        _updater.Dispose();
    }
}
