# NaturalVoiceSAPIAdapter

An [SAPI 5 text-to-speech (TTS) engine][1] that can utilize the [natural/neural voices][2] provided by the [Azure AI Speech Service][3], including:

- Installable natural voices for Narrator on Windows 11
- Online natural voices from Microsoft Edge's Read Aloud feature
- Online natural voices from the Azure AI Speech Service, if you have a proper subscription key

Any program that supports SAPI 5 voices can use those natural voices via this TTS engine.

See the [wiki pages][4] for some more technical information.

## 简介

连接 [Azure AI 语音服务][3]，使第三方程序也能使用微软[自然语音][2]的 [SAPI 5 TTS 引擎][1]。支持如下自然语音：

- Windows 11 中的讲述人自然语音
- Microsoft Edge 中“大声朗读”功能的在线自然语音
- 来自 Azure AI 语音服务的在线自然语音，只要你有对应的 key

任何支持 SAPI 5 语音的程序都可以借助此引擎使用上述的自然语音。

更多技术相关的信息可以参阅 [wiki 页面][4]。

## System Requirements

Windows XP SP3, or later versions of Windows. x86 32/64-bit.

**I'm using Windows 10. Can I use the Narrator natural voices on Windows 11?**

Yes, as long as your Windows 10 build number is 17763 or above (version 1809). You can choose and install Windows 11 Narrator voices [here][5].

Windows 10's Narrator doesn't support natural voices directly, but it does support SAPI 5 voices. So you can make Windows 11 Narrator voices work on Windows 10 via this engine.

**Does it really work on Windows XP?**

Yes, although I only tested it on a virtual machine.

On Windows XP, Windows 11 Narrator voices don't work. But online Microsoft Edge voices and Azure voices work.

**Will it work on future versions of Windows?**

This engine uses some encryption keys extracted from system files to use the voices, so it's more like a hack than a proper solution.

As for now, Microsoft hasn't yet allowed third-party apps to use the Narrator/Edge voices, and this can stop working at any time, for example, after a system update.

## 系统要求

Windows XP SP3 或更高版本。x86 32/64 位。

**我用的 Windows 10 系统，可以使用 Windows 11 系统的讲述人自然语音吗？**

可以，只要系统版本为 1809 或更新版本。可以转到[这里][5]下载安装 Windows 11 系统的讲述人自然语音。

Windows 10 系统的讲述人并不支持自然语音，但是支持 SAPI 5 语音，所以可以借助本引擎间接让 Windows 10 系统的讲述人也支持自然语音。

**Windows XP 上真的能用？**

可以，虽然我只在虚拟机上测试过。

XP 上讲述人自然语音不能用，但是 Edge 和 Azure 的在线语音依然能用。

**之后的 Windows 版本还能用吗？**

本引擎使用了从系统文件中提取的解密密钥来使用语音，这并不是官方认可的行为。

目前微软并没有允许第三方程序使用讲述人语音和 Edge 语音。因此，本引擎随时可能会在某次系统更新后停止工作。

## Installation

1. Download the zip file from the [Releases][6] section.
2. Extract the files in a folder. Make sure not to move or rename the files after installation.
3. Run `Installer.exe`.
4. It will tell you if the 32-bit version and the 64-bit version have been installed.
    - The 32-bit version works with 32-bit programs, and the 64-bit version works with 64-bit programs.
    - On 64-bit systems, to make this work with every program (32-bit and 64-bit), you need to install both of them.
    - On 32-bit systems, the "64-bit" row will not be shown.
5. Click Install/Uninstall. Administrator's permission is required.
6. You can choose if you want to use the installed Narrator voices, and the Microsoft Edge Read Aloud online voices.
    - Narrator voices requires Windows 10, 17763 or later.
    - By default, only Edge voices in your preferred languages and in English (US) are included, to avoid cluttering the voice selection list. Choose "All support languages" if you want to use all Edge voices.
    - Online voices require Internet access, and they can be slower and less stable. If you only want to use the local Narrator voices, you can uncheck "Include Microsoft Edge online voices".

![Installer UI in English](https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/assets/55008943/20e29cd7-9951-4cc2-93e9-a0dd5dd058da)

Or, you can use `regsvr32` to register the DLL files manually.

## 安装

1. 从 [Releases][6] 栏下载 zip 文件。
2. 解压至一个文件夹。安装完成后，不要再移动或重命名这些文件。
3. 运行 `Installer.exe`。
4. 界面会显示 32 位和 64 位版本是否已经安装。
    - 32 位版本用于 32 位程序，64 位版本用于 64 位程序。
    - 64 位系统中，若希望所有程序（包括 32 位和 64 位程序）都能使用语音，则两个版本都要安装。
    - 32 位系统中，“64 位”一行不会显示。
5. 单击安装/卸载。需要管理员权限。
6. 可以选择是否使用已安装的讲述人语音，以及 Microsoft Edge 大声朗读功能的在线语音。
    - 讲述人语音需要 Windows 10 17763 或更新版本。
    - Edge 语音默认只包含符合你的偏好语言的语音和英语(美国)的语音，以免语音列表出现过多项目。若希望使用所有的 Edge 语音，选择“所有支持的语言”。
    - 在线语音要求互联网连接，且可能更慢、更不稳定。如果只需要使用本地的讲述人语音，可以取消勾选“包括 Microsoft Edge 在线语音”。

![中文安装程序界面](https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/assets/55008943/fd583321-5f41-4652-ae8b-5623ace7b7ed)

也可以用 `regsvr32` 手动注册 DLL 文件。

## Testing

You can use the `TtsApplication.exe` in folders `x86` and `x64` to test the engine.

It's a modified version of the [TtsApplication in Windows-classic-samples][7], which added Chinese translation, and more detailed information for phoneme/viseme events.

Or, you can go to Control Panel > Speech (Windows XP), or Control Panel > Speech Recognition > Text to Speech (Windows Vista and later).

## 测试

可以使用 `x86` 和 `x64` 文件夹中的 `TtsApplication.exe` 来测试本引擎。

这个程序修改自 [Windows-classic-samples 中的 TtsApplication][7]，添加了中文翻译和更详细的语素口型事件信息。

或者，可以转到 控制面板 > 语音 (Windows XP)，或 控制面板 > 语音识别 > 文本到语音转换 (Windows Vista 及以后)。

## Libraries used
- Microsoft.CognitiveServices.Speech.Extension.Embedded.TTS
- [websocketpp](https://github.com/zaphoyd/websocketpp)
- ASIO (standalone version)
- OpenSSL
- [nlohmann/json](https://github.com/nlohmann/json)
- [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks) (for Windows XP compatibility)

[1]: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717037(v=vs.85)
[2]: https://speech.microsoft.com/portal/voicegallery
[3]: https://learn.microsoft.com/azure/ai-services/speech-service/
[4]: https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/wiki
[5]: https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/wiki/Narrator-natural-voice-download-links
[6]: https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/releases
[7]: https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/speech/ttsapplication
