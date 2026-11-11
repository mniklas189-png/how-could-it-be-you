using System.Globalization;

namespace HowCouldItBeYou.Launcher;

/// <summary>
/// Launcher language, switched with the DE | EN toggle in the window's top bar.
/// The choice is saved in language.txt next to the .exe; without it the Windows display language decides
/// (German for German Windows, English for everything else).
/// </summary>
public static class Lang
{
    private const string FileName = "language.txt";

    public static bool English { get; private set; } = Load();

    /// <summary>The German or the English text, whichever language is active.</summary>
    public static string T(string de, string en) => English ? en : de;

    /// <summary>Dates as "11.11.2026" in German and "11 Nov 2026" in English.</summary>
    public static string Date(DateTime date) =>
        English ? date.ToString("d MMM yyyy", CultureInfo.GetCultureInfo("en-GB")) : date.ToString("dd.MM.yyyy", CultureInfo.InvariantCulture);

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
        return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName != "de";
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
