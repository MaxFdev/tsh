void eval(char *cmdline)
{
    // set up local variables
    char *argv[MAXARGS];
    int bg;
    pid_t pid;
    sigset_t mask;

    // parse input and get bg indicator
    bg = parseline(cmdline, argv);
    if (argv[0] == NULL)
        return;

    // builtin cmd check
    if (!builtin_cmd(argv))
    {
        // Block SIGCHLD
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);
        sigprocmask(SIG_BLOCK, &mask, NULL);

        // Fork a child process
        if ((pid = fork()) == 0)
        {
            // Child process
            // Unblock SIGCHLD
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            // Set new process group
            setpgid(0, 0);

            // Execute the command
            if (execve(argv[0], argv, environ) < 0)
            {
                printf("%s: Command not found\n", argv[0]);
                exit(1);
            }
        }

        // Unblock SIGCHLD
        sigprocmask(SIG_UNBLOCK, &mask, NULL);

        // add the job to the job list
        addjob(jobs, pid, bg ? BG : FG, cmdline);

        // deal with background vs foreground
        if (!bg)
        {
            // wait for the job to finish in foreground
            waitfg(pid);
        }
        else
        {
            // print a confirmation for background job
            printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
        }
    }
}