# brave-fingerprint 历史重写 — Windows 机器交接说明

日期：2026-09-07
执行方：macOS 主力机（已完成并推送到两个远程）
待处理：Windows 机器上的同一份 checkout，含未提交改动

---

## 1. 发生了什么

主仓库迁到了自建 GitLab，**并且历史被重写了**。GitLab 上的历史是截断过的，不含 brave-core 上游历史，仓库从 5.05 GiB 降到 379 MiB。

因为 Windows 机器连不上 GitLab，**GitHub fork 现在是新历史的完整镜像**，两个远程的 `fingerprint` 指向同一个提交。Windows 那边继续用 GitHub，不需要碰 GitLab。

代码内容一个字节都没变。变的是提交 SHA：

| 项 | 值 |
| --- | --- |
| 旧 `fingerprint` 末端 | `c0d4cb0142dfddac500cfa9bae1266896b4bb554` |
| 新历史里的等价提交 | `c1a889a667ec9c5f28b2b74a1e7f8b1c9180b5fd` |
| 两者共同的文件树 | `80be48470eeb2b56d46811983468fd86267edf74` |
| 新 `fingerprint` 末端（写文档时） | `1ee8ed3a4f3`，之后会随 macOS 的提交前移 |
| 快照根提交 | `f39db930f3f56695a3b2d53ad1890fdec53faafe` |
| 快照对应的上游 master | `9fca1f7dea556b026bd3c8381dffe7f5ad1f55b0`（2026-07-15） |

两个提交 SHA 不同，指向的文件树完全一致。这是无损接驳的依据，也是第 5 步验证要比对的值。

## 2. 远程结构

| 机器 | remote 名 | 地址 | 用途 |
| --- | --- | --- | --- |
| Windows | `origin` | GitHub fork（保持不变） | 日常 fetch / push |
| macOS | `origin` | GitLab | 主仓库 |
| macOS | `github` | GitHub fork | 镜像，与 GitLab 保持同步 |
| 两台 | `upstream` | `https://github.com/brave/brave-core.git` | 官方仓库，只 fetch |

Windows 上 remote 命名不用改，`origin` 仍然是 GitHub。两台机器 `origin` 指向不同的地方，这是有意的。

GitHub 上现在的分支：

- `fingerprint` — 默认工作分支，已被强推为新历史
- `upstream-snapshot` — 上游整树快照序列，不放自定义代码
- `safety/fingerprint-pre-upstream-sync-20260802` — 历史存档
- `master` — 上游镜像，没动

迁移前的历史用 tag 锚在 GitHub 上，永久可回滚：

```
archive/pre-gitlab-fingerprint          c0d4cb0142d
archive/pre-gitlab-safety-20260715      4acfeff3969
archive/pre-gitlab-safety-20260802      8cb52429c14
```

## 3. 操作步骤

全部在 `<你的 checkout>\src\brave` 目录下执行。PowerShell 里 `^{tree}` 这类写法要加引号，建议直接用 Git Bash。

### 第 0 步：先看清现状

```bash
cd <checkout>/src/brave
git status
git rev-parse HEAD
git rev-parse HEAD^{tree}                     # 记下这个值，迁移后要对比
git log --oneline origin/fingerprint..HEAD    # 本地未推送的提交，记下有几个
git stash list
```

这一步的输出决定第 3 步走哪条分支。

### 第 1 步：备份，然后把未提交的改动提交掉

```bash
git bundle create ../pre-rewrite-backup.bundle --all
```

bundle 包含所有分支和提交，出任何问题都能从它恢复。必做，别跳。

接着**直接提交**，不要用 stash：

```bash
git add -A
git commit -m "<描述>"
```

在旧历史上提交完全没问题，提交对象跟历史怎么重写无关，第 3 步会把它重放到新历史上。提交比 stash 稳得多：commit 是持久对象，`rebase` 处理冲突的工具也比 `stash pop` 好用。

**这一步之后、第 3 步做完之前，绝对不要 push。**详见第 5 步的警告。

实在不想提交，就存 patch 双保险：

```bash
git diff > ../uncommitted.patch
git diff --cached > ../staged.patch
git stash push -u -m "pre-rewrite-migration"
```

### 第 2 步：拉取新历史

```bash
git fetch origin --prune --tags
git log --oneline -1 origin/fingerprint
```

你会看到一个陌生的 SHA，这是正常的，整条历史都被重写过了。写这份文档时末端是 `1ee8ed3a4f3 docs: mirror rewritten history to GitHub for the Windows machine`，之后会前移。

### 第 3 步：把本地工作挂到新历史上

**情况 A — 有本地提交（照第 1 步提交过的话就是这条）**：

```bash
ANCHOR=$(git merge-base HEAD refs/tags/archive/pre-gitlab-fingerprint)
git rebase --onto origin/fingerprint $ANCHOR
```

这会把你的本地提交重放到新历史顶端。归档 tag 就是旧的分支末端，拿它当锚点最稳。冲突只会来自你自己的改动和这段时间 macOS 机器上的提交之间。

**情况 B — 什么都没提交，只有 stash**：

```bash
git checkout -B fingerprint origin/fingerprint
git stash pop
```

两边文件树一致，checkout 不会动你的文件，stash 正常落回。

**情况 C — 本地状态太乱，或上面两条出问题**：

重新克隆一份干净的，再把改动 apply 回去：

```bash
cd <上层目录>
git clone https://github.com/secure-artifacts/brave-core--fingerprint.git brave-core-new
cd brave-core-new
git apply ../uncommitted.patch
```

注意这份克隆只有截断历史，做不了上游同步。

### 第 4 步：验证

```bash
git log --oneline -3
git status
git rev-parse HEAD^{tree}
```

如果你没有本地提交，这个树哈希应该等于 `80be48470eeb2b56d46811983468fd86267edf74`，或者等于第 0 步记下的值。不一致说明有改动丢了，回 bundle 恢复，别继续往下走。

### 第 5 步：推送前检查

```bash
git rev-list --count origin/fingerprint..HEAD
```

**这个数字必须是你自己提交的数量，通常个位数。如果是几万或几十万，说明挂错了历史，停下来别推。**

确认没问题再推：

```bash
git push --force-with-lease origin fingerprint
```

**红线：第 3 步的 rebase 做完之前不要 push，尤其不要 `git push -f`。** GitHub 上的 `fingerprint` 现在已经是新历史，拿旧历史强推会把镜像冲掉。普通 push 会被 git 以 non-fast-forward 拒绝，那是保护，别绕过它。真冲掉了能从 GitLab 恢复，但要多折腾一轮。

推之前跟 macOS 那边说一声，避免和对方的 rebase 撞车。

## 4. 两台机器怎么协作

- **Windows** 只跟 GitHub 打交道，正常 fetch / push `fingerprint`。
- **macOS** 拿 GitLab 当主仓库，但每次推送要同时推 GitHub，保持镜像一致：

  ```bash
  git fetch github
  git rebase github/fingerprint      # 先吃掉 Windows 的提交
  git push origin fingerprint        # GitLab
  git push github fingerprint        # GitHub
  ```

- **上游同步只在 macOS 上做**，Windows 不要碰 `upstream-snapshot` 分支。两边各自生成快照提交会打架。

没有配 dual pushurl 一次推两处，是因为两台机器都会 push，`--force-with-lease` 的租约按 GitLab 状态计算，两边一分叉这条命令要么失败要么覆盖掉对方的提交。显式推两次更安全。

## 5. 红线

- **不要把 `master` 或任何带完整上游历史的分支推到 GitLab。** 推上去会把 5 GB 上游历史带过去，迁移就白做了。Windows 连不上 GitLab，风险主要在 macOS 那边。
- `archive/pre-gitlab-*` 这几个 tag 不要删，它们是唯一的回滚锚点。
- 两个远程的 `fingerprint` 要保持一致，别只推一边就走。

## 6. 以后怎么同步上游

必须在有完整上游历史的 checkout 里做。完整流程写在仓库 `README.md` 顶部的「本 Fork 说明」里，简述：

```bash
git fetch upstream master
git checkout master && git merge --ff-only upstream/master

git checkout upstream-snapshot
git read-tree --reset -u master^{tree}
git commit -m "chore: snapshot brave-core upstream master @ $(git rev-parse master)"

git checkout fingerprint && git rebase upstream-snapshot

git push origin upstream-snapshot && git push github upstream-snapshot
git push --force-with-lease origin fingerprint && git push --force-with-lease github fingerprint
```

合并基准是上一次快照提交，三方合并正常工作，冲突量和以前直接 rebase 上游一样。每同步一次仓库约增加 90 MB。

## 7. 出问题怎么回滚

```bash
git bundle verify ../pre-rewrite-backup.bundle
git fetch ../pre-rewrite-backup.bundle 'refs/heads/*:refs/heads/restored/*'
```

或者从 GitHub 上的归档 tag 取：

```bash
git fetch origin --tags
git checkout -b restored archive/pre-gitlab-fingerprint
```

macOS 机器上另有 `backup/pre-gitlab-*` 四个本地 tag。
