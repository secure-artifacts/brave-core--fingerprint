# ADR-0006: L2 渲染用 per-profile 持久确定性噪声(非 GPU 精确伪装)

Status: Accepted (2026-07-08)

## Context

Canvas/WebGL/Audio 的**渲染字节**(不是 metadata 串)由宿主机真显卡经 Skia/ANGLE 产生。即使把
`UNMASKED_RENDERER` 设成 "RTX
3060",真实像素仍是宿主 GPU 渲的,hash 对不上真 3060 机器的产出。两条路:

- 持久 per-profile 噪声(仿 Brave
  PerturbPixels,种子改 persona):hash 稳定+唯一,便宜。
- GPU 精确伪装(ANGLE/驱动层复现目标显卡输出):BotBrowser 级,几个月工作量。

## Decision

**v1 采用持久 per-profile 确定性噪声。**
复用 Brave 的 canvas/audio 扰动机制,但种子从 persona 派生(同 Profile 恒定),使渲染 hash 稳定且 per-Profile 唯一。

## Consequences

- 便宜、可复用现有钩子,足以躲 FingerprintJS/CreepJS 级(它们比对的是 hash 稳定性+熵,不是「hash 是否等于该 GPU 真值」)。
- 残余风险:遇到持有「GPU 型号 → canvas hash」映射库的检测器,noise
  hash 对不上声称的显卡 → 可能穿帮。FB 目前未知是否有此库;列为已知残余风险,若实测被 flag 再升级到 GPU 精确伪装。
- metadata 串(renderer/vendor)仍取自 persona 真值池,与噪声 hash 是两回事,都要做。
