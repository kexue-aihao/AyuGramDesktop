# Windows 自构建与发布

工作流：`.github/workflows/windows-release.yml`，支持 Windows x64 和 Windows x86。
使用可调度的 `windows-2022`、MSVC 14.44、Windows SDK 10.0.26100.0 和 Python 3.10，
通过仓库现有的 `prepare/win.bat` 准备两种架构的依赖，再编译 Release；依赖准备阶段将 OpenSSL、libvpx 和 Qt 的并行度限制为 1，避免标准 runner 上共享 PDB 或 MSYS2 Make 的并行编译挂起。
工具、各架构的库和 Qt 分开缓存；首次构建需要较长时间。
依赖脚本使用 MSYS2 的 `diffutils`，并固定 NASM 3.01 以兼容仓库当前的 libvpx。

## 发布 Ayugram Desktop Base 20261018

将包含所需代码的提交推送到仓库，然后推送日期标签：

```powershell
git push origin dev
git tag -a base-20261018 -m "Ayugram Desktop Base 20261018"
git push origin base-20261018
```

推送 `base-YYYYMMDD` 标签会自动构建并发布，Release 名称为 `Ayugram Desktop Base YYYYMMDD`。
构建不修改 `Telegram/build/version` 中的上游数字版本，该版本仍供 Telegram 协议和 Windows 资源使用。

也可以在 Actions 中手动运行 **Windows self-build release**：

- `release_tag`：默认 `base-20261018`。
- `publish`：默认关闭，仅上传 Actions 构件；开启时，对应标签必须已经存在并指向所选提交。
- 重新运行失败的任务可以续传草稿 Release；已公开的 Release 不会被覆盖。

## API 配置

默认使用仓库 `docs/api_credentials.md` 和 `docs/building-win.md` 已记录的 API 配置。
如需自有配置，在仓库 Settings → Secrets and variables → Actions 中同时设置
`TDESKTOP_API_ID` 和 `TDESKTOP_API_HASH` 两个 Secrets。
无需 Canary、代码签名或更新签名服务的 Secrets。

## 下载与使用

两种架构均成功后，工作流校验 ZIP 和 SHA-256，再公开 GitHub Release，附件为：

- `Ayugram-Desktop-Base-20261018-Windows-x64.zip`
- `Ayugram-Desktop-Base-20261018-Windows-x86.zip`
- `SHA256SUMS.txt`

解压后运行 `AyuGram.exe`。包内包含匹配架构的 Direct3D 编译器、许可证和 `BUILD.txt` 源码提交信息。
`TelegramForcePortable` 目录用于保存便携版账号数据，移动或清理程序目录前请备份该目录。
下载加速设置位于 AyuGram → General → Downloads → Download Acceleration。

自构建产物未做代码签名，自动更新关闭。后续更新请从本仓库 Releases 下载。

```powershell
Get-FileHash .\Ayugram-Desktop-Base-20261018-Windows-x64.zip -Algorithm SHA256
Get-FileHash .\Ayugram-Desktop-Base-20261018-Windows-x86.zip -Algorithm SHA256
```

将输出与 `SHA256SUMS.txt` 对应行比较即可验证文件完整性。
