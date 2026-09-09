# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 这个目录是什么

这是一个 **gclient 管理的 Chromium + brave-core 完整 checkout**（约定用途：Brave 指纹防护 / fingerprinting 相关工作）。根目录本身**不是 git 仓库**，只是 gclient workspace：

- `/`（根）— gclient 配置（`.gclient`、`.gclient_entries`），由 `npm run sync` 自动维护，勿手改
- `src/` — Chromium 源码（git，当前基于 tag `151.0.7922.71`）
- `src/brave/` — brave-core（git），所有 Brave 定制代码在这里；remote 设置见下方「Fork 与上游同步」
- `src/out/Component_arm64/` — 构建输出（component build，macOS arm64）；`args.gn` 可手动追加参数（import `args_generated.gni` 之后）

**所有 `npm run` 命令必须在 `src/brave/` 目录下执行。**

## 权威文档入口（先读这些）

brave-core 自带完整的 agent 文档体系，位于 `src/brave/`：

- `src/brave/.claude/CLAUDE.md` — brave-core 的 canonical agent 指引（在根目录开 session 时不会自动加载，进入 brave 相关任务前先读）
- `src/brave/docs/README.md` — 文档索引，按任务需要选读
- `src/brave/docs/best_practices.md` — 最佳实践索引（C++ 规范、测试、patches、chromium_src overrides 等）
- `src/brave/.claude/skills/` — brave-core 自带 skills（pr、review、preflight、make-ci-green、rebase-downstream 等）

## 常用命令（均在 `src/brave/` 下）

```bash
npm run sync                 # gclient sync + 应用 patches（Chromium 版本变动后必跑）
npm run build                # 构建（默认 Component 配置）
npm run start                # 启动构建出的浏览器
npm run test -- <suite> --filter="Fixture.Test*"   # C++ 测试
npm run test-unit -- <path>  # Jest（TypeScript/React 单测）
npm run format               # 格式化（含 markdown 文档）
npm run presubmit            # 提交前检查
npm run update_patches       # 修改 Chromium 侧文件后重新生成 patch
npm run gn_check             # GN 依赖检查
```

C++ 测试 suite 取值：`brave_components_unittests` / `brave_unit_tests` / `brave_browser_tests` / `chromium_unit_tests` / `browser_tests`。`--filter` 支持通配与 `:` 组合。详见 `src/brave/docs/running_test_suites.md`。

## 架构大图

brave-core 通过三种机制定制 Chromium（详见 `src/brave/docs/patching_and_chromium_src.md`）：

1. `src/brave/patches/` — 对 Chromium 源码的最小化 diff patch
2. `src/brave/chromium_src/` — 同路径文件覆盖（override）机制，优先于 patch
3. `plaster` — 语义化 patch 引擎（`src/brave/docs/plaster.md`）

修改了 `src/`（Chromium 侧）文件后必须 `npm run update_patches`，否则改动会在下次 sync 时丢失。

### 指纹防护（farbling）相关代码位置

- `src/brave/browser/farbling/` — 各 API 的 farbling browsertests（WebGL、canvas、screen、navigator.* 等，文件即功能清单）
- `src/brave/third_party/blink/renderer/core/farbling/` — `brave_session_cache`（farbling renderer cache；`fingerprint` 分支优先使用 persona 派生 token，persona 缺失/损坏时回退 Brave 原随机 token）
- `src/brave/components/brave_shields/` — Shields 设置层（`core/common/brave_shield_constants.h` 等），farbling 级别由 Shields 的 fingerprinting 控制项决定
- `src/brave/chromium_src/third_party/blink/renderer/` — Blink 侧 API 拦截的 override 实现

## Fork 与上游同步（`src/brave/`）

主仓库已于 2026-09-07 迁到自建 GitLab。GitLab 上的历史是**截断过的**：不含 brave-core 上游历史，根提交是一个上游整树快照（`f39db930f3f`，对应上游 master `9fca1f7`，2026-07-15）。完整上游历史只存在于本地这份 checkout 和 GitHub fork。

remote：

- `origin` → `git@gitlab.195322.xyz:chromeextentions/software/brave-fingerprint.git`（主仓库，默认分支 `fingerprint`，日常 push 到这里）
- `github` → `https://github.com/secure-artifacts/brave-core--fingerprint.git`（同一份新历史的完整镜像；Windows 机器连不上 GitLab，只走这个远程，所以每次推送两边都要推）
- `upstream` → `https://github.com/brave/brave-core.git`（官方仓库，只 fetch，不 push）

分支约定：

- `upstream-snapshot` — 上游整棵树的快照序列，每同步一次上游追加一个快照提交，不放任何自定义代码
- `fingerprint` — 自定义改动分支（persona、per-Profile proxy、L1/L2 指纹接入、L3 联动），基于 `upstream-snapshot`
- `master` — 本地上游镜像，只做 fast-forward，**不要 push 到 GitLab**（推上去会把 5 GB 上游历史带过去，迁移就白做了）

**同步 upstream 更新的标准流程**（只能在本地这份 checkout 执行，GitLab 上没有上游历史）：

```bash
cd src/brave
git fetch upstream master
git checkout master
git merge --ff-only upstream/master

git checkout upstream-snapshot          # 生成新的上游快照提交
git read-tree --reset -u master^{tree}  # 用上游树覆盖工作区，执行前确认工作区干净
git commit -m "chore: snapshot brave-core upstream master @ $(git rev-parse master)"

git checkout fingerprint
git rebase upstream-snapshot            # 合并基准是上一次快照，冲突处理与直接 rebase 上游一致

git push origin upstream-snapshot && git push github upstream-snapshot
git push --force-with-lease origin fingerprint && git push --force-with-lease github fingerprint
```

每同步一次上游约给仓库增加 90 MB（按一个月上游跨度实测），同步越频繁仓库越大。

迁移前的原始分支保存在本地 tag `backup/pre-gitlab-*`，GitHub 上锚为 `archive/pre-gitlab-*` 三个 tag，回滚从这两处取。

两台机器协作：Windows 只跟 GitHub 打交道；本机推送前先 `git fetch github && git rebase github/fingerprint` 吃掉对方的提交，再推两个远程。上游快照只在本机生成。Windows 机器的交接说明见 `GITLAB-MIGRATION-HANDOFF.md`。

## 本 checkout 的约定

- brave-core 是上游开源仓库：commit message、代码注释用英文，遵循上游规范（`src/brave/CONTRIBUTING.md`），不适用 duty workspace 的中文 commit 约定
- 日常开发在 `fingerprint` 分支上进行；`master` 只用于同步 upstream，不要在上面直接 commit
- 根目录的 `.gclient*`、`.cipd/`、`.npm-cache/` 均为工具生成物，不要编辑或提交

## 从零重建 checkout

本目录（`src/brave/workspace/`）保存的是 gclient 根目录那几个不在任何仓库里的文件。gclient 根不是 git 仓库，机器一旦回收这些就没了，所以在这里留了一份。重建步骤：

1. 按 https://github.com/brave/brave-browser/wiki/macOS-Development-Environment 装好依赖，`git clone https://github.com/brave/brave-browser.git && npm install`
2. 把 brave-core 的 remote 指到本仓库：`origin` → GitLab，`github` → GitHub 镜像，`upstream` → 官方，checkout `fingerprint` 分支
3. `npm run sync` 拉 Chromium 并应用 patch；`.gclient`、`.gclient_entries`、`.gcs_entries` 由它生成，不要从旧机器拷
4. 把本目录的 `CLAUDE.md`、`AGENTS.md`、`GITLAB-MIGRATION-HANDOFF.md`、`build-release-arm64.sh` 拷回 gclient 根，`beads/beads.db` 拷成根目录的 `.beads/beads.db`
5. `build-release-arm64.sh` 会自己定位 brave-core，放在 gclient 根或本目录下都能跑

`brand/` 是 Fingerprint Browser 的图标母版。`logo.svg` 和 `userDocs/public/logo.svg` 同源，几个 PNG 是它的导出；仓库里 `app/theme/brave/` 下的 app 图标是更早一版，重新出图标时以本目录为准。
