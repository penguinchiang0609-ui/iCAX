# Facades

`Facades` 是前端 SDK 的调用边界。产品页面通常通过 `AppProxy`、`ProductProxy`、`ProjectProxy` 和 `SceneProxy` 使用它，无需接触传输细节。

- `facadeClient.mjs`：发起 Facade 调用、接收 report/response/event，并可通过 `expose()` 向后端公开前端 Facade。
- `facadeMethod.mjs`：稳定的 Facade 方法编码。
- `variantSerializer.mjs`：跨前后端 payload 编解码。
- `channelId.mjs`：Facade channel ID 校验。

一次调用的 Request、Report 和 Response 始终共享同一个 `callId`。
