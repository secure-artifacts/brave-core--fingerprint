![Brave Browser](./docs/images/brave.svg)

# Brave Core

> ## 本 Fork 说明
>
> 这是 `brave/brave-core` 的 fork，用于开发 Brave 指纹防护（fingerprinting）相关的定制功能。主仓库托管在自建 GitLab：`git@gitlab.195322.xyz:chromeextentions/software/brave-fingerprint.git`
>
> ### 仓库结构
>
> GitLab 上的历史是**截断过的，不含 brave-core 上游历史**。根提交 `f39db930f3f` 是上游 master `9fca1f7`（2026-07-15）的一次整树快照，仓库体积因此从 5.05 GiB 降到 379 MiB。完整上游历史只存在于本地 checkout 和 GitHub 备份 fork。
>
> remote：
>
> | 名称 | 地址 | 用途 |
> | --- | --- | --- |
> | `origin` | `git@gitlab.195322.xyz:chromeextentions/software/brave-fingerprint.git` | 主仓库，日常 push |
> | `upstream` | `https://github.com/brave/brave-core.git` | 官方仓库，只 fetch，不 push |
> | `github` | `https://github.com/secure-artifacts/brave-core--fingerprint.git` | 新历史的完整镜像，Windows 机器连不上 GitLab，走这里 |
>
> 分支：
>
> - `fingerprint` — 默认分支，所有自定义改动（persona、per-Profile proxy、L1/L2 指纹接入、L3 联动）都在这里
> - `upstream-snapshot` — 上游整棵树的快照序列，每同步一次上游追加一个快照提交，不放任何自定义代码
> - `master` — 只存在于本地，纯上游镜像，fast-forward only，**不要 push 到 GitLab**（会把 5 GB 上游历史带过去）
>
> ### 如何更新上游版本
>
> 只能在有完整上游历史的本地 checkout 里执行，GitLab 上没有上游历史。
>
> 第 2 步会用上游树覆盖工作区，开始前确认 `git status --porcelain` 无输出。
>
> ```bash
> # 1. 拉上游，快进本地镜像分支
> git fetch upstream master
> git checkout master
> git merge --ff-only upstream/master
>
> # 2. 生成新的上游快照提交
> git checkout upstream-snapshot
> git read-tree --reset -u master^{tree}
> git commit -m "chore: snapshot brave-core upstream master @ $(git rev-parse master)"
>
> # 3. 把自定义改动挪到新快照上
> git checkout fingerprint
> git rebase upstream-snapshot
>
> # 4. 推送
> git push origin upstream-snapshot && git push github upstream-snapshot
> git push --force-with-lease origin fingerprint && git push --force-with-lease github fingerprint
> ```
>
> 第 3 步的合并基准是上一次快照提交，三方合并正常工作，冲突量与直接 rebase 上游一致。不想改写自定义分支历史就换成 `git merge upstream-snapshot` 加普通 `git push`。
>
> 上游的 Chromium 版本变了的话，最后回到 checkout 根目录跑 `npm run sync` 重新应用 patch，再 `npm run build`。
>
> 频繁在 `upstream-snapshot` 和 `fingerprint` 之间切分支会大面积改写工作区、触发全量重建。想避免就给快照分支单开一个 worktree：`git worktree add ../brave-upstream-snapshot upstream-snapshot`。
>
> 每同步一次上游约给仓库增加 90 MB（按一个月上游跨度实测），同步越频繁仓库越大。体积失控时可以把旧快照压成一个新的根提交重新 baseline，代价是改写历史、所有克隆要重拉。
>
> ### 回滚
>
> 迁移前的分支保存在本地 tag `backup/pre-gitlab-*`，GitHub 上锚为 `archive/pre-gitlab-*` 三个 tag。
>
> 两个远程必须保持一致：GitLab 是主仓库，GitHub 是镜像，每次推送两边都要推。上游同步只在 macOS 机器上做。

Brave Core is a set of changes, APIs, and scripts used for customizing Chromium
to make the Brave browser. Please also check
https://github.com/brave/brave-browser which only holds the issues, releases and
the wiki.

## Overview

This repository holds the build tools needed to build the Brave desktop browser
for all platforms. In particular, it fetches and syncs code from the projects
defined in `package.json` and `src/brave/DEPS`:

- [Chromium](https://chromium.googlesource.com/chromium/src.git)
  - Fetches code via `depot_tools`.
  - Sets the branch for Chromium (ex: 65.0.3325.181).
- [brave-core](https://github.com/brave/brave-core)
  - Mounted at `src/brave`.
  - Maintains patches for 3rd party Chromium code.
- [adblock-rust](https://github.com/brave/adblock-rust)
  - Implements Brave's adblock engine.
  - Linked through
    [brave/adblock-rust-ffi](https://github.com/brave/brave-core/tree/master/components/adblock_rust_ffi).

## Resources

- [Documentation and guides](https://github.com/brave/brave-core/blob/master/docs/README.md)
- [Issues](https://github.com/brave/brave-browser/issues)
- [Releases](https://github.com/brave/brave-browser/releases)
- [Wiki](https://github.com/brave/brave-browser/wiki)

## Downloads

You can [visit our website](https://brave.com/download) to get the latest stable
release.

## Contributing

Please see the [contributing guidelines](./CONTRIBUTING.md).

Our [Wiki](https://github.com/brave/brave-browser/wiki) also has some useful
technical information, especially about setting the development environment.

## Security Policy

Please see the [security policy](./SECURITY.md).

## Community

[Join the Q&A community](https://community.brave.app/) if you'd like to get more
involved with Brave. You can
[ask for help](https://community.brave.app/c/support-and-troubleshooting),
[discuss features you'd like to see](https://community.brave.app/c/brave-feature-requests),
and a lot more. We'd love to have your help so that we can continue improving
Brave.

You can also ask questions and interact in the
[`community-guest`](https://bravesoftware.slack.com) channel on Brave Software's
Slack.

Help us translate Brave to your language by submitting translations at
https://explore.transifex.com/brave/brave_en/.

Follow [@brave](https://x.com/brave) on X for important news and announcements.

## Install prerequisites

Follow the instructions for your platform:

- [Android](https://github.com/brave/brave-browser/wiki/Android-Development-Environment)
- [Linux](https://github.com/brave/brave-browser/wiki/Linux-Development-Environment)
- [iOS](https://github.com/brave/brave-browser/wiki/iOS-Development-Environment)
- [macOS](https://github.com/brave/brave-browser/wiki/macOS-Development-Environment)
- [Windows](https://github.com/brave/brave-browser/wiki/Windows-Development-Environment)

## Clone and initialize

Once you have the prerequisites installed, you can get the code and initialize
the build environment.

```bash
git clone git@github.com:brave/brave-core.git path-to-your-project-folder/src/brave
cd path-to-your-project-folder/src/brave

# the Chromium source is downloaded, which has a large history (gigabytes of data)
# this might take really long to finish depending on internet speed

pnpm run init
```

brave-core based android builds should use
`pnpm run init --target_os=android --target_arch=arm` (or whichever CPU type you
want to build for) brave-core based iOS builds should use
`pnpm run init --target_os=ios`

Additional config needed to build are documented at
https://github.com/brave/brave-browser/wiki/Build-configuration

Internal developers can find more information at
https://github.com/brave/internal/wiki/Build-configuration

## Build Brave

The default build type is component.

```
# start the component build compile
pnpm run build
```

To do a release build:

```
# start the release compile
pnpm run build Release
```

brave-core based android builds should use
`pnpm run build --target_os=android --target_arch=arm`

brave-core based iOS builds should use the Xcode project found in
`ios/brave-ios/App`. You can open this project directly or run
`pnpm run ios_bootstrap --open_xcodeproj` to have it opened in Xcode. See the
[iOS Developer Environment](https://github.com/brave/brave-browser/wiki/iOS-Development-Environment#Building)
for more information on iOS builds.

### Build Configurations

Running a release build with `pnpm run build Release` can be very slow and use a
lot of RAM, especially on Linux with the Gold LLVM plugin.

To run a statically linked build (takes longer to build, but starts faster):

```bash
pnpm run build Static
```

To run a debug build (Component build with is_debug=true):

```bash
pnpm run build Debug
```

NOTE: the build will take a while to complete. Depending on your processor and
memory, it could potentially take a few hours.

## Run Brave

To start the build:

`pnpm start [Release|Component|Static|Debug]`

## Update Brave

`pnpm run sync [--force] [--init] [--create] [brave_core_ref]`

**This will attempt to stash your local changes in brave-core, but it's safer to
commit local changes before running this**

`pnpm run sync` will (depending on the below flags):

1. 📥 Update sub-projects (chromium, brave-core) to latest commit of a git ref
   (e.g. tag or branch)
2. 🤕 Apply patches
3. 🔄 Update gclient DEPS dependencies
4. ⏩ Run hooks

| flag                           | Description                                                                                                                                                                                                                                                                                                                                                                |
| ------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `[no flags]`                   | updates chromium if needed and re-applies patches. If the chromium version did not change, it will only re-apply patches that have changed. Will update child dependencies **only if any project needed updating during this script run**. <br> **Use this if you want the script to manage keeping you up to date instead of pulling or switching branches manually. **   |
| `--force`                      | updates both _Chromium_ and _brave-core_ to the latest remote commit for the current brave-core branch and the _Chromium_ ref specified in brave-core/package.json (e.g. `master` or `74.0.0.103`). Will re-apply all patches. Will force update all child dependencies. <br> **Use this if you're having trouble and want to force the branches back to a known state. ** |
| `--init`                       | force update both _Chromium_ and _brave-core_ to the versions specified in brave-core/package.json and force updates all dependent repos - same as `pnpm run init`                                                                                                                                                                                                         |
| `--sync_chromium (true/false)` | Will force or skip the chromium version update when applicable. Useful if you want to avoid a minor update when not ready for the larger build time a chromium update may result in. A warning will be output about the current code state expecting a different chromium version. Your build may fail as a result.                                                        |
| `-D, --delete_unused_deps`     | Will delete from the working copy any dependencies that have been removed since the last sync. Mimics `gclient sync -D`.                                                                                                                                                                                                                                                   |

Run `pnpm run sync brave_core_ref` to checkout the specified _brave-core_ ref
and update all dependent repos including chromium if needed.

## Scenarios

#### Create a new branch:

```bash
> cd src/brave
src/brave> git checkout -b branch_name
```

#### Checkout an existing branch or tag:

```bash
src/brave> git fetch origin
src/brave> git checkout [-b] branch_name
src/brave> pnpm run sync
...Updating 2 patches...
...Updating child dependencies...
...Running hooks...
```

#### Update the current branch to the latest remote:

```bash
src/brave> git pull
src/brave> pnpm run sync
...Updating 2 patches...
...Updating child dependencies...
...Running hooks...
```

#### Reset to latest brave-core master (via `init`, will always result in a longer build and will remove any pending changes in your brave-core working directory):

```bash
src/brave> git checkout master
src/brave> git pull
src/brave> pnpm run sync --init
```

#### When you know that DEPS didn't change, but .patch files did (quickest attempt to perform a mini-sync before a build):

```bash
src/brave> git checkout featureB
src/brave> git pull
src/brave> pnpm run apply_patches
...Applying 2 patches...
```

## Enabling third-party APIs

1. **Google Safe Browsing**: Get an API key with SafeBrowsing API enabled from
   https://console.developers.google.com/. Update the `GOOGLE_API_KEY`
   environment variable with your key as per
   https://www.chromium.org/developers/how-tos/api-keys to enable Google
   SafeBrowsing.

## Development

- [Security rules from Chromium](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/docs/security/rules.md)
- [IPC review guidelines](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/security/ipc-reviews.md)
  (in particular
  [this reference](https://docs.google.com/document/d/1Kw4aTuISF7csHnjOpDJGc7JYIjlvOAKRprCTBVWw_E4/edit#heading=h.84bpc1e9z1bg))
- [Brave's internal security guidelines](https://github.com/brave/internal/wiki/Pull-request-security-audit-checklist)
  (for employees only)
- [Rust usage](https://github.com/brave/brave-core/blob/master/docs/rust.md)

## Troubleshooting

See
[Troubleshooting](https://github.com/brave/brave-browser/wiki/Troubleshooting)
for solutions to common problems.
