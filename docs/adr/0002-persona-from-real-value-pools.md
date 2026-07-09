# ADR-0002: Persona = 真值池合成,per-profile 持久

Status: Accepted (2026-07-08)

## Context

每个 Profile 需要一套指纹。三条路:随机但持久 / 真机采集 / 真值池合成 / 买商业库。
随机值的组合(如某 WebGL renderer 串)在真机人群里不存在 → 唯一性本身就是破绽,高级
检测器(crowd-blending)专抓。真机采集需采集基础设施+量。商业库要钱+可能合规问题。

## Decision

**用真值池合成:** 人工维护真实存在的候选值池——真 WebGL renderer 串、各 OS 真屏幕档、
各 OS/locale 真字体集、真 UA+版本组合等——由一致性引擎按规则拼出**合法自洽**的组合,
分配给 Profile 并持久化(同 Profile 恒定)。

## Consequences

- 单人可维护,无需采集真机;组合真实度靠一致性规则保证,而非碰运气。
- 核心工作转移到:①维护真值池数据 ②写一致性引擎(哪些值能共存:OS↔UA↔renderer↔字体↔屏幕)。
- 池子越大越真;初期小池 = 多 Profile 撞相同 persona 的风险,需注意 crowd-blending 双刃。
- persona 需要一个存储位置(per-Profile pref 或独立存储),供 renderer 侧种子读取。
