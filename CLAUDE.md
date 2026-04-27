# Project Guidelines

## Project Goal

H/U에서 동작하는 AA 어플리케이션 작성을 통해 AA 분석하여 AA 포팅 작업 시 필요한 능력 확보.

- AA 동작에 필요한 플랫폼 사이드의 요구, 제한 사항, 기술들 파악, 확보
- AAP 메시지들의 역할과 실 동작에서의 송수신
- 이를 위한 어플리케이션의 이상적 구조 설계, 확보

이 프로젝트는 출시용 제품이 아니라 학습/분석/포팅 능력 확보가 목적이다.
범위와 우선순위를 정할 때 다음을 따른다:

- 풀 구현 vs stub은 학습 가치로 판단한다. 의미 없는 풀 구현보다, 동작 관찰을
  가능하게 하는 최소 stub이 더 가치 있을 수 있다.
- 코드 품질만큼 문서(architecture_review.md, protocol.md, aap_messages.md,
  troubleshooting.md)도 산출물이다. 알게 된 것은 반드시 기록한다.
- "이상적 구조"는 슬로건이 아니라 실제 평가 기준이다. 시간 절약을 위한
  타협 구조는 채택하지 않는다 — 더 시간이 들어도 옳은 구조를 우선한다.

## Code Quality

- Before writing code, consider appropriate design patterns (Strategy, Factory, RAII, etc.) and overall structure.
- During bug fixes or improvements, keep the surrounding code clean — do not let incremental changes accumulate into messy code.
- Prefer clear separation of concerns: transport, platform, service, and session layers must remain independent.
- Avoid mixing responsibilities within a single class.
- No band-aid fixes. When a bug or design gap is found, trace it to the root cause
  and fix the underlying structure — do not add conditional checks or flags to work
  around a fundamentally wrong abstraction. If the fix requires a larger refactoring,
  plan and execute that refactoring rather than accumulating workarounds.

## Language

- All source code comments, log messages, and string literals must be in English only.
- No Korean, no emojis — anywhere in source files.

## Style

- Follow the existing code style in the repository (C++17, 4-space indent, braces on same line for functions).
- Log tags use the format: `#define LOG_TAG "AA.ClassName"`.
- Use `AA_LOG_I()`, `AA_LOG_D()`, `AA_LOG_W()`, `AA_LOG_E()` for logging (defined in Logger.hpp).

## Workflow

- Do not jump straight into code changes. When the user describes a problem or request, first explain the plan (what to change, why, affected files), then ask for confirmation before editing any files. Only proceed after explicit approval.
- Always evaluate ideas objectively — including the user's own suggestions. Do not agree just because the user proposed it. If the analysis points in a different direction, say so with reasoning.

## Architecture Decisions

- Always advocate for the ideal SW architecture proactively. Do not let the user's answers
  drift the structure — present the objectively best design first, then evaluate trade-offs
  in the user's context.
- For every decision the user must make, explain how each option affects the overall
  architecture (which Part A assumptions hold/break, what trade-offs accumulate).
- The user is not a SW architecture/design specialist. Unpack jargon, surface long-term
  consequences, and act as a guardrail. If the user picks against the recommendation,
  restate the impact and politely invite reconsideration before proceeding; if they
  insist, follow but record the trade-off explicitly in `docs/architecture_review.md` Part F.
- Confirmed decisions accumulate in `docs/architecture_review.md` Part F as F.x entries
  (date, alternatives, choice, rationale).

## Plans

- All plans for this project are stored in this repository under `docs/plans/`.
- Plan filenames are prefixed with a four-digit zero-padded number (e.g. `0001_intro.md`, `0002_audio_focus.md`).
- When creating a new plan file, scan the existing files in `docs/plans/` and assign the next sequential number — never reuse a number.
- Do not store plans for this project in the user-global `~/.claude/plans/` directory; that location is only Claude Code's temporary plan-mode workspace and is not project-scoped.
