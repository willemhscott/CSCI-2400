//
// tsh - A tiny shell program with job control
//
// <Put your name and login ID here>
//
 
using namespace
std;
 
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <string>
 
#include "globals.h"
#include "jobs.h"
#include "helper-routines.h"
 
extern char **environ;
 
//
// Needed global variable definitions
//
 
static char prompt[] = "tsh> ";
int verbose = 0;
int stopped = 0;
pid_t ctask = 0;
 
//
// You need to implement the functions eval, builtin_cmd, do_bgfg,
// waitfg, sigchld_handler, sigstp_handler, sigint_handler
//
// The code below provides the "prototypes" for those functions
// so that earlier code can refer to them. You need to fill in the
// function bodies below.
//
 
void eval(char *cmdline);
 
int builtin_cmd(char **argv);
 
void do_bgfg(char **argv);
 
void waitfg(pid_t pid);
 
void sigchld_handler(int sig);
 
void sigtstp_handler(int sig);
 
void sigint_handler(int sig);
 
void job_cleanup();
 
//
// main - The shell's main routine
//
int main(int argc, char *argv[]) {
    int emit_prompt = 1; // emit prompt (default)
 
    //
    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    //
    dup2(1, 2);
 
    /* Parse the command line */
    char c;
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
            case 'h':             // print help message
                usage();
                break;
            case 'v':             // emit additional diagnostic info
                verbose = 1;
                break;
            case 'p':             // don't print a prompt
                emit_prompt = 0;  // handy for automatic testing
                break;
            default:
                usage();
        }
    }
 
    //
    // Install the signal handlers
    //
 
    //
    // These are the ones you will need to implement
    //
    Signal(SIGINT, sigint_handler);   // ctrl-c
    Signal(SIGTSTP, sigtstp_handler);  // ctrl-z
    Signal(SIGCHLD, sigchld_handler);  // Terminated or stopped child
 
    //
    // This one provides a clean way to kill the shell
    //
    Signal(SIGQUIT, sigquit_handler);
 
    //
    // Initialize the job list
    //
    initjobs(jobs);
 
    //
    // Execute the shell's read/eval loop
    //
    for (;;) {
        //
        // Read command line
        //
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
 
        char cmdline[MAXLINE];
 
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin)) {
            app_error("fgets error");
        }
        //
        // End of file? (did user type ctrl-d?)
        //
        if (feof(stdin)) {
            fflush(stdout);
            exit(0);
        }
 
        //
        // Evaluate command line
        //
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }
 
    exit(0); //control never reaches here
}
 
/////////////////////////////////////////////////////////////////////////////
//
// eval - Evaluate the command line that the user has just typed in
//
// If the user has requested a built-in command (quit, jobs, bg or fg)
// then execute it immediately. Otherwise, fork a child process and
// run the job in the context of the child. If the job is running in
// the foreground, wait for it to terminate and then return.  Note:
// each child process must have a unique process group ID so that our
// background children don't receive SIGINT (SIGTSTP) from the kernel
// when we type ctrl-c (ctrl-z) at the keyboard.
//
void eval(char *cmdline) {
    /* Parse command line */
    //
    // The 'argv' vector is filled in by the parseline
    // routine below. It provides the arguments needed
    // for the execve() routine, which you'll need to
    // use below to launch a process.
    //
    char *argv[MAXARGS];
   
 
    //
    // The 'bg' variable is TRUE if the job should run
    // in background mode or FALSE if it should run in FG
    //
    int bg = parseline(cmdline, argv);
    if (bg) {
        bg = BG;
    } else {
        bg = FG;
    }
    if (argv[0] == NULL)
        return;   /* ignore empty lines */
   
    int bc = builtin_cmd(argv);
    pid_t cpid = fgpid(jobs); // return the new fg process or 0
 
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
   
    if (!bc) {
        if (bg == BG) {
            sigprocmask(SIG_BLOCK, &mask, NULL); // block if bg
        }
 
        if (cpid == 0) { // if its 0, then we need to execute the executable
            cpid = fork();
        }
 
        if (cpid == 0) { // if its still 0, then this is the child process
            // catch all those juicy signals
            sigemptyset(&mask);
            sigprocmask(SIG_UNBLOCK, &mask, NULL);
            setpgid(0,0); // setpgid(cpid,0);
            if(execve(argv[0],argv,environ) < 0){ // exit from child immediately if exec errors
                printf("%s: Command not found\n", argv[0]);
                exit(0);
            }
        } else { // otherwise we're the parent and need to update the job list
//             printf("Added brand new job!\n");
            addjob(jobs, cpid, bg, cmdline);
           
//             sigemptyset(&mask);
            sigprocmask(SIG_UNBLOCK, &mask, NULL); // dont if not
            job_t* j = getjobpid(jobs, cpid);
            if (bg == FG) { // we only need to wait if its an executable file, everything else runs instantly
                waitfg(cpid);
            } else {
                printf("[%d] (%d) %s",j->jid,j->pid,j->cmdline);
            }
        }
    }
    return;
}
 
 
/////////////////////////////////////////////////////////////////////////////
//
// builtin_cmd - If the user has typed a built-in command then execute
// it immediately. The command name would be in argv[0] and
// is a C string. We've cast this to a C++ string type to simplify
// string comparisons; however, the do_bgfg routine will need
// to use the argv array as well to look for a job number.
//
int builtin_cmd(char **argv) {
    string cmd(argv[0]);
//     printf("Builtin cmd! %s\n", argv[0]);
//     printf("%d", cmd.compare("jobs"));
    if (!cmd.compare("quit")) {
        exit(0);
    }
    if (!cmd.compare("jobs")) {
        listjobs(jobs);
    } else if (!cmd.compare("bg")) {
        do_bgfg(argv);
    } else if (!cmd.compare("fg")) {
        do_bgfg(argv);
    } else if (!cmd.compare("killall")) {
        job_cleanup();
    } else {
        return 0;
    }
   
    return 1;     /* not a builtin command */
}
 
/////////////////////////////////////////////////////////////////////////////
//
// do_bgfg - Execute the builtin bg and fg commands
//
void do_bgfg(char **argv) {
    struct job_t *jobp = NULL;
 
    /* Ignore command if no argument */
    if (argv[1] == NULL) {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }
 
    /* Parse the required PID or %JID arg */
    if (isdigit(argv[1][0])) {
        pid_t pid = atoi(argv[1]);
        if (!(jobp = getjobpid(jobs, pid))) {
            printf("(%d): No such process\n", pid);
            return;
        }
    } else if (argv[1][0] == '%') {
        int jid = atoi(&argv[1][1]);
        if (!(jobp = getjobjid(jobs, jid))) {
            printf("%s: No such job\n", argv[1]);
            return;
        }
    } else {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }
 
    //
    // You need to complete rest. At this point,
    // the variable 'jobp' is the job pointer
    // for the job ID specified as an argument.
    //
    // Your actions will depend on the specified command
    // so we've converted argv[0] to a string (cmd) for
    // your benefit.
    //
    string cmd(argv[0]);
    if (jobp->state == ST)
        kill(-jobp->pid, SIGCONT);
//         pause();
   
    if (!cmd.compare("bg")) {
        printf("[%d] (%d) %s", jobp->jid, jobp->pid, jobp->cmdline);
        jobp->state = BG;
    } else {
//         printf("BOLO!");
        waitfg(jobp->pid);
    }
 
    return;
}
 
/////////////////////////////////////////////////////////////////////////////
//
// waitfg - Block until process pid is no longer the foreground process
//
void waitfg(pid_t pid) {
    job_t *j = getjobpid(jobs, pid);
    j->state = FG;
    while (1) {
//         printf("waited!\n");
//         listjobs(jobs);
        pause();
//         printf("%d %d\n", fgpid(jobs), pid);
        if (fgpid(jobs) != pid) {
            return;
        }
        sleep(.1);
    }
}
 
/////////////////////////////////////////////////////////////////////////////
//
// Signal handlers
//
 
 
/////////////////////////////////////////////////////////////////////////////
//
// sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
//     a child job terminates (becomes a zombie), or stops because it
//     received a SIGSTOP or SIGTSTP signal. The handler reaps all
//     available zombie children, but doesn't wait for any other
//     currently running children to terminate.
//
void sigchld_handler(int sig) {
//     printf("Received signal: %d - %s\n", sig, "sigchld");
    int status = 3;
    pid_t exited = 0;
    while ((exited = waitpid(-1, &status, WUNTRACED | WNOHANG)) > 0) {
//         printf("Received signal: %d - %s. Exit status: %d\n", sig, "sigchld", status);
 
        job_t* j = getjobpid(jobs, exited);
        if (WIFSTOPPED(status) || status == 20) {
            if (j->state != ST)
                printf("Job [%d] (%d) stopped by signal %d\n", j->jid, exited, 20);
            j->state = ST;
            return;
        }
       
        else if (WIFSIGNALED(status)) {
            printf("Job [%d] (%d) terminated by signal %d\n", j->jid, exited, WTERMSIG(status));
        }
       
        deletejob(jobs, exited);
    }
}
 
/////////////////////////////////////////////////////////////////////////////
//
// sigint_handler - The kernel sends a SIGINT to the shell whenver the
//    user types ctrl-c at the keyboard.  Catch it and send it along
//    to the foreground job.
//
void sigint_handler(int sig) {
//     printf("Received signal: %d - %s\n", sig, "sigint");
    pid_t jpid = fgpid(jobs);
//     printf("JPID: %d", jpid);
    if (jpid != 0){
        // send sigint to child
        job_t *j = getjobpid(jobs, jpid);
        kill(-jpid, SIGINT);
//         printf("Job [%d] (%d) terminated by signal %d\n", j->jid, j->pid, sig);
//         fflush(stdout);
        pause();
        return;
    }
//     job_cleanup();
//     exit(0);
}
 
/////////////////////////////////////////////////////////////////////////////
//
// sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
//     the user types ctrl-z at the keyboard. Catch it and suspend the
//     foreground job by sending it a SIGTSTP.
//
void sigtstp_handler(int sig) {
//     printf("Received signal: %d - %s\n", sig, "sigtstp");
    pid_t jpid = fgpid(jobs);
//     printf("JPID: %d", jpid);
    if (jpid != 0){
        // send sigtstp to child
        job_t *j = getjobpid(jobs, jpid);
        j->state = ST;
        kill(-jpid, SIGTSTP);
        printf("Job [%d] (%d) stopped by signal %d\n", j->jid, j->pid, sig);
        fflush(stdout);
//         pause();
        return;
    }
    job_cleanup();
    exit(0);
}
 
/*********************
 * End signal handlers
 *********************/
 
void job_cleanup() {
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            kill(-jobs[i].pid, SIGTERM);
            pause();
        }
    }
}