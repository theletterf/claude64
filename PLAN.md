# Claude64 — A Claude client for the Commodore 64

## Context

We're building a Claude chat client that runs on a Commodore 64 inside the
VICE emulator. The C64 cannot do TLS itself (6502 @ 1 MHz, 64 KB RAM) and
the Anthropic API requires HTTPS, so the C64 side speaks plain bytes over
an emulated serial link and a tiny host-side proxy terminates TLS and
relays to `api.anthropic.com`. The point is the experience: PETSCII-rendered
Claude responses streaming into a 40×25 screen, with the smallest possible
dependency footprint and no "magic black box" middleware.

User decisions locked in:
- **Network path**: Swiftlink + IP232 + thin proxy (lowest deps; streams).
- **Display**: live token-by-token streaming.
- **Target**: VICE emulator only for now; real hardware is a stretch goal.

## Architecture

```
+----------+   PETSCII / bytes    +----------+   IP232 over TCP   +-----------+   HTTPS   +------------+
|  C64     | <------------------> | VICE     | <----------------> |  Proxy    | <-------> | Anthropic  |
| (cc65)   |  via $DE00 Swiftlink | (x64sc)  |  127.0.0.1:25232   |  (Python) |   SSE     |  Messages  |
+----------+                      +----------+                    +-----------+           +------------+
```

- **C64 binary** uses cc65's stock Swiftlink driver to read/write bytes.
- **VICE** bridges the emulated 6551 ACIA at `$DE00` to a TCP socket via the
  IP232 protocol (`-rsdev2 127.0.0.1:25232 -rsdev2ip232 -acia1`).
- **Proxy** speaks IP232 (handles `0xFF`-escape framing + DCD assert) on
  one side, HTTPS + SSE on the other. It carries the API key, holds the
  conversation history, converts UTF-8 → ASCII, and emits text deltas as
  plain bytes wrapped in a tiny line protocol.

## Tech choices

| Concern | Choice | Rationale |
|---|---|---|
| C64 toolchain | **cc65** (`cl65 -t c64`) | De facto C compiler for 6502; ships Swiftlink + `conio` |
| Emulator | **VICE x64sc** | Cycle-accurate; built-in IP232 bridge; ships on macOS via Homebrew |
| Serial driver | **cc65 `c64-swlink.s`** | Already exists, ring buffer, NMI-driven; just call `ser_*` from C |
| Proxy language | **Python 3 stdlib only** | `socket` + `ssl` + `http.client` + `json` — no `pip install` needed |
| Wire framing C64↔proxy | **Line protocol on top of IP232** | Text lines + sentinel markers; trivial to parse on 6502 |
| Display | **cc65 `conio.h`** (`cputc`, `cputs`, `cclear`) | Handles PETSCII screen codes; mode 14 lowercase charset |
| Build host | macOS (this machine) | cc65 + VICE both via Homebrew |

Anthropic API:
- Endpoint: `POST https://api.anthropic.com/v1/messages` with `stream: true`.
- Headers: `x-api-key`, `anthropic-version: 2023-06-01`, `content-type: application/json`.
- Prompt caching on the system prompt (saves cost across turns).
- Model: `claude-haiku-4-5-20251001` initially (cheap, fast — matches the
  vibe), bump to `claude-sonnet-4-6` once it works.

## Wire protocol (proxy ↔ C64, inside IP232)

Tiny, line-oriented, ASCII-only. C64 parses by scanning for `\n` and a
2-character sentinel at line start.

Client → server:
```
U <user-prompt-bytes>\n
.\n                         (terminator — end of user turn)
```

Server → client:
```
S \n                        (assistant turn starting)
T <text-chunk>\n            (one or more text-delta lines)
E <reason>\n                (turn ended; reason ∈ end_turn|max_tokens|error)
```

`R <text>\n` is reserved for proxy-injected status (e.g., "connecting...",
"rate-limited, retrying").

Conversation history lives entirely on the proxy — the C64 only ever sends
the next user turn. Keeps C64 memory free for scrollback.

## Repo layout

```
claude64/
├── PLAN.md
├── Makefile                       # `make build run clean`
├── proxy/
│   ├── claude_proxy.py            # stdlib-only IP232 <-> HTTPS bridge
│   └── README.md                  # how to set ANTHROPIC_API_KEY
├── c64/
│   ├── src/
│   │   ├── main.c                 # entry, REPL loop
│   │   ├── net.c / net.h          # ser_open + framed I/O over Swiftlink
│   │   ├── ui.c  / ui.h           # scroll buffer, word-wrap, prompt rendering
│   │   ├── ascii.c / ascii.h      # ASCII<->PETSCII edge cases
│   │   └── proto.c / proto.h      # U/S/T/E line protocol state machine
│   └── build/                     # cl65 outputs claude64.prg here
├── scripts/
│   ├── run.sh                     # boots proxy + x64sc with correct flags
│   └── kill.sh
└── .gitignore
```

## Development phases

### Phase 0 — Toolchain + skeleton
Goal: prove the build/run loop.
- `brew install cc65 vice`
- `c64/src/main.c` prints "HELLO CLAUDE64" via `cputs`.
- `Makefile`: `build` target runs `cl65 -t c64 -O -o c64/build/claude64.prg`.
- `Makefile`: `run` target runs `x64sc -autostartprgmode 1 c64/build/claude64.prg`.
- **Done when**: VICE boots and shows the greeting.

### Phase 1 — Proxy (host-side)
Goal: a working HTTPS client testable from the host before any C64 wiring.
- `proxy/claude_proxy.py` (stdlib only):
  - Reads `ANTHROPIC_API_KEY` from env.
  - Listens on `127.0.0.1:25232` (IP232 framing: strip/escape `0xFF`, assert DCD-on `\xFF\x02`).
  - On connection: reads U/. user turn, POSTs to Anthropic with `stream: true`,
    parses SSE `content_block_delta` events, writes `T <text>\n` + `E <reason>\n`.
  - Maintains `messages` list across turns on the same TCP connection.
  - System prompt marked `"cache_control": {"type": "ephemeral"}` for prompt caching.
  - UTF-8 fold: smart quotes → `"` / `'`, em/en dash → `-`, ellipsis → `...`, else `?`.
- **Done when**: `nc 127.0.0.1 25232` + typing `U hello\n.\n` returns streaming `T` lines + `E end_turn\n`.

### Phase 2 — VICE network bridge
Goal: confirm bytes flow C64↔proxy through the emulator.
- `scripts/run.sh` starts proxy, then:
  ```sh
  x64sc -default \
    -acia1 -acia1dev 2 \
    -rsdev2 127.0.0.1:25232 -rsdev2ip232 -rsdev2baud 2400 \
    c64/build/claude64.prg
  ```
- Minimal C64 echo test: open serial, write `ping\n`, read bytes, `cputs`.
- **Done when**: C64 prints `pong\n` from proxy echo mode.

### Phase 3 — C64 networking primitives
Goal: clean serial I/O abstraction.
- `net.c`: wraps `ser_open` with Swiftlink driver; exposes `net_putc/net_puts/net_putln`, `net_getc/net_getln`.
- Non-blocking polling via `ser_get` (returns `SER_ERR_NO_DATA` when empty — ideal for streaming UX).
- **Done when**: proxy `R hello from proxy\n` line appears on screen.

### Phase 4 — Display + PETSCII
Goal: legible streaming output on 40×25.
- `cputc(14)` at startup switches to lowercase/uppercase charset.
- `ui.c`: word-wrap at col 40, ring scrollback, cursor advances + scrolls.
- Colors: assistant = light green, user = white, status = cyan.
- Edge cases: `\t` → 4 spaces, drop `\r`, map special symbols as needed.
- **Done when**: long prefab response renders cleanly with no artifacts.

### Phase 5 — Input
Goal: edit + submit a user turn.
- `cgetc()`-based line editor: printable keys, `INST/DEL` backspace, `RETURN` to submit, `RUN/STOP` to clear.
- ~200-char line buffer; visual word-wrap.
- **Done when**: typed text echoes, RETURN sends `U ...\n.\n` to proxy.

### Phase 6 — End-to-end conversation
Goal: real Claude turns.
- `main.c` REPL: input → send U-turn → receive state → parse S/T/E → render T chunks live → return to input.
- Status line: `R connecting...` while proxy dials Anthropic; clears on `S`.
- **Done when**: "what is 2+2" answered character-by-character on screen.

### Phase 7 — Polish
- Splash screen with PETSCII banner.
- SID beep on response complete.
- `F1` clears screen; `F3` cycles model (sends `C model haiku\n` to proxy).
- Transcript save to D64 via `fopen("0:CHAT,S,W")` (cc65 IEC stdio).
- README with 30-second demo recipe.

### Stretch
- Real-hardware port via WiC64 (rewrite `net.c`; `proto.c` and `ui.c` unchanged).
- Tool use forwarded to host (sandboxed shell, calculator).
- ANSI→PETSCII for code blocks (box-drawing chars).

## Critical references

- `cc65/libsrc/c64/ser/c64-swlink.s` — Swiftlink driver shipped with cc65; call `ser_*`, don't reimplement.
- VICE RS232 wiki: https://vice-emu.pokefinder.org/wiki/RS232 — IP232 framing rules.
- cc65 `conio.h` — PETSCII screen handling.
- Anthropic Messages API: https://docs.anthropic.com/en/api/messages.

Files to create (target sizes):
- `proxy/claude_proxy.py` — <300 LOC
- `c64/src/main.c`, `net.c`, `ui.c`, `proto.c` — <200 LOC each

## Verification

End-to-end smoke test:
1. `export ANTHROPIC_API_KEY=sk-...`
2. `make build`
3. `./scripts/run.sh` — proxy starts, VICE boots, splash appears.
4. Type `hello, who are you?` + RETURN.
5. Watch text stream across 40-column screen.
6. Follow-up turn to verify proxy retained context.
7. Kill proxy; C64 shows `R disconnected`, doesn't hang.

Component tests:
- Proxy: `nc 127.0.0.1 25232` driving U/./E turns by hand.
- C64 serial: `make echo-test` against proxy in echo mode.
- IP232 framing: `python3 -m unittest` on escape/unescape helpers.

Cost guardrail: `max_tokens: 512` during development; raise when stable.
Prompt caching on system prompt keeps per-turn cost sub-cent on Haiku.
