## NaturalVoiceSAPIAdapter v0.3.0

This fork adds Amazon Polly and ElevenLabs as SAPI 5 online voice providers.

### Installation

This release is an **overlay package**, not a standalone distribution.

1. Download and extract the original project's [v0.2.9 release](https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/releases/tag/v0.2.9) to its final local folder.
2. Extract this fork's v0.3.0 archive into the same folder and allow it to replace files. Keep the other files from the original release.
3. Run `Installer.exe` from the merged folder and install the required x86/x64 components as administrator.

### New providers

- **Amazon Polly** — configure an AWS access key, secret key, region, and a current engine name. The engine is a required text field; see the [Amazon Polly SynthesizeSpeech API documentation](https://docs.aws.amazon.com/polly/latest/APIReference/API_SynthesizeSpeech.html).
- **ElevenLabs** — configure an API key and a current Model ID. The Model ID is a required text field; see the [ElevenLabs models documentation](https://elevenlabs.io/docs/overview/models).

Model and engine identifiers are no longer hardcoded or assigned a default value, so newly released provider identifiers can be used without waiting for an adapter update.

Polly and ElevenLabs return audio only: SAPI word, sentence and bookmark events are unavailable. ElevenLabs receives plain text, so SAPI SSML markup is not retained.

---

## NaturalVoiceSAPIAdapter v0.3.0

本 fork 新增了 Amazon Polly 和 ElevenLabs 两个 SAPI 5 在线语音服务商。

### 安装

本发行包是**覆盖包**，不是独立发行版。

1. 下载并将原项目的 [v0.2.9 发行版](https://github.com/gexgd0419/NaturalVoiceSAPIAdapter/releases/tag/v0.2.9)解压到最终的本地文件夹。
2. 将本 fork 的 v0.3.0 发行包解压到同一文件夹，并允许覆盖文件。保留原项目发行包中的其他文件。
3. 从合并后的文件夹运行 `Installer.exe`，以管理员身份安装需要的 x86/x64 组件。

### 新服务商

- **Amazon Polly** — 配置 AWS access key、secret key、region 和当前 engine 名称。engine 是必填文本字段；请参阅 [Amazon Polly SynthesizeSpeech API 文档](https://docs.aws.amazon.com/polly/latest/APIReference/API_SynthesizeSpeech.html)。
- **ElevenLabs** — 配置 API key 和当前 Model ID。Model ID 是必填文本字段；请参阅 [ElevenLabs 模型文档](https://elevenlabs.io/docs/overview/models)。

不再硬编码或默认指定 model 和 engine，因此服务商发布新的标识符后，无需等待适配器更新即可使用。

Polly 和 ElevenLabs 仅返回音频，因此不提供 SAPI 的词、句和书签事件。ElevenLabs 接收纯文本，不保留 SAPI SSML 标记。
