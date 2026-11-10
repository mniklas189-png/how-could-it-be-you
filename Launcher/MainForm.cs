using System.Diagnostics;

namespace HowCouldItBeYou.Launcher;

/// <summary>Launcher window: shows installed/latest version, an Update button and a Play button.</summary>
public sealed class MainForm : Form
{
    private static readonly Color Background = Color.FromArgb(14, 16, 20);
    private static readonly Color Panel = Color.FromArgb(24, 27, 33);
    private static readonly Color TextColor = Color.FromArgb(220, 224, 230);
    private static readonly Color Muted = Color.FromArgb(130, 138, 150);
    private static readonly Color Accent = Color.FromArgb(200, 40, 40);
    private static readonly Color AccentGreen = Color.FromArgb(40, 150, 80);

    private readonly LauncherConfig _config;
    private readonly GitHubUpdater _updater;

    private readonly Label _title = new();
    private readonly Label _subtitle = new();
    private readonly Label _versionLabel = new();
    private readonly Label _statusLabel = new();
    private readonly TextBox _notes = new();
    private readonly ProgressBar _progress = new();
    private readonly Button _updateButton = new();
    private readonly Button _playButton = new();
    private readonly LinkLabel _folderLink = new();

    private UpdateCheckResult? _lastCheck;
    private CancellationTokenSource? _cts;
    private bool _busy;

    public MainForm()
    {
        _config = LauncherConfig.Load();
        _updater = new GitHubUpdater(_config);

        Text = "How Could It Be You – Launcher";
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(720, 440);
        MinimumSize = new Size(600, 400);
        BackColor = Background;
        ForeColor = TextColor;
        Font = new Font("Segoe UI", 10f);
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;

        BuildLayout();
        Shown += async (_, _) => await CheckForUpdatesAsync();
        FormClosing += OnFormClosing;
    }

    private void BuildLayout()
    {
        _title.Text = "HOW COULD IT BE YOU";
        _title.Font = new Font("Segoe UI", 24f, FontStyle.Bold);
        _title.ForeColor = TextColor;
        _title.AutoSize = true;
        _title.Location = new Point(28, 24);

        _subtitle.Text = "Es ist einer von euch.";
        _subtitle.Font = new Font("Segoe UI", 11f, FontStyle.Italic);
        _subtitle.ForeColor = Accent;
        _subtitle.AutoSize = true;
        _subtitle.Location = new Point(32, 72);

        _versionLabel.ForeColor = Muted;
        _versionLabel.AutoSize = true;
        _versionLabel.Location = new Point(32, 108);

        _notes.Multiline = true;
        _notes.ReadOnly = true;
        _notes.ScrollBars = ScrollBars.Vertical;
        _notes.BorderStyle = BorderStyle.None;
        _notes.BackColor = Panel;
        _notes.ForeColor = TextColor;
        _notes.Location = new Point(32, 140);
        _notes.Size = new Size(656, 160);
        _notes.Text = "Suche nach Updates ...";

        _statusLabel.ForeColor = Muted;
        _statusLabel.AutoSize = false;
        _statusLabel.Location = new Point(32, 312);
        _statusLabel.Size = new Size(656, 22);

        _progress.Location = new Point(32, 338);
        _progress.Size = new Size(656, 8);
        _progress.Style = ProgressBarStyle.Continuous;

        StyleButton(_updateButton, "Nach Updates suchen", Panel);
        _updateButton.Location = new Point(32, 366);
        _updateButton.Size = new Size(260, 46);
        _updateButton.Click += async (_, _) => await OnUpdateClickedAsync();

        StyleButton(_playButton, "SPIELEN", Accent);
        _playButton.Font = new Font("Segoe UI", 12f, FontStyle.Bold);
        _playButton.Location = new Point(468, 366);
        _playButton.Size = new Size(220, 46);
        _playButton.Click += (_, _) => Play();

        _folderLink.Text = "Spielordner öffnen";
        _folderLink.LinkColor = Muted;
        _folderLink.ActiveLinkColor = TextColor;
        _folderLink.AutoSize = true;
        _folderLink.Location = new Point(310, 380);
        _folderLink.LinkClicked += (_, _) =>
        {
            Directory.CreateDirectory(_config.InstallPath);
            Process.Start(new ProcessStartInfo("explorer.exe", $"\"{_config.InstallPath}\"") { UseShellExecute = true });
        };

        Controls.AddRange([_title, _subtitle, _versionLabel, _notes, _statusLabel, _progress, _updateButton, _folderLink, _playButton]);
        RefreshState();
    }

    private static void StyleButton(Button button, string text, Color back)
    {
        button.Text = text;
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0;
        button.BackColor = back;
        button.ForeColor = Color.White;
        button.Cursor = Cursors.Hand;
    }

    private async Task OnUpdateClickedAsync()
    {
        if (_busy)
        {
            _cts?.Cancel();
            return;
        }
        if (_lastCheck is { State: UpdateState.UpdateAvailable or UpdateState.NotInstalled, Latest: not null })
        {
            await InstallAsync(_lastCheck.Latest);
        }
        else
        {
            await CheckForUpdatesAsync();
        }
    }

    private async Task CheckForUpdatesAsync()
    {
        SetBusy(true, "Suche nach Updates ...");
        try
        {
            _cts = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            _lastCheck = await _updater.CheckAsync(_cts.Token);
            var latest = _lastCheck.Latest;
            _notes.Text = latest is null
                ? "Auf GitHub gibt es noch keine Version des Spiels.\r\n\r\nSobald ein Release mit einer Datei \"" +
                  _config.AssetNameContains + "....zip\" veröffentlicht ist, kannst du es hier installieren."
                : $"{latest.Name}  ({latest.PublishedAt.LocalDateTime:dd.MM.yyyy})\r\n\r\n{latest.Notes}".Replace("\n", "\r\n").Replace("\r\r", "\r");
            _statusLabel.Text = _lastCheck.State switch
            {
                UpdateState.NotInstalled => $"Spiel nicht installiert. Download: {GitHubUpdater.FormatSize(latest!.AssetSize)}",
                UpdateState.UpdateAvailable => $"Update verfügbar: {_lastCheck.InstalledVersion} → {latest!.Tag} ({GitHubUpdater.FormatSize(latest.AssetSize)})",
                UpdateState.UpToDate => "Das Spiel ist auf dem neuesten Stand.",
                _ => "Keine Version gefunden.",
            };
        }
        catch (OperationCanceledException)
        {
            _statusLabel.Text = "Zeitüberschreitung bei der Update-Suche. Bist du online?";
        }
        catch (Exception ex)
        {
            _statusLabel.Text = "Update-Suche fehlgeschlagen.";
            _notes.Text = ex.Message;
        }
        finally
        {
            SetBusy(false, null);
        }
    }

    private async Task InstallAsync(ReleaseInfo release)
    {
        SetBusy(true, "Starte Download ...");
        _updateButton.Text = "Abbrechen";
        _updateButton.Enabled = true;
        _cts = new CancellationTokenSource();
        var progress = new Progress<(string Status, double? Fraction)>(p =>
        {
            _statusLabel.Text = p.Status;
            if (p.Fraction is { } f)
            {
                _progress.Style = ProgressBarStyle.Continuous;
                _progress.Value = (int)Math.Clamp(f * 1000, 0, 1000);
            }
            else
            {
                _progress.Style = ProgressBarStyle.Marquee;
            }
        });
        try
        {
            await _updater.InstallAsync(release, progress, _cts.Token);
            _lastCheck = new UpdateCheckResult(UpdateState.UpToDate, release, release.Tag);
        }
        catch (OperationCanceledException)
        {
            _statusLabel.Text = "Update abgebrochen. Die bisherige Version bleibt erhalten.";
            GitHubUpdater.CleanupLeftovers();
        }
        catch (Exception ex)
        {
            _statusLabel.Text = "Update fehlgeschlagen. Die bisherige Version bleibt erhalten.";
            _notes.Text = ex.Message;
            GitHubUpdater.CleanupLeftovers();
        }
        finally
        {
            SetBusy(false, null);
        }
    }

    private void Play()
    {
        var exe = _updater.FindGameExecutable();
        if (exe is null)
        {
            MessageBox.Show(this, "Das Spiel ist noch nicht installiert. Klicke zuerst auf \"Installieren\".", Text);
            return;
        }
        Process.Start(new ProcessStartInfo(exe) { UseShellExecute = true, WorkingDirectory = Path.GetDirectoryName(exe)! });
        WindowState = FormWindowState.Minimized;
    }

    private void SetBusy(bool busy, string? status)
    {
        _busy = busy;
        if (status is not null)
        {
            _statusLabel.Text = status;
        }
        if (!busy)
        {
            _progress.Style = ProgressBarStyle.Continuous;
            _progress.Value = 0;
            _cts?.Dispose();
            _cts = null;
        }
        else
        {
            _progress.Style = ProgressBarStyle.Marquee;
        }
        RefreshState();
    }

    private void RefreshState()
    {
        var installed = _updater.GetInstalledVersion();
        var latest = _lastCheck?.Latest?.Tag;
        _versionLabel.Text = $"Installiert: {installed ?? "–"}     Neueste: {latest ?? "?"}     Launcher {Application.ProductVersion.Split('+')[0]}";

        _playButton.Enabled = !_busy && _updater.FindGameExecutable() is not null;
        _playButton.BackColor = _playButton.Enabled ? Accent : Panel;

        if (_busy)
        {
            _updateButton.Enabled = _cts is not null && _updateButton.Text == "Abbrechen";
            return;
        }
        _updateButton.Enabled = true;
        (_updateButton.Text, _updateButton.BackColor) = _lastCheck?.State switch
        {
            UpdateState.UpdateAvailable => ("UPDATE", AccentGreen),
            UpdateState.NotInstalled => ("INSTALLIEREN", AccentGreen),
            _ => ("Nach Updates suchen", Panel),
        };
    }

    private void OnFormClosing(object? sender, FormClosingEventArgs e)
    {
        if (_busy && _cts is not null)
        {
            var answer = MessageBox.Show(this, "Ein Update läuft noch. Abbrechen und schließen?", Text, MessageBoxButtons.YesNo);
            if (answer != DialogResult.Yes)
            {
                e.Cancel = true;
                return;
            }
            _cts.Cancel();
        }
        _updater.Dispose();
    }
}
