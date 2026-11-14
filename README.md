# Mini Shell

A small Unix shell written in C, modelled on bash. It runs programs, keeps a table of background jobs and handles Ctrl+C and Ctrl+Z without ever killing itself.

Built for the Operating Systems I course (*Sistemes Operatius I*) of the Computer Engineering degree at the University of the Balearic Islands (UIB), 2025-26. Code comments are in Catalan, the language of the course.

## Features

- **External commands** from `$PATH`, run with `fork` + `execvp`.
- **Built-in commands:** `cd`, `export`, `source`, `jobs`, `fg`, `bg` and `exit` (or Ctrl+D).
- **Background jobs** with `&`, kept in a jobs table.
- **Job control:** Ctrl+Z stops the foreground job, and `bg` / `fg` resume it with `SIGCONT`.
- **Ctrl+C** ends the foreground job with `SIGTERM`, never the shell.
- **Output redirection** with `>`, using `open` + `dup2`.
- **Scripts and comments:** `source file` runs a file line by line, and anything after `#` is ignored.
- **No zombie processes:** a `SIGCHLD` handler reaps every finished child with `waitpid(-1, …, WNOHANG)`.

## Demo

```text
$ echo Hola desde el mini shell
Hola desde el mini shell
$ export SALUDO=hola
$ printenv SALUDO
hola
$ echo esto va a un fichero > salida.txt
$ cat salida.txt
esto va a un fichero
$ sleep 1 &
[1] 230 E sleep 1 &
$ sleep 2
Terminado PID 230 (sleep 1 &) en jobs_list[1] con status 0
$ sleep 30
^Z
[1] 241 D sleep 30
$ bg 1
[1] 241 E sleep 30 &
$ fg 1
sleep 30
^C
$ comando_que_no_existe
comando_que_no_existe: No such file or directory
$ exit
Bye Bye
```

In the jobs table, `E` means running and `D` stopped. The shell's own messages are in Spanish.

## How it works

```text
read_line ─▶ parse_args ─▶ built-in? ── yes ─▶ run it inside the shell
                               │
                               no
                               ▼
                             fork ─┬─▶ child:  apply '>' if present, then execvp
                                   └─▶ parent: foreground → wait in pause()
                                               background → add to the jobs table
```

- `jobs_list[0]` holds the foreground process. The shell waits in `pause()` until the `SIGCHLD` handler, or Ctrl+Z, clears that slot.
- Children ignore `SIGINT` and `SIGTSTP`. The shell's own handlers decide what to send to the foreground job (`SIGTERM` or `SIGSTOP`), so Ctrl+C and Ctrl+Z never reach the shell itself.

The shell was built in six incremental levels, each one on top of the previous. They are kept in [`niveles/`](niveles/):

| Level | Adds |
|---|---|
| 1 | Prompt, reading and splitting the command line |
| 2 | `cd` and `export` |
| 3 | `source` and external commands (`fork` + `execvp`) |
| 4 | Reaping children with `SIGCHLD`, and Ctrl+C |
| 5 | Background jobs, `jobs` and Ctrl+Z |
| 6 | `fg`, `bg` and `>` redirection |

## Build and run

Requires Linux (or WSL) and `gcc`.

```bash
make            # builds ./my_shell
./my_shell
make niveles    # optional: builds nivel1 … nivel6
```

Each level prints debug traces that can be switched on with the `DEBUGN1` … `DEBUGN6` macros at the top of its source file.

## Limitations

- No pipes (`|`), input redirection (`<`) or append (`>>`).
- No quoting or variable expansion: arguments are split on spaces, so `echo $HOME` prints `$HOME`.
- Signals are set up with `signal()` rather than `sigaction()`.
