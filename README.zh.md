# NaturalVoiceSAPIAdapter

[For the English version, click here](README.md)

连接 [Azure AI 语音服务][3]，使第三方程序也能使用微软[自然语音][2]的 [SAPI 5 TTS 引擎][1]。支持如下自然语音：

- Windows 11 中的讲述人自然语音
- Microsoft Edge 中“大声朗读”功能的在线自然语音
- 来自 Azure AI 语音服务的在线自然语音，只要你有对应的 key

任何支持 SAPI 5 语音的程序都可以借助此引擎使用上述的自然语音。

更多技术相关的信息可以参阅 [wiki 页面][4]。

## 系统要求

最低的测试可用的系统版本：Windows XP SP3，Windows XP Professional x64 Edition SP2

最低的支持本地讲述人语音的系统版本：Windows 7 RTM，x86 32/64位

最低的支持通过微软商店安装讲述人语音的系统版本：Windows 10 17763

### 如何在 Windows 11 上安装讲述人自然语音？

转到 系统设置 > **辅助功能** > **讲述人**，点击**添加自然语音**右侧的**添加**按钮。

如果你的系统版本不够新，没有这个选项，可以看看下面的方法。

### 我用的 Windows XP/Vista/7/8/10 系统，可以使用 Windows 11 系统的讲述人自然语音吗？

**Windows XP/Vista**: 很遗憾，这些平台并不支持本地讲述人自然语音。不过 Edge 和 Azure 的在线语音依然能用。

**Windows 10 (版本 17763 及以上)**: 可以使用[这里的微软商店的链接][5]下载安装 Windows 11 系统的讲述人自然语音。

**Windows 7/8/10 (版本 17763 之前)**，或者如果不能使用微软商店: 
1. 在[这里][5]复制某个语音的微软商店链接。
2. 使用 [store.rg-adguard.net](https://store.rg-adguard.net/) 获取链接以下载该语音的 **MSIX 文件**。
3. 准备一个文件夹用于存放语音文件夹，确保其路径内没有非 ASCII 字符（如汉字和中文符号）。
4. 将下载的 MSIX 文件作为 ZIP 压缩包解压至其子文件夹内。可以在相同的父文件夹下放置多个语音的子文件夹，确保子文件夹的名称内没有非 ASCII 字符。
5. 在安装程序中，将父文件夹设为“本地语音路径”。
6. 之后不要在此父文件夹下存放语音文件夹以外的内容，以免语音加载失败。

Windows 10 系统的讲述人并不支持自然语音，但是支持 SAPI 5 语音，所以可以借助本引擎间接让 Windows 10 系统的讲述人也支持自然语音。

### 之后的 Windows 版本还能用吗？

本引擎使用了从系统文件中提取的解密密钥来使用语音，这并不是官方认可的行为。

目前微软并没有允许第三方程序使用讲述人语音和 Edge 语音。因此，本引擎随时可能会在某次系统更新后停止工作。

## 安装

1. 从 [Releases][6] 栏下载 zip 文件。
2. 解压至一个文件夹。安装完成后，不要再移动、重命名或删除这些文件。若需要移动或删除文件，应先卸载。
3. 运行 `Installer.exe`。
4. 界面会在“安装状态”分区显示 32 位和 64 位版本是否已经安装。
    - 32 位版本用于 32 位程序，64 位版本用于 64 位程序。
    - 64 位系统中，若希望所有程序（包括 32 位和 64 位程序）都能使用语音，则两个版本都要安装。
    - 32 位系统中，“64 位”一行不会显示。
5. 单击安装/卸载。需要管理员权限。
6. 可以选择需要使用的语音类型。默认情况下，本地讲述人语音（若支持）以及 Microsoft Edge 大声朗读功能的在线语音处于启用状态。
    - 在线语音要求互联网连接，且可能更慢、更不稳定。如果只需要使用本地的讲述人语音，可以取消勾选“启用 Microsoft Edge 在线语音”和“启用 Azure 在线语音”。
    - 由于在线语音数量众多，默认只包含符合你的偏好语言的语音和英语(美国)的语音，以免语音列表出现过多项目。单击“更改...”按钮以更改包含的语言。
    - Azure 语音要求提供订阅密钥 (API key) 及其区域。可以访问 [Azure 门户](https://portal.azure.com/)，转到你的语音服务资源，之后转到 **资源管理** > **密钥和终结点**，复制粘贴密钥和区域。
7. 关闭安装程序窗口以应用更改。若之后想更改设置，可以再次打开安装程序。更改设置无需重新安装，无需管理员权限。

![中文安装程序界面](https://github.com/user-attachments/assets/22fe9b09-555f-4878-8a80-7ad3ae92fb60)


也可以用 `regsvr32` 手动注册 DLL 文件。

对于高级用户，这里有本程序的[可配置的注册表值][8]的列表。

## 测试

可以使用 `x86` 和 `x64` 文件夹中的 `TtsApplication.exe` 来测试本引擎。

这个程序修改自 [Windows-classic-samples 中的 TtsApplication][7]，添加了中文翻译和更详细的语素口型事件信息。

或者，可以转到 控制面板 > 语音 (Windows XP)，或 控制面板 > 语音识别 > 文本到语音转换 (Windows Vista 及以后)。

## 使用的库
- Microsoft.CognitiveServices.Speech.Extension.Embedded.TTS
- [websocketpp](https://github.com/zaphoyd/websocketpp)
- ASIO (独立版本)
- OpenSSL
- [nlohmann/json](https://github.com/nlohmann/json)
- [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks) (用于实现 Windows XP 兼容)
- [spdlog](https://github.com/gabime/spdlog)

[1]: https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ms717037(v=vs.85)
[2]: https://speech.microsoft.com/portal/voicegallery
[3]: https://learn.microsoft.com/azure/ai-services/speech-service/
[4]: ../../wiki
[5]: ../../wiki/讲述人自然语音下载链接
[6]: ../../releases
[7]: https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/winui/speech/ttsapplication
[8]: ../../wiki/Configurable-registry-values
