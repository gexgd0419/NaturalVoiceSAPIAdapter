# NaturalVoiceSAPIAdapter

[查看中文文档请点击这里](README.zh.md)

An [SAPI 5 text-to-speech (TTS) engine][1] that can utilize the [natural/neural voices][2] provided by the [Azure AI Speech Service][3], including:

- Natural voices for Narrator on Windows 11
- Online natural voices from Microsoft Edge's Read Aloud feature
- Online natural voices from the Azure AI Speech Service, if you have a proper subscription key
- Online voices from Amazon Polly, if you provide AWS credentials
- Online voices from ElevenLabs, if you provide an API key

Any program that supports SAPI 5 voices can use those natural voices via this TTS engine.

See the [wiki pages][4] for some more technical information.

This repository's releases are fork overlays: they keep the original providers and add Amazon Polly and ElevenLabs support. See [Installing a release from this fork](#installing-a-release-from-this-fork) for the required installation procedure.

Online providers send the text being spoken to their respective services. They require an Internet connection and an account with the selected provider, and may incur provider charges.

## Online provider setup

### Amazon Polly

1. In `Installer.exe`, enable **Amazon Polly online voices** and select **Set Polly keys...**.
2. Enter an AWS access key, secret key and region, then enter the current engine name from the [Amazon Polly SynthesizeSpeech API documentation](https://docs.aws.amazon.com/polly/latest/APIReference/API_SynthesizeSpeech.html). All four values are required; no engine is preselected.
3. Close the Installer to save the settings, then reopen the voice list in the target application.

Engine availability varies by AWS region, account and voice. Polly returns audio only, so word, sentence and bookmark events are not available.

### ElevenLabs

1. In `Installer.exe`, enable **ElevenLabs online voices** and select **Set ElevenLabs key...**.
2. Enter the API key and the current Model ID from the [ElevenLabs models documentation](https://elevenlabs.io/docs/overview/models). Both values are required; no model is preselected.
3. Close the Installer to save the settings, then reopen the voice list in the target application.

ElevenLabs synthesis is plain-text based in this adapter. SAPI SSML markup, bookmarks, and word/sentence events are not preserved for this provider.

Keep API keys and AWS secrets private. They are stored in the current user's adapter settings; do not share exported settings or enable trace logging when the text or credentials are sensitive.

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

### Installing a release from this fork

Fork releases are **overlay packages**, not standalone distributions. They contain only files built or changed in this fork; retain the other files from the original release archive.

1. Check the fork release notes and download the matching release of the [original project][9] named there.
2. Extract the original release to its final local folder. Do not use a network location.
3. Extract this fork's release into the **same** folder and allow it to replace existing files. Do not delete the files left from the original release.
4. Run `Installer.exe` from the merged folder.

To update an existing installation, copy the overlay files into that existing release folder and run `Installer.exe` again. Do not move, rename or delete the folder after installation; if it must be moved or deleted, uninstall first.

### Install and configure

5. It will tell you if the 32-bit version and the 64-bit version have been installed, in the "Installation Status" section.
    - The 32-bit version works with 32-bit programs, and the 64-bit version works with 64-bit programs.
    - On 64-bit systems, to make this work with every program (32-bit and 64-bit), you need to install both of them.
    - On 32-bit systems, the "64-bit" row will not be shown.
6. Click Install/Uninstall. Administrator's permission is required.
7. Choose what kinds of voices you want to use. By default, local Narrator voices (if supported) and Microsoft Edge Read Aloud online voices are enabled.
    - Online voices require Internet access, and they can be slower and less stable. If you only want to use the local Narrator voices, uncheck the online providers you do not need.
    - As there are many online voices, by default, only those in your preferred languages and in English (US) are included, to avoid cluttering the voice selection list. Click "Change..." to change what languages are included.
    - Azure voices require a subscription key (API key) and its region. Click "Set Azure key" to enter your key. You can visit [Azure Portal](https://portal.azure.com/), go to your speech service resource, then go to **Resource Management** > **Keys and Endpoint** to copy & paste the key and the region.
    - Amazon Polly requires an AWS access key, secret key, region and current engine name. Click "Set Polly keys..." to enter all four values.
    - ElevenLabs requires an API key and current Model ID. Click "Set ElevenLabs key..." to enter both values.
8. Close the Installer window to apply the changes. You can open the Installer again when you want to change something, and changing the settings doesn't require reinstallation or administrator's permission.

![Installer UI in English](https://github.com/user-attachments/assets/422264b8-a2ef-4ab7-96e9-4017dd88ca13)


Or, you can use an architecture-matching `regsvr32` to register the DLL files manually.

For advanced users, here's a list of this program's [configurable registry values][8].

## Testing

You can use the `TtsApplication.exe` in folders `x86` and `x64` to test the engine.

It's a modified version of the [TtsApplication in Windows-classic-samples][7], which added Chinese translation, and more detailed information for phoneme/viseme events.

Or, you can go to Control Panel > Speech (Windows XP), or Control Panel > Speech Recognition > Text to Speech (Windows Vista and later).

## Building this fork

Use Visual Studio 2022 with the **Desktop development with C++** workload, the v143 toolset and a Windows SDK. Restore the NuGet packages, then build `NaturalVoiceSAPIAdapter.sln` in both `Release|x64` and `Release|x86`; the latter also builds the installer. The GitHub Actions workflow is the reference for producing release archives.

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
[9]: https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/releases
