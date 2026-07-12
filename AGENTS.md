# SuperGuardian Agent Instructions

在本仓库中工作前，必须完整阅读根目录的 `PROJECT_MEMORY.md`，并同时遵守 `.github/copilot-instructions.md` 中的产品约束。

工作规则：

- 默认使用中文与用户沟通。
- 先检查当前 Git 状态，保留与当前任务无关的用户修改。
- 不要把守护启动延时和定时重启启动延时合并为相同行为；具体规则见 `PROJECT_MEMORY.md` 第 5 节。
- 修改持久化字段时，同步处理加载、保存、导入、导出和诊断信息。
- 核心守护逻辑修改后，按 `PROJECT_MEMORY.md` 第 9 节进行验证。
- 不要仅修改未被运行时代码使用的 `.ui` 文件来实现界面功能。
- 每次修改并完成必要验证后，必须创建本地 Git 提交；提交前只暂存当前任务范围内的文件，并检查暂存差异。
- Git 提交说明统一使用中文。
- 每个修改任务默认递增补丁版本号，并使用 `tools/set-version.ps1` 同步 `src/app/main.cpp` 与 `resources/app.rc`；只有用户明确要求不更新版本号时才跳过。
- 每个修改任务默认生成新的 Release EXE，并更新 `x64/Release/SuperGuardian.exe`；只有用户明确要求不生成时才跳过。生成时必须保留 `x64/Release/data/` 等现有运行数据，不得用项目默认清理流程直接删除该目录。
- 每次修改都要更新根目录的 `更新记录.md`；所有版本记录保存在同一个文件中，最新版本写在最上方。
- 不要推送到任何远端，除非用户在当前任务中明确要求推送。
- 未经用户明确要求，不创建 Release 或执行其他发布操作。
- 当用户要求上传程序时：先确保版本号、更新记录和 Release EXE 已完成；再按 `SuperGuardian_v{版本}_{yyyyMMdd_HHmmss}.zip` 命名，在 `package/` 中生成 ZIP。ZIP 根目录内容应与现有包一致，只包含 `SuperGuardian.exe` 和 `README.md`，然后将该 ZIP 上传到对应版本的 GitHub Release。上传程序不等于允许推送源码分支。
- 发现长期有效的新事实或明确产品约束时，同步更新 `PROJECT_MEMORY.md`。
