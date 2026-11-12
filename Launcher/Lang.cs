using System.Globalization;

namespace HowCouldItBeYou.Launcher;

/// <summary>
/// Launcher language, switched with the DE | EN toggle in the window's top bar.
/// The choice is saved in language.txt next to the launcher; without it the system language decides
/// (German for a German Windows or Linux desktop, English for everything else).
/// </summary>
public static class Lang
{
    private const string FileName = "language.txt";

    private static readonly string[] Months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

    public static bool English { get; private set; } = Load();

    /// <summary>The German or the English text, whichever language is active.</summary>
    public static string T(string de, string en) => English ? en : de;

    /// <summary>
    /// Dates as "11.11.2026" in German and "11 Nov 2026" in English.
    /// Built by hand: the Linux launcher has no culture data (InvariantGlobalization).
    /// </summary>
    public static string Date(DateTime date) =>
        English ? $"{date.Day} {Months[date.Month - 1]} {date.Year}" : date.ToString("dd.MM.yyyy", CultureInfo.InvariantCulture);

    public static void Set(bool english)
    {
        English = english;
        try
        {
            File.WriteAllText(Path.Combine(LauncherConfig.BaseDirectory, FileName), english ? "en" : "de");
        }
        catch (Exception)
        {
            // read-only folder: the choice just isn't remembered
        }
    }

    private static bool Load()
    {
        try
        {
            var path = Path.Combine(LauncherConfig.BaseDirectory, FileName);
            if (File.Exists(path))
            {
                return File.ReadAllText(path).Trim().StartsWith("en", StringComparison.OrdinalIgnoreCase);
            }
        }
        catch (Exception)
        {
            // fall back to the system language
        }
        return SystemLanguage() != "de";
    }

    /// <summary>Two-letter code of the desktop language, e.g. "de".</summary>
    private static string SystemLanguage()
    {
        var culture = CultureInfo.CurrentUICulture.TwoLetterISOLanguageName;
        if (!string.IsNullOrEmpty(culture) && culture != "iv")
        {
            return culture;
        }
        // Linux without culture data: the locale variables say it ("de_DE.UTF-8"; LANGUAGE may list several: "de:en")
        foreach (var name in new[] { "LANGUAGE", "LC_ALL", "LC_MESSAGES", "LANG" })
        {
            var value = Environment.GetEnvironmentVariable(name);
            if (!string.IsNullOrWhiteSpace(value) && value != "C" && value != "POSIX" && value.Length >= 2)
            {
                return value[..2].ToLowerInvariant();
            }
        }
        return "en";
    }
}

/// <summary>A text in German and English. Shows the active language, so it follows a switch while on screen.</summary>
public readonly record struct Phrase(string De, string En)
{
    public override string ToString() => Lang.T(De, En);
}

/// <summary>An error whose message exists in both languages.</summary>
public sealed class PhraseException(Phrase phrase) : Exception(phrase.De)
{
    public Phrase Phrase { get; } = phrase;
}
