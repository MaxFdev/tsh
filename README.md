# Tiny Shell Project

### Description:

This shell was written entirely in C.  
This project was for school and was meant to show mastery of signal handling and process management.

---

### How to compile:

1. navigate to `/shell`
2. run `make`

---

### How to run:

1. navigate to `/`
2. run `tsh`
   - flag `-h` = print help
   - flag `-v` = print diagnostics
   - flag `-p` = do not emit command prompt
3. using tsh:
   - `path/to/commandOrProgram [args]` = run an external command or program (end with `&` to run in background)
   - `quit` / `cmd/ctrl + d` = exit shell
   - `jobs` = list jobs
   - `bg` = run job in background
   - `fg` = run job in foreground

---

### How to test:

1. navigate to `/shell`
2. run `./tester.sh`
   - make sure it has executable permissions with `chmod +x tester.sh`
3. validate the results.txt file
   - everything runs to the project standards if the differences are only in the run commands and process IDs (including when `ps` is run)
