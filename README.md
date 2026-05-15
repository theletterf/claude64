# Claude64

A Claude chat client for the Commodore 64.

Type a message on the C64, get a streaming response from Claude — over a real
(emulated) RS-232 serial connection.

```
 C64 (VICE)                proxy (Python)             Anthropic API
 ──────────────            ──────────────             ─────────────
 claude64.prg  ←─ IP232 ─→ claude_proxy.py  ←─ HTTPS ─→ claude-haiku
 Swiftlink drv    TCP       127.0.0.1:25232
```

The C64 side is a ~200-byte-per-turn chat UI written in C and compiled with
cc65. The proxy handles IP232 framing, conversation history, and streaming the
Anthropic Messages API back to the C64 one character at a time.


## Requirements

| Tool | Purpose |
|---|---|
| [cc65](https://cc65.github.io) (`cl65`) | Cross-compiler for the C64 |
| [VICE](https://vice-emu.sourceforge.io) (`x64sc`) | C64 emulator |
| Python 3.9+ | Proxy server |
| Anthropic API key | `ANTHROPIC_API_KEY` env var |


## Build

```sh
make
```

Produces `c64/build/claude64.prg` and copies the Swiftlink driver to
`c64/build/c64-swlink.ser`.


## Run

Start the proxy in one terminal:

```sh
export ANTHROPIC_API_KEY=sk-ant-...
python3 proxy/claude_proxy.py
```

Launch the emulator in another:

```sh
make run
```

VICE starts with the Swiftlink cartridge configured on the User Port, pointed
at `127.0.0.1:25232`. The C64 loads the driver from device 8 (served from
`c64/build/`), connects, and shows the chat prompt.


## Usage

Type a message and press **Return** to send. The response streams in as it
arrives. Press **RUN/STOP** to cancel the current input line. Press **F1** to
clear the screen.

Built-in debug commands:

| Command | What it does |
|---|---|
| `/rx` | Send the current line and hex-dump the raw bytes received |
| `/raw` | Same but print printable ASCII instead of hex |
| `/local` | Enter offline echo mode (no API calls); `/exit` to leave |


## Echo mode (no API key needed)

```sh
python3 proxy/claude_proxy.py --echo
```

The proxy echoes your input back without calling Anthropic. Useful for testing
the serial link and UI without spending tokens.


## License

MIT — see [LICENSE](LICENSE).
