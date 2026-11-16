Run `python3 -B testing/vtshell/interrupt.py` from the repository root.

The test compiles the production completion-prompt builder, starts each available
shell (zsh, bash and dash) in a real PTY, and uses far2l's source-and-marker
protocol. It checks successful and failed commands, Ctrl+C during a loop and an
external command, and subsequent commands in the same shell session. Missing
shells are reported explicitly. Shell startup files are disabled.

`integration/test.js` uses the existing Go smoke harness and a `TESTING=YES`
far2l binary. Run `testing/far2l-smoke /path/to/far2l testing/vtshell/integration`
after building the harness in `testing/src`. It checks that Ctrl+C returns to
the actual panels, that F9 opens far2l's menu, and that subsequent commands work
with bash and zsh. Run it from a temporary copy of the tests if desired.
