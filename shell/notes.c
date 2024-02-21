#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE 1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
char sbuf[MAXLINE];      /* for composing sprintf messages */

struct job_t
{                          /* The job struct */
    pid_t pid;             /* job PID */
    int jid;               /* job ID [1, 2, ...] */
    int state;             /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE]; /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

// Function to evaluate the command line that the user has just typed in
void eval(char *cmdline)
{
    // Declare an array of character pointers to hold the arguments of the command
    char *argv[MAXARGS];

    // Declare an integer to indicate whether the command should be run in the background
    int bg;

    // Declare a process ID
    pid_t pid;

    // Declare a signal set
    sigset_t mask;

    // Parse the command line into arguments and determine whether the command should be run in the background
    bg = parseline(cmdline, argv);

    // If no command was entered (argv[0] is NULL), return immediately
    if (argv[0] == NULL)
        return;

    // Check if the command is a built-in command using builtin_cmd
    if (!builtin_cmd(argv)) {
        // Initialize the signal set to contain only SIGCHLD
        sigemptyset(&mask);
        sigaddset(&mask, SIGCHLD);

        // Block SIGCHLD signals
        sigprocmask(SIG_BLOCK, &mask, NULL);

        // Create a new process using fork
        if ((pid = fork()) == 0) { // Child process
            // Set a new process group ID for the child
            setpgid(0, 0);

            // Unblock SIGCHLD signals
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            // Attempt to execute the command using execve
            if (execve(argv[0], argv, environ) < 0) {
                // If the command cannot be executed, print an error message and exit
                printf("%s: Command not found.\n", argv[0]);
                exit(0);
            }
        }

        // Parent process
        if (!bg) { // If the command should be run in the foreground
            // Add the job to the foreground
            addjob(jobs, pid, FG, cmdline);

            // Unblock SIGCHLD signals
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            // Wait for this job to complete
            waitfg(pid);
        } else { // If the command should be run in the background
            // Add the job to the background
            addjob(jobs, pid, BG, cmdline);

            // Unblock SIGCHLD signals
            sigprocmask(SIG_UNBLOCK, &mask, NULL);

            // Print the job's details
            printf("[%d] (%d) %s", pid2jid(pid), pid, cmdline);
        }
    }

    // Return to the caller
    return;
}