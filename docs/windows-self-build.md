# Windows 自构建与发布

工作流：`.github/workflows/windows-release.yml`，支持 Windows x64 和 Windows x86。
使用可调度的 `windows-2022`、MSVC 14.44、Windows SDK 10.0.26100.0 和 Python 3.10，
通过仓库现有的 `prepare/win.bat` 准备两种架构的依赖，再编译 Release；依赖准备阶段使用 OpenSSL 官方的 `nmake` 串行目标、关闭 OpenSSL 的强制同步 PDB 写入，禁用 libvpx 工程内部的多处理器编译，并将依赖并行度限制为 2，避免标准 runner 上共享 PDB 或 MSYS2 Make 的并行编译挂起。
工具、各架构的库和 Qt 分开缓存；首次构建需要较长时间。
应用编译使用显式并行上限：x64 为 MSBuild 4、编译器 `/MP2`，x86 为 MSBuild 2、编译器 `/MP1`，避免无限并行导致内存耗尽。
编译步骤每分钟输出新增编译日志、已生成的对象文件数、编译器进程数和剩余物理内存；完整日志保存在 Actions 的 `windows-<架构>-build-logs` 构件中，保留 7 天。
依赖脚本使用 MSYS2 的 `diffutils`，并固定 NASM 3.01 以兼容仓库当前的 libvpx。

## 跨版本复用依赖

GitHub 缓存有分支和标签作用域。在发布标签下生成的缓存只供该标签复用；后续新标签可以读取默认分支 `dev` 的缓存。
需要提前准备共享依赖时，在 Actions 手动选择 `dev`，开启 `dependencies_only`，保持 `publish` 关闭：

```powershell
gh workflow run windows-release.yml --ref dev -f dependencies_only=true -f publish=false
```

这次运行只准备并缓存依赖，跳过应用配置、编译、打包和发布。首次预热仍需要完整依赖编译；后续标签在依赖版本和缓存键匹配时直接恢复缓存。
依赖缓存可能被 GitHub 清理，因此缓存未命中时仍会执行准备脚本。

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
- `dependencies_only`：默认关闭；开启时仅准备依赖，用于在 `dev` 预热跨版本共享缓存，不发布 Release。
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
