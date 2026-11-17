using System.IO;
using System.IO.Pipes;
using System.Text;
using Microsoft.Win32;

namespace Winawdl.Shell;

/// <summary>"Send with AirDrop" on a file's right-click menu in Explorer. The entry
/// starts the app with the file's path; a copy that is already running takes the
/// path over a pipe, so every chosen file lands in the same hand.</summary>
public static class SendMenu
{
    private const string Verb = @"Software\Classes\*\shell\winawdl";
    private const string Label = "Enviar via AirDrop";
    private const int PassOnWaitMs = 3000;

    /// <summary>The pipe belongs to this user's session: another user's copy has its own.</summary>
    private static string PipeName => $"winawdl-files-{Environment.UserName}-{System.Diagnostics.Process.GetCurrentProcess().SessionId}";

    /// <summary>The arguments that name files that exist, as full paths.</summary>
    public static List<string> FilesAmong(string[] args) =>
        [.. args.TakeWhile(argument => !argument.StartsWith("--")).Where(File.Exists).Select(Path.GetFullPath)];

    /// <summary>Writes the menu entry for this user, pointing at this program. Done at
    /// every start, so a copy that moved is found again.</summary>
    public static void Register()
    {
        if (Environment.ProcessPath is not { } program) return;
        using var verb = Registry.CurrentUser.CreateSubKey(Verb);
        verb.SetValue("MUIVerb", Label);
        verb.SetValue("Icon", $"\"{program}\",0");
        // One copy of the app per chosen file; each hands its file to the first.
        verb.SetValue("MultiSelectModel", "Player");
        using var command = verb.CreateSubKey("command");
        command.SetValue("", $"\"{program}\" \"%1\"");
    }

    public static void Unregister() => Registry.CurrentUser.DeleteSubKeyTree(Verb, throwOnMissingSubKey: false);

    /// <summary>Hands file paths to the copy that is running. Nothing is said when it
    /// does not take them: it is shown either way, and the files can be dropped on it.</summary>
    public static void PassOn(IReadOnlyCollection<string> files)
    {
        if (files.Count == 0) return;
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.Out);
            pipe.Connect(PassOnWaitMs);
            using var writer = new StreamWriter(pipe, new UTF8Encoding(false));
            foreach (var file in files) writer.WriteLine(file);
        }
        catch (Exception error) when (error is IOException or TimeoutException or UnauthorizedAccessException)
        {
        }
    }

    /// <summary>Takes file paths from later copies for as long as the app runs.
    /// `take` is called off the window's thread.</summary>
    public static void Listen(Action<List<string>> take)
    {
        var listening = new Thread(() =>
        {
            for (;;)
            {
                try
                {
                    using var pipe = new NamedPipeServerStream(PipeName, PipeDirection.In, 1,
                                                               PipeTransmissionMode.Byte,
                                                               PipeOptions.CurrentUserOnly);
                    pipe.WaitForConnection();
                    using var reader = new StreamReader(pipe, Encoding.UTF8);
                    var files = new List<string>();
                    while (reader.ReadLine() is { } line)
                        if (File.Exists(line)) files.Add(line);
                    if (files.Count > 0) take(files);
                }
                catch (IOException)
                {
                    Thread.Sleep(200);
                }
            }
        })
        {
            IsBackground = true, Name = "files",
        };
        listening.Start();
    }
}
