# Bugfix Requirements Document

## Introduction

Chat 页面在向 LLM Provider（如 OpenAI、Anthropic、DeepSeek 等）发送消息时，
`FastNetHttpTransport::send()` 抛出 TLS 握手失败错误：

```
Error: Error [SSLHandshakeError]: TLS handshake failed:
  error:0A000410:SSL routines::ssl/tls alert handshake failure
```

错误码 `0A000410` 对应 OpenSSL 3 的 `SSL_AD_HANDSHAKE_FAILURE`。
根本原因是 `FastNetHttpTransport` 在构建 `SSLConfig` 时设置了 `verifyPeer = true`，
但未指定 CA 证书路径，导致 OpenSSL 3 在 Windows 上无法验证服务端证书链，握手中止。

该 Bug 影响所有通过 HTTPS 与 LLM Provider 通信的功能，包括 Chat 对话、模型列表拉取、
音频转录等，实际上使整个 AI 功能不可用。

## Bug Analysis

### Current Behavior (Defect)

1.1 WHEN 用户在 Chat 页面发送消息且 Provider 端点为 HTTPS 时，
    THEN 系统抛出 `TLS handshake failed: error:0A000410` 错误，请求失败，消息无法送达

1.2 WHEN `FastNetHttpTransport::send()` 对 HTTPS URL 构建 `SSLConfig` 时，
    THEN 系统设置 `verifyPeer = true` 但不提供 CA 证书路径，
    导致 OpenSSL 3 找不到受信任的根证书而拒绝握手

1.3 WHEN 模型列表同步（`listModels()`）或音频转录（`transcribeAudioFile()`）
    向 HTTPS 端点发起请求时，
    THEN 系统同样因 TLS 握手失败而返回错误，功能不可用

### Expected Behavior (Correct)

2.1 WHEN 用户在 Chat 页面发送消息且 Provider 端点为 HTTPS 时，
    THEN 系统 SHALL 成功完成 TLS 握手，建立加密连接，正常发送请求并返回响应

2.2 WHEN `FastNetHttpTransport::send()` 对 HTTPS URL 构建 `SSLConfig` 时，
    THEN 系统 SHALL 在 `SSLConfig` 中提供有效的 CA 证书路径（Windows 系统证书存储
    或 OpenSSL 3 随附的 `cacert.pem`），使 OpenSSL 3 能够验证服务端证书链

2.3 WHEN 模型列表同步或音频转录向 HTTPS 端点发起请求时，
    THEN 系统 SHALL 成功完成 TLS 握手，正常返回结果

### Unchanged Behavior (Regression Prevention)

3.1 WHEN 请求目标为 HTTP（非 HTTPS）端点时，
    THEN 系统 SHALL CONTINUE TO 不启用 SSL，行为与修复前完全一致

3.2 WHEN TLS 握手成功后，HTTP 请求/响应的序列化、超时、代理、错误处理逻辑时，
    THEN 系统 SHALL CONTINUE TO 保持与修复前完全相同的行为

3.3 WHEN `FastNetWebSocketTransport` 建立 WebSocket 连接时，
    THEN 系统 SHALL CONTINUE TO 保持与修复前完全相同的行为（WebSocket 传输层不在本次修复范围内）

3.4 WHEN `EmailChannel` 使用 SMTP/IMAP TLS 连接时，
    THEN 系统 SHALL CONTINUE TO 保持与修复前完全相同的行为（Email 通道不在本次修复范围内）
