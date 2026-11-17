using System.Net.Http;
using System.Text.Json;

namespace Winawdl.Shell;

/// <summary>Whether GitHub has a release newer than this copy. One request to GitHub's public
/// API a start, which carries nothing but the program's name; nothing is downloaded or
/// installed. A release is only pointed at: the person decides.</summary>
public static class Updates
{
    private const string Releases = "https://github.com/matheusbgodoi/WinDrop/releases";
    private const string Latest = "https://api.github.com/repos/matheusbgodoi/WinDrop/releases/latest";

    /// <summary>The newer version and its page, or null when there is none or GitHub cannot be asked.</summary>
    public static async Task<(Version Version, string Page)?> NewerThan(Version current)
    {
        try
        {
            using var http = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
            http.DefaultRequestHeaders.UserAgent.ParseAdd("WinDrop");
            using var answer = JsonDocument.Parse(await http.GetStringAsync(Latest));
            var tag = answer.RootElement.GetProperty("tag_name").GetString() ?? "";
            var page = answer.RootElement.GetProperty("html_url").GetString() ?? "";
            // Only a page of this project's releases is ever opened, whatever the answer says.
            if (Version.TryParse(tag.TrimStart('v'), out var latest) && latest > current
                && page.StartsWith(Releases + "/", StringComparison.Ordinal))
                return (latest, page);
        }
        catch (Exception error) when (error is HttpRequestException or TaskCanceledException or JsonException
                                          or KeyNotFoundException or InvalidOperationException)
        {
            // Offline, a private repository, an answer of another shape: no word on updates this time.
        }
        return null;
    }
}
