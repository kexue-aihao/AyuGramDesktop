# Windows 自构建与发布

工作流：`.github/workflows/windows-release.yml`，只构建 Windows x64。
使用可调度的 `windows-2022`、MSVC 14.44、Windows SDK 10.0.26100.0 和 Python 3.10，
通过仓库现有的 `prepare/win.bat` 准备所选架构的依赖，再编译 Release；依赖准备阶段使用 OpenSSL 官方的 `nmake` 串行目标、关闭 OpenSSL 的强制同步 PDB 写入，禁用 libvpx 工程内部的多处理器编译，并将依赖并行度限制为 2，避免标准 runner 上共享 PDB 或 MSYS2 Make 的并行编译挂起。
工具、各架构的库和 Qt 分开缓存；首次构建需要较长时间。
应用编译使用显式并行上限：MSBuild 4、编译器 `/MP2`，避免无限并行导致内存耗尽。
编译步骤每分钟输出新增编译日志、已生成的对象文件数、编译器进程数和剩余物理内存；完整日志保存在 Actions 的 `windows-<架构>-build-logs` 构件中，保留 7 天。
依赖脚本使用 MSYS2 的 `diffutils`，并固定 NASM 3.01 以兼容仓库当前的 libvpx。

## 本机构建（推荐）

迭代时不必等 GitHub 托管的 `windows-2022` runner（4 vCPU 上 Release 全量编译约 85–110 分钟）。
本机依赖准备好后，首次 Release 编译约 40–60 分钟，之后增量编译只需几分钟，
失败后可以直接原地重跑。

前置条件：

- Visual Studio 2022 Build Tools，包含 MSVC v143 14.44、ATL/MFC 和 Windows SDK 10.0.26100.0。
- 在仓库上一级目录准备好依赖（`Libraries\win64` 和 `ThirdParty`）；首次可在
  x64 Native Tools 命令提示符中运行 `Telegram\build\prepare\win.bat silent`。
- Python 3；脚本优先使用 `ThirdParty\python\Scripts\python.exe`，也可以用
  `AYUGRAM_PYTHON` 环境变量指定。

构建：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_windows_release.ps1
```

构建并打包：

```powershell
powershell -ExecutionPolicy Bypass -File tools\build_windows_release.ps1 -ReleaseTag v20261020
```

脚本会自动初始化 MSVC 14.44 环境，用与工作流相同的参数运行 `configure.bat`，
并以 `MSBuild /m` 加 `cl /MP2` 编译 Release，产物为 `out\Release\AyuGram.exe`。
指定 `-ReleaseTag` 时调用现有的 `package_windows.ps1` 生成便携版 ZIP；检测到
Inno Setup 时还会生成 `setup.exe`。打包结果位于仓库上一级的 `dist` 目录。

可选参数：

- `-ValidateOnly`：只检查工具链、Python 和依赖，不配置、不编译。
- `-SkipConfigure`：复用现有 `out` 目录，跳过 `configure.bat`。
- `-SkipPackage`：即使指定了 `-ReleaseTag` 也只编译。
- `-Parallel`：MSBuild 并行项目数，默认取逻辑处理器数的一半。

上传到已有 Release：

```powershell
gh release upload v20261020 ..\dist\*Windows-x64*.zip ..\dist\*.sha256 --clobber
```

本机脚本和发布工作流都只构建 x64。

## 跨版本复用依赖

GitHub 缓存有分支和标签作用域。在发布标签下生成的缓存只供该标签复用；后续新标签可以读取默认分支 `dev` 的缓存。
需要提前准备共享依赖时，在 Actions 手动选择 `dev`，开启 `dependencies_only`，保持 `publish` 关闭：

```powershell
gh workflow run windows-release.yml --ref dev -f dependencies_only=true -f publish=false
```

这次运行只准备并缓存依赖，跳过应用配置、编译、打包和发布。首次预热仍需要完整依赖编译；后续标签在依赖版本和缓存键匹配时直接恢复缓存。
依赖缓存可能被 GitHub 清理，因此缓存未命中时仍会执行准备脚本。

## 发布 Ayugram Desktop v20261020

将包含所需代码的提交推送到仓库，然后推送版本标签：

```powershell
git push origin dev
git tag -a v20261020 -m "Ayugram Desktop v20261020"
git push origin v20261020
```

推送 `vYYYYMMDD` 标签会自动构建并发布，Release 名称为 `Ayugram Desktop vYYYYMMDD`。
构建不修改 `Telegram/build/version` 中的上游数字版本，该版本仍供 Telegram 协议和 Windows 资源使用。

也可以在 Actions 中手动运行 **Windows self-build release**：

- `release_tag`：默认 `v20261020`。
- `publish`：默认关闭，仅上传 Actions 构件；开启时，对应标签必须已经存在并指向所选提交。
- `dependencies_only`：默认关闭；开启时仅准备依赖，用于在 `dev` 预热跨版本共享缓存，不发布 Release。
- 重新运行失败的任务可以续传草稿 Release；已公开的 Release 不会被覆盖。

## API 配置

默认使用仓库 `docs/api_credentials.md` 和 `docs/building-win.md` 已记录的 API 配置。
如需自有配置，在仓库 Settings → Secrets and variables → Actions 中同时设置
`TDESKTOP_API_ID` 和 `TDESKTOP_API_HASH` 两个 Secrets。
无需 Canary、代码签名或更新签名服务的 Secrets。

## 下载与使用

构建成功后，工作流校验 ZIP 和 SHA-256，再公开 GitHub Release，附件为：

- `Ayugram-Desktop-v20261020-Windows-x64.zip`
- `Ayugram-Desktop-v20261020-Windows-x64-setup.exe`
- `SHA256SUMS.txt`

解压后运行 `AyuGram.exe`。包内包含匹配架构的 Direct3D 编译器、许可证和 `BUILD.txt` 源码提交信息。
`TelegramForcePortable` 目录用于保存便携版账号数据，移动或清理程序目录前请备份该目录。
下载加速设置位于 AyuGram → General → Downloads → Download Acceleration。

自构建产物未做代码签名，自动更新关闭。后续更新请从本仓库 Releases 下载。

```powershell
Get-FileHash .\Ayugram-Desktop-v20261020-Windows-x64.zip -Algorithm SHA256
Get-FileHash .\Ayugram-Desktop-v20261020-Windows-x64-setup.exe -Algorithm SHA256
```

将输出与 `SHA256SUMS.txt` 对应行比较即可验证文件完整性。
