# NaturalVoiceSAPIAdapter

[查看中文文档请点击这里](README.zh.md)

An [SAPI 5 text-to-speech (TTS) engine][1] that can utilize the [natural/neural voices][2] provided by the [Azure AI Speech Service][3], including:

- Natural voices for Narrator on Windows 11
- Online natural voices from Microsoft Edge's Read Aloud feature
- Online natural voices from the Azure AI Speech Service, if you have a proper subscription key

Any program that supports SAPI 5 voices can use those natural voices via this TTS engine.

See the [wiki pages][4] for some more technical information.

## System Requirements

Minimum tested platform: Windows XP SP3, and Windows XP Professional x64 Edition SP2 (32-bit only).

Minimum platform that supports local Narrator voices: Windows 7 RTM, x86 32/64-bit.

Minimum platform that supports installing Narrator voices via Microsoft Store: Windows 10, build 17763.

### How can I install Narrator natural voices on Windows 11?

It's no longer recommended to install Narrator natural voices on Windows 11 if you want to use this program, because the latest version of those voices stopped working with this program. It's recommended to download and use [the last working version][5] of the voices instead.

If Narrator stops working when this program is installed, try uninstalling all Narrator voice packs as a temporary workaround.

### I'm using Windows XP/Vista/7/8/10. Can I use the Narrator natural voices from Windows 11?

**Windows XP/Vista**: Unfortunately local Narrator voices are not supported on those platforms. But online voices, including Edge and Azure voices, still work.

**Windows 10 (build 17763 or above)**: You can choose and install Windows 11 Narrator voices using [these links][5].

**Windows 7/8/10 (before build 17763)**:
1. Download the MSIX file of the voice from [here][5].
2. Prepare a folder to store the voice folders. Make sure its path contains no non-ASCII character.
3. Unzip the MSIX file (as if it were a ZIP file) to its sub folder. You can have multiple voice sub folders in the same parent folder. Make sure the sub folder's name contains no non-ASCII character.
4. Set the parent folder as "Local voice path" in the installer.
5. Do not put things other than voice sub folders inside this parent folder, or voice loading may fail.

Windows 10's Narrator doesn't support natural voices directly, but it does support SAPI 5 voices. So you can make Windows 11 Narrator voices work on Windows 10 via this engine.

### Will it work on future versions of Windows?

This engine uses some encryption keys extracted from system files to use the voices, so it's more like a hack than a proper solution.

As for now, Microsoft hasn't yet allowed third-party apps to use the Narrator/Edge voices, and this can stop working at any time, for example, after a system update.

## Installation

1. Download the zip file from the [Releases][6] section.
2. Extract the files in a folder. Make sure not to move, rename or delete the files after installation. If you want to move/delete the files, you should uninstall it first.
3. Run `Installer.exe`.
4. It will tell you if the 32-bit version and the 64-bit version have been installed, in the "Installation Status" section.
    - The 32-bit version works with 32-bit programs, and the 64-bit version works with 64-bit programs.
    - On 64-bit systems, to make this work with every program (32-bit and 64-bit), you need to install both of them.
    - On 32-bit systems, the "64-bit" row will not be shown.
5. Click Install/Uninstall. Administrator's permission is required.
6. Choose what kinds of voices you want to use. By default, local Narrator voices (if supported) and Microsoft Edge Read Aloud online voices are enabled.
    - Online voices require Internet access, and they can be slower and less stable. If you only want to use the local Narrator voices, you can uncheck "Enable Microsoft Edge online voices" and "Enable Azure online voices".
    - As there are many online voices, by default, only those in your preferred languages and in English (US) are included, to avoid cluttering the voice selection list. Click "Change..." to change what languages are included.
    - Azure voices require a subscription key (API key) and its region. Click "Set Azure key" to enter your key. You can visit [Azure Portal](https://portal.azure.com/), go to your speech service resource, then go to **Resource Management** > **Keys and Endpoint** to copy & paste the key and the region.
  7. Close the Installer window to apply the changes. You can open the Installer again when you want to change something, and changing the settings doesn't require reinstallation or administrator's permission.

![Installer UI in English](https://github.com/user-attachments/assets/422264b8-a2ef-4ab7-96e9-4017dd88ca13)


Or, you can use `regsvr32` to register the DLL files manually.

For advanced users, here's a list of this program's [configurable registry values][8].

## Testing

You can use the `TtsApplication.exe` in folders `x86` and `x64` to test the engine.

It's a modified version of the [TtsApplication in Windows-classic-samples][7], which added Chinese translation, and more detailed information for phoneme/viseme events.

Or, you can go to Control Panel > Speech (Windows XP), or Control Panel > Speech Recognition > Text to Speech (Windows Vista and later).

## Libraries used
- Microsoft.CognitiveServices.Speech.Extension.Embedded.TTS
- [websocketpp](https://github.com/zaphoyd/websocketpp)
- ASIO (standalone version)
- OpenSSL
- [nlohmann/json](https://github.com/nlohmann/json)
- [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks) (for Windows XP compatibility)
- [spdlog](https://github.com/gabime/spdlog)

[1]: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717037(v=vs.85)
[2]: https://speech.microsoft.com/portal/voicegallery
[3]: https://learn.microsoft.com/azure/ai-services/speech-service/
[4]: ../../wiki
[5]: ../../wiki/Narrator-natural-voice-download-links
[6]: ../../releases
[7]: https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/speech/ttsapplication
[8]: ../../wiki/Configurable-registry-values
