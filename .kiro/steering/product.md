# Product

YAOS is a C++ / Qt / QML desktop AI runtime and control console for Windows. It is not just a chat UI, it bundles the following capabilities in one system:

- Model and provider management (OpenAI, Anthropic, Azure, OpenRouter, DeepSeek, Gemini, Zhipu, etc.)
- Desktop chat workbench (QML Studio)
- Multi-channel messaging gateway (Telegram, Slack, Feishu, DingTalk, Discord, Matrix, Email, WhatsApp, QQ, Mochat)
- Tool permissions and human approval flows
- Runtime topology with multi-node delegation
- Local and clustered memory plane (legacy Markdown or layered SQLite/hybrid)
- Plugin, skill, and MCP extension loading
- Automation and scheduled execution (cron)
- Task, event, resource index, and structured run logs

## Shipped artifacts

- `yaos.exe` - desktop studio (Qt Quick front-end, default entry)
- `yaosd.exe` - headless local daemon / sidecar
- `yaos_base.lib` / `yaos_business.lib` - static libraries shared by both processes

## Key entry points

Top-level CLI subcommands dispatched by `src/main.cpp`:

- `yaos` / `yaos gui` - desktop studio
- `yaos init` - create `~/.yaos/config.json` and workspace
- `yaos status` - runtime health summary
- `yaos gateway` - start channel gateway
- `yaos daemon` - loopback IPC sidecar
- `yaos runtime-service` / `memory-service` / `control-service` - HTTP splits of the runtime
- `yaos agent --message "..."` - one-shot agent call
- `yaos provider-login|status|models|refresh|logout` - provider auth and catalog
- `yaos route-preview` / `submit-delegation` / `template-export|import|push|pull`
- `yaos gui-regression --case ...` - GUI regression harness

## Default endpoints

- Gateway: `0.0.0.0:18790`
- Runtime service: `http://127.0.0.1:18890`
- Memory service: `http://127.0.0.1:18891`
- Control service: `http://127.0.0.1:18892`

## Workspace

`~/.yaos/workspace` holds persistent state. Never treat it as ephemeral - GUI pages read directly from these files:

- `runtime/tasks.json`, `approvals.json`, `notifications.json`, `events.jsonl`, `logs/*.jsonl`, `control_node_health.json`
- `automations/flows.json`, `runs.json`; `cron/jobs.json`
- `sessions/*.jsonl`
- `memory/MEMORY.md`, `HISTORY.md`, `daily/*.md`, `daily/*.summary.md`
- `plugins/`, `skills/`

## Provider ID and naming rules

- Provider registry and CLI use **snake_case** canonical IDs (`azure_openai`, `github_copilot`, `openai_codex`, `volcengine`, `dashscope`, ...)
- Some GUI config slots still use camelCase for historical reasons - the canonical form is snake_case
- When adding a new provider, register it in `src/providers/ProviderRegistry.cpp` and `ProviderFactory.cpp`

## Default tool policy (security-first)

- `readFile`, `listDir`, `message` - `allow`
- `writeFile`, `spawn`, `cron`, `mcpCall`, `pluginCall` - `confirm` (human approval)
- `exec` - `deny`

Preserve this default posture when adding new tools.

## User-facing language

README, GUI pages, and most docs are in Simplified Chinese. Code, identifiers, log messages, and commit messages stay in English. When editing user-visible strings, match the surrounding language.
