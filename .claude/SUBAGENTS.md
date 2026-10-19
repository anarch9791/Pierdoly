# Creating Claude Code subagents

Source: https://code.claude.com/docs/en/sub-agents

A subagent is a specialised assistant with its own context window, system
prompt, tool allowlist and model. Claude delegates to it when a task matches
its `description`, and only the subagent's final summary returns to the main
conversation.

## Where to put them (so every device gets them)

| Location | Scope | Syncs across devices? |
| :- | :- | :- |
| `.claude/agents/<name>.md` in this repo | This project | **Yes** — committed to git; every clone, laptop, desktop and cloud session picks it up after `git pull` |
| `~/.claude/agents/<name>.md` | All projects on one machine | No — local to that machine |
| `claude --agents '{...}'` | One session | No |

**Rule for this repo: create subagents in `.claude/agents/`, commit them and
push.** Do not put project agents in `~/.claude/agents/`; they would exist on
one machine only. Changes to agent files are picked up within seconds (restart
Claude Code only if `.claude/agents/` did not exist when the session started).

## File format

Markdown with YAML frontmatter. The body is the subagent's system prompt — it
does **not** get the main Claude Code system prompt or the conversation
history, so the body must be self-contained.

```markdown
---
name: my-agent                 # required; lowercase-hyphenated, unique, no ":"
description: What it does and WHEN to use it. Add "Use proactively" to encourage automatic delegation.   # required
tools: Read, Grep, Glob, Bash  # optional allowlist; omit to inherit all tools
model: inherit                 # sonnet | opus | haiku | fable | full model ID | inherit
---

You are a ... When invoked:
1. ...
2. ...
Report back: ...
```

### Optional frontmatter fields

| Field | Use |
| :- | :- |
| `disallowedTools` | Denylist, e.g. `Write, Edit` for a read-only agent |
| `permissionMode` | `default`, `acceptEdits`, `auto`, `dontAsk`, `bypassPermissions`, `plan` |
| `maxTurns` | Cap on agentic turns |
| `effort` | `low` … `max` |
| `skills` | Skills to preload into the agent |
| `mcpServers` | MCP servers only this agent uses |
| `hooks` | `PreToolUse` / `PostToolUse` / `Stop` hooks active only for this agent |
| `memory` | `project` (recommended, `.claude/agent-memory/<name>/`, committed), `user`, or `local` |
| `isolation` | `worktree` — run in a throwaway git worktree |
| `background` | `true` — always run in the background |
| `color` | `red`, `blue`, `green`, `yellow`, `purple`, `orange`, `pink`, `cyan` |

Field names are camelCase. Files missing `name` or `description`, or with
invalid YAML, are skipped silently — run `claude --debug` to see why.

## How to use them

- Automatic: Claude picks an agent whose `description` matches the task.
- Explicit: "Use the `code-reviewer` subagent to check my changes."
- @-mention: `@agent-code-reviewer look at the last commit`.
- Whole session: `claude --agent code-reviewer`.
- Built-ins always available: `Explore` (read-only search), `Plan`, `general-purpose`.

## Checklist for a new subagent

1. One job per agent; name it after the job (`test-runner`, not `helper`).
2. Write a specific `description` — that is what routing uses. Keep it short.
3. Grant the fewest tools needed (reviewers: `Read, Grep, Glob, Bash`).
4. Pick the cheapest model that works (`haiku` for search/summaries).
5. Make the prompt self-contained: steps, project facts, and the exact report format.
6. Save to `.claude/agents/<name>.md`, then `git add .claude && git commit && git push`.
7. On other devices: `git pull` — the agent is available immediately.
