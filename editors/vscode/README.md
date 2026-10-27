# Oro Language Server

This client starts `oroc language-server` for files beneath the nearest
`oro.toml` (or legacy `oro.ini`). Each application and language family gets a
separate client. Ordinary projects keep their existing language providers.

Install Oro, Node.js 22+, and the backends you use on the editor's `PATH`:

| Language family                       | Default backend                             |
| ------------------------------------- | ------------------------------------------- |
| Android Kotlin                        | `kotlin-language-server` 1.3.13             |
| JavaScript / TypeScript               | `typescript-language-server --stdio`        |
| C / C++ / Objective-C / Objective-C++ | `clangd`                                    |
| Swift                                 | `sourcekit-lsp` from Swift 6.0+ / Xcode 16+ |

Windows C/C++ language-server support is deferred to a future release. On a
Windows workspace host, this client leaves C, C++, Objective-C and Objective-C++
files to other installed providers, even when `cpp` is selected in
`oro.languageServer.languages`. JavaScript/TypeScript support remains enabled.
For remote workspaces, the workspace host's OS determines this behavior.

Kotlin also requires JDK 21+, the selected Android SDK, Gradle and resolved
dependencies. Run `oroc tooling kotlin --prepare --variant devDebug` from the
application root once to populate dependencies before offline use. Kotlin startup
prepares SDK/resource classes automatically, including when application code has
errors. Other families can opt into preparation through their launch arguments.

Open the application directory and trust the workspace. Select additional launch
arguments in workspace settings, for example:

```json
{
  "oro.languageServer.arguments": {
    "kotlin": ["--variant", "devDebug", "--offline"]
  }
}
```

Set `oro.languageServer.executable` to the installed CLI when it is not on `PATH`.
On Windows, select the native `oroc.exe` executable. Put machine paths such as
`JAVA_HOME`, `ANDROID_HOME` and `GRADLE_HOME` in user settings under
`oro.languageServer.environment`. Select wrapped server commands and arguments
in `[language_server.<language>]` in `oro.toml`.

Disable overlapping providers for the language families Oro manages in your
workspace. Keep your independent C/C++ provider enabled on Windows.
Use `oro.languageServer.languages` to choose which families this extension owns.
After fixing a launch error, run **Oro: Restart Language Servers**. Model changes
normally refresh the backend while preserving unsaved buffers.

For development, run `npm ci --prefix editors/vscode` from the Oro repository and
launch VS Code with `--extensionDevelopmentPath` pointing to this directory.
`npm run check --prefix editors/vscode` checks the client against the published
VS Code and language-client interfaces without a VS Code installation. The client
runs in the workspace extension host, including remote workspaces, and requires
trusted file-based projects.
Package this directory with the VS Code extension packaging tool to create a
VSIX. See `docs/LANGUAGE_SERVER.md` in the Oro repository for model schemas,
Coc configuration, preparation, refresh and compatibility details.
