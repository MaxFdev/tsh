/*
 * tsh - Make your own shell
 *
 * Max Franklin
 */
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
/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Custom function for writing safely */
void safe_write(char *str, int size);
void safe_write_int(int value);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

/*
 * main - The shell's main routine
 */
int main(int argc, char **argv)
{
    char c;
    char cmdline[MAXLINE];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
        case 'h': /* print help message */
            usage();
            break;
        case 'v': /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':            /* don't print a prompt */
            emit_prompt = 0; /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT, sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler);

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1)
    {

        /* Read command line */
        if (emit_prompt)
        {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin))
        { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
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
    {
        return;
    }

    // builtin cmd check
    if (!builtin_cmd(argv))
    {
        // set up a signal block for SIGCHLD
        if (sigemptyset(&mask) != 0)
        {
            unix_error("sigemptyset error");
        }
        if (sigaddset(&mask, SIGCHLD) != 0)
        {
            unix_error("sigaddset error");
        }
        if (sigprocmask(SIG_BLOCK, &mask, NULL) != 0)
        {
            unix_error("sigprocmask blocking error");
        }

        // fork the child process
        if ((pid = fork()) == 0)
        {
            // child process sets a new group id for itself
            setpgid(0, 0);

            // Execute the command
            if (execve(argv[0], argv, environ) < 0)
            {
                printf("%s: Command not found\n", argv[0]);
                exit(1);
            }
        }

        // add the job to the job list
        addjob(jobs, pid, bg ? BG : FG, cmdline);

        // unblock SIGCHLD after the job is added
        if (sigprocmask(SIG_UNBLOCK, &mask, NULL) != 0)
        {
            unix_error("sigprocmask unblocking error");
        }

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

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv)
{
    static char array[MAXLINE]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
        buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'')
    {
        buf++;
        delim = strchr(buf, '\'');
    }
    else
    {
        delim = strchr(buf, ' ');
    }

    while (delim)
    {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;
        if (*buf == '\'')
        {
            buf++;
            delim = strchr(buf, '\'');
        }
        else
        {
            delim = strchr(buf, ' ');
        }
    }

    argv[argc] = NULL;

    if (argc == 0) /* ignore blank line */
        return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc - 1] == '&')) != 0)
    {
        argv[--argc] = NULL;
    }

    return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv)
{
    if (strcmp(argv[0], "quit") == 0)
    {
        // exit the terminal
        exit(0);
    }
    else if (strcmp(argv[0], "jobs") == 0)
    {
        // list the jobs
        listjobs(jobs);
    }
    else if (strcmp(argv[0], "bg") == 0 || strcmp(argv[0], "fg") == 0)
    {
        // hand off to do_bgfg
        do_bgfg(argv);
    }
    else
    {
        // Not a builtin command
        return 0;
    }

    // Was a builtin command
    return 1;
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{
    // set up the local variables
    int jid;
    pid_t pid;
    struct job_t *job;

    // confirm whether there is a second argument
    if (argv[1] == NULL)
    {
        // print error for not having a second argument and return
        printf("%s command requires PID or %cjobid argument\n", argv[0], '%');
        return;
    }
    else
    {
        // check if the second arg is a job
        if (argv[1][0] == '%')
        {
            // get the job number and job
            jid = atoi(argv[1] + 1);
            job = getjobjid(jobs, jid);

            // check that the job exists
            if (job == NULL)
            {
                printf("%s: No such job\n", argv[1]);
                return;
            }
        }
        // check for a process id number
        else if ((pid = atoi(argv[1])) > 0)
        {
            // get the job based on the process id
            job = getjobpid(jobs, pid);

            // check that the job exists
            if (job == NULL)
            {
                printf("(%s): No such process\n", argv[1]);
                return;
            }
        }
        else
        {
            // print error for not being a job id or pid and return
            printf("%s: argument must be a PID or %cjobid\n", argv[0], '%');
            return;
        }

        // check if the process is moving to the background or foreground
        if (strcmp(argv[0], "bg") == 0)
        {
            // send the continue signal to the process
            if (kill(-(job->pid), SIGCONT) == 0)
            {
                // change the job state to background
                job->state = BG;

                // print a confirmation that the job is running
                printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
            }
            else
            {
                unix_error("continue in background error");
            }
        }
        else
        {
            // continue in the foreground
            if (kill(-(job->pid), SIGCONT) == 0)
            {
                // set the job state to foreground
                job->state = FG;

                // wait for the job to finish in the foreground
                waitfg(job->pid);
            }
            else
            {
                unix_error("continue in the foreground error");
            }
        }
    }
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
    // get and store the job
    struct job_t *job = getjobpid(jobs, pid);

    // return if the job has already completed
    if (job == NULL)
    {
        return;
    }

    // wait for job to leave foreground and check that process is the same
    while (job->state == FG && job->pid == pid)
    {
        sleep(1);
    }
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig)
{
    // set up all local variables
    struct job_t *job;
    pid_t pid;
    int jid;
    int status;

    // check if any child process changes state
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0)
    {
        // get the job id for later
        jid = pid2jid(pid);

        // check if the process finished
        if (WIFEXITED(status))
        {
            // delete the job
            deletejob(jobs, pid);
        }
        // check if the process was interrupted
        else if (WIFSIGNALED(status))
        {
            // delete the job and print the confirmation
            if (deletejob(jobs, pid))
            {
                safe_write("Job [", 5);
                safe_write_int(jid);
                safe_write("] (", 3);
                safe_write_int(pid);
                safe_write(") terminated by signal ", 23);
                safe_write_int(WTERMSIG(status));
                safe_write("\n", 1);
            }
        }
        // check if the process was stopped
        else if (WIFSTOPPED(status))
        {
            // get the job for processing
            job = getjobpid(jobs, pid);

            // check the job exists and print a stop confirmation
            if (job != NULL)
            {
                job->state = ST;
                safe_write("Job [", 5);
                safe_write_int(jid);
                safe_write("] (", 3);
                safe_write_int(pid);
                safe_write(") stopped by signal ", 20);
                safe_write_int(WSTOPSIG(status));
                safe_write("\n", 1);
            }
        }
    }
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenever the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
    // get the current fg job's pid
    pid_t fg_pid = fgpid(jobs);

    // check that the process exists
    if (fg_pid > 0)
    {
        // send the interrupt signal to the process
        if (kill(-fg_pid, SIGINT) == -1)
        {
            unix_error("Failed to interrupt");
        }
    }
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
    // get the current fg job's pid
    pid_t fg_pid = fgpid(jobs);

    // check that the process exists
    if (fg_pid > 0)
    {
        // send the stop signal to the process
        if (kill(-fg_pid, SIGTSTP) == -1)
        {
            unix_error("Failed to stop");
        }
    }
}

/*
 * safe_write - Uses the write system call to print out messages in
 *      an async-signal-safe way.
 */
void safe_write(char *str, int size)
{
    // use write to safely print a message
    if (write(STDOUT_FILENO, str, size) == -1)
    {
        unix_error("Write error");
    }
}

/*
 * safe_write_int - On the fly calculates a string from a given int
 *      and prints it using better print.
 */
void safe_write_int(int value)
{
    // buffer to hold the string
    char buffer[20];
    int i = 0;

    // check for negative
    if (value < 0)
    {
        // print the '-' and convert to positive
        safe_write("-", 1);
        value = -value;
    }

    // convert the integer to a string in reverse order
    do
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    } while (value > 0);

    // safely print the string in reverse order
    while (i > 0)
    {
        safe_write(&buffer[--i], 1);
    }
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job)
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs)
{
    int i, max = 0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline)
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == 0)
        {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose)
            {
                printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
        }
    }
    printf("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
        return 0;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid == pid)
        {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs) + 1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].state == FG)
            return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid)
{
    int i;

    if (pid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
            return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid)
{
    int i;

    if (jid < 1)
        return NULL;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid)
{
    int i;

    if (pid < 1)
        return 0;
    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].pid == pid)
        {
            return jobs[i].jid;
        }
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
    int i;

    for (i = 0; i < MAXJOBS; i++)
    {
        if (jobs[i].pid != 0)
        {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state)
            {
            case BG:
                printf("Running ");
                break;
            case FG:
                printf("Foreground ");
                break;
            case ST:
                printf("Stopped ");
                break;
            default:
                printf("listjobs: Internal error: job[%d].state=%d ",
                       i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void)
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler)
{
    struct sigaction action, old_action;

    action.sa_handler = handler;
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */

    if (sigaction(signum, &action, &old_action) < 0)
        unix_error("Signal error");
    return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig)
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
