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
3. Run the `install.bat` file **as administrator**.
4. If you are using this on Windows XP, you can also merge the `PhoneConverters.reg` file to make the system support more TTS languages.
5. Run the `uninstall.bat` file as administrator when you want to uninstall it.

Or, you can use `regsvr32` to register the DLL files manually.

## 安装

1. 从 [Releases][6] 栏下载 zip 文件。
2. 解压至一个文件夹。安装完成后，不要再移动或重命名这些文件。
3. **以管理员身份**运行 `install.bat`。
4. 若在 Windows XP 上使用，可以合并 `PhoneConverters.reg` 以让系统支持更多语音语言。
5. 若要卸载，以管理员身份运行 `uninstall.bat`。

也可以用 `regsvr32` 手动注册 DLL 文件。

## Testing

You can use the `TtsApplication-x86.exe` and `TtsApplication-x64.exe` to test the engine.

It's a modified version of the [TtsApplication in Windows-classic-samples][7], which added Chinese translation, and more detailed information for phoneme/viseme events.

## 测试

可以使用 `TtsApplication-x86.exe` 和 `TtsApplication-x64.exe` 来测试本引擎。

这个程序修改自 [Windows-classic-samples 中的 TtsApplication][7]，添加了中文翻译和更详细的语素口型事件信息。

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