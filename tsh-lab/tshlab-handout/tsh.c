/* 
 * tsh - A tiny shell program with job control
 * 
 * Name      - Abhishek Bhardwaj
 * Andrew Id - abhisheb
 */


#define DEBUG_PRINT_ENABLED 1
#if DEBUG_PRINT_ENABLED 
#define DEBUG printf
#else
#define DEBUG(format, args...) ((void)0)
#endif


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>

/* Misc manifest constants */
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF         0   /* undefined */
#define FG            1   /* running in foreground */
#define BG            2   /* running in background */
#define ST            3   /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Parsing states */
#define ST_NORMAL   0x0   /* next token is an argument */
#define ST_INFILE   0x1   /* next token is the input file */
#define ST_OUTFILE  0x2   /* next token is the output file */


/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t job_list[MAXJOBS]; /* The job list */

struct cmdline_tokens {
    int argc;               /* Number of arguments */
    char *argv[MAXARGS];    /* The arguments list */
    char *infile;           /* The input file */
    char *outfile;          /* The output file */
    enum builtins_t {       /* Indicates if argv[0] is a builtin command */
        BUILTIN_NONE,
        BUILTIN_QUIT,
        BUILTIN_JOBS,
        BUILTIN_BG,
        BUILTIN_FG} builtins;
};


int fg_pid ; /* pid of current foreground group */
pid_t  tsh_pid; /* for storing shell pid */

/* End global variables */


/* Function prototypes */
void eval(char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, struct cmdline_tokens *tok); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *job_list);
int maxjid(struct job_t *job_list); 
int addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *job_list, pid_t pid); 
pid_t fgpid(struct job_t *job_list);
struct job_t *getjobpid(struct job_t *job_list, pid_t pid);
struct job_t *getjobjid(struct job_t *job_list, int jid); 
int pid2jid(pid_t pid); 
pid_t jid2pid(int jid);
void listjobs(struct job_t *job_list, int output_fd);
void printjob(pid_t pid,int output_fd); 
void printjob_jid(int jid,int output_fd); 
void print_sigint_job(struct job_t *job_list,pid_t pid,int signal,int output_fd);           
void print_sigtstp_job(struct job_t *job_list,pid_t pid,int signal,int output_fd);
void change_job_state(struct job_t *job_list,pid_t pid,int new_state);
void change_job_state_jid(struct job_t *job_list,int jid,int new_state);
void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);


pid_t Fork();
void Execve(char *program,char **argv,char **env);
int Sigemptyset(sigset_t *set);
int Sigaddset(sigset_t *set,int signum);
int Sigprocmask(int how,const sigset_t *set,sigset_t *oldset);
int Kill(pid_t pid,int signal);

/*
 * main - The shell's main routine 
 */
int 
main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE];    /* cmdline for fgets */
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
            break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
            break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
            break;
        default:
            usage();
        }
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */
    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(job_list);


    /* Execute the shell's read/eval loop */
    while (1) {

        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { 
            /* End of file (ctrl-d) */
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            exit(0);
        }
        
        /* Remove the trailing newline */
        cmdline[strlen(cmdline)-1] = '\0';
        
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
void 
eval(char *cmdline) 
{
    int bg;              /* should the job run in bg or fg? */
    int job_state;      /* initial value of job BG or FG based on bg */
    int status;
    int check;
    struct cmdline_tokens tok;
    pid_t pid = -255;  // Initialzing to
    int jid = -255;   //  dummy values 
    sigset_t mask;      
    sigset_t mask2;      
    int flag = 0; //Used while processing "fg" built in command
    int infile_fd ; //file descriptor to be used for infile if specified in job 
    int outfile_fd ; //file descriptor to be used for outfile if specified in job

    //Get shell pid
    tsh_pid = getpid();

    //Intialize mask for blocked signal
    //Block SIGCHLD SIGINT SIGTSTP signals
    Sigemptyset(&mask);
    Sigemptyset(&mask2);
    Sigaddset(&mask,SIGCHLD);
    Sigaddset(&mask,SIGINT);
    Sigaddset(&mask2,SIGINT);
    Sigaddset(&mask,SIGTSTP);
    Sigaddset(&mask2,SIGTSTP);
    Sigprocmask(SIG_BLOCK,&mask,NULL);

    /* Parse command line */
    bg = parseline(cmdline, &tok); 

    if (bg == -1) return;               /* parsing error */
    if (tok.argv[0] == NULL)  return;   /* ignore empty lines */

    /* If tok is a BUILTIN shell command */
    if (tok.builtins != BUILTIN_NONE) {
        
       switch(tok.builtins) {                           

                           //Built in command quit :
                           //Send SIGKILL to all processes in shell
       case BUILTIN_QUIT : tsh_pid = getpid();
                           kill(-tsh_pid,SIGKILL);
                           break;

                           //List out all jobs when "jobs" called
                           //Also open output file if redirection specified and 
                           //redirect jobs output to the new file's descriptor

       case BUILTIN_JOBS :  if (tok.outfile != NULL) {
				    outfile_fd = open(tok.outfile , O_WRONLY);
				    listjobs(job_list,outfile_fd);
				    break;
			    }

			    else
				    listjobs(job_list,STDOUT_FILENO);
			    break;


			    // Parse job id or pid given with bg command
			    // Change state from ST to BG in job_list
			    // Send SIGCONT signal to job

       case BUILTIN_FG :   if(*(char *)(tok.argv[1]) == '%' ) {
				   jid = atoi( (((char *)(tok.argv[1])) + 1) ); 
				   pid = jid2pid(jid);
			   }
			   else {
				   pid = atoi(tok.argv[1]);
				   jid = pid2jid(pid);
			   }
			   change_job_state(job_list,pid,FG);
			   flag = 1;                            //flag set because we want to jump into else clause below 
			   // to process resumed job as a foreground job
			   Kill(-pid,SIGCONT);  
			   break;

			   //Parse job id or pid given with bg command
			   // Change state from ST to BG in job_list
			   // Send SIGCONT signal to job
       case BUILTIN_BG :   if(*(char *)(tok.argv[1]) == '%' ) {
				   jid = atoi( (((char *)(tok.argv[1])) + 1) ); 
				   pid = jid2pid(jid);
			   }
			   else {
				   pid = atoi(tok.argv[1]);
				   jid = pid2jid(pid);
			   }
			   printjob(pid,STDOUT_FILENO);
			   change_job_state(job_list,pid,BG);
			   Kill(-pid,SIGCONT);
			   break;
       case BUILTIN_NONE : break;
       default : break;

       }

    }


    //If tok is a external program to be run by shell 
    else if ((tok.builtins == BUILTIN_NONE) || (flag == 1)) {

	    if (flag == 1) 
		    bg = 0;

	    if(!bg)
		    job_state = FG;
	    else
		    job_state = BG;




	    //Child process   
	    if ((pid = Fork()) == 0) {

		    setpgid(0,0);  //Start process in new group     
		    Signal(SIGINT,  SIG_DFL);   /* ctrl-c from child handled by parent's sigchld */

		    addjob(job_list,getpid(),job_state,cmdline); 

		    // If input/output redirection specified open given file and point STDIN/STDOUT descriptor
		    // to new file's file descriptor
		    if (tok.infile != NULL) {
			    infile_fd = open(tok.infile , O_RDONLY);
			    dup2(infile_fd,STDIN_FILENO);   
		    }

		    if (tok.outfile != NULL) {
			    outfile_fd = open(tok.outfile , O_WRONLY);
			    dup2(outfile_fd,STDOUT_FILENO);   
		    }

		    //Unblock masks inherited from parent process
		    //and execute program using exec
		    Sigprocmask(SIG_UNBLOCK,&mask,NULL);
		    Execve(tok.argv[0],tok.argv,environ); 

	    }


	    //Parent process
	    //Add child to job list and unblock signal,also set fg_pid if job is foreground job	    
	    addjob(job_list,pid,job_state,cmdline); 
	    if(!bg)
		    fg_pid = pid;
	    Sigprocmask(SIG_UNBLOCK,&mask2,NULL); 

	    //Until foreground process terminates SIGCHLD functionality is done here , SIGINT and SIGTSTP are handled by handlers 
	    if(!bg) {

		    check = waitpid(pid,&status,WUNTRACED);
		    if ((check<0) && (errno!=ECHILD)) 
			    unix_error("waitfg : wait pid error\n");

		    if ((check == pid) && WIFSTOPPED(status)){
			    print_sigtstp_job(job_list,pid,SIGTSTP,STDOUT_FILENO);          //Print message that job was stopped by SIGSTP signal 

			    //Change stopped job state in list to ST (stopped) 
			    change_job_state(job_list,pid,ST);
			    return;
		    }

		    if ((check == pid) && (WIFSIGNALED(status)))
			    print_sigint_job(job_list,pid,WTERMSIG(status),STDOUT_FILENO);       //Print message that job/pid was terminated by a signal 


		    deletejob(job_list,pid);
		    Sigprocmask(SIG_UNBLOCK,&mask,NULL); 

	    }

	    else {
		    Sigprocmask(SIG_UNBLOCK,&mask,NULL); 
		    printjob(pid,STDOUT_FILENO);
	    }



    }

    return;
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Parameters:
 *   cmdline:  The command line, in the form:
 *
 *                command [arguments...] [< infile] [> oufile] [&]
 *
 *   tok:      Pointer to a cmdline_tokens structure. The elements of this
 *             structure will be populated with the parsed tokens. Characters 
 *             enclosed in single or double quotes are treated as a single
 *             argument. 
 * Returns:
 *   1:        if the user has requested a BG job
 *   0:        if the user has requested a FG job  
 *  -1:        if cmdline is incorrectly formatted
 * 
 * Note:       The string elements of tok (e.g., argv[], infile, outfile) 
 *             are statically allocated inside parseline() and will be 
 *             overwritten the next time this function is invoked.
 */
	int 
parseline(const char *cmdline, struct cmdline_tokens *tok) 
{

	static char array[MAXLINE];          /* holds local copy of command line */
	const char delims[10] = " \t\r\n";   /* argument delimiters (white-space) */
	char *buf = array;                   /* ptr that traverses command line */
	char *next;                          /* ptr to the end of the current arg */
	char *endbuf;                        /* ptr to the end of the cmdline string */
	int is_bg;                           /* background job? */

	int parsing_state;                   /* indicates if the next token is the
						input or output file */

	if (cmdline == NULL) {
		(void) fprintf(stderr, "Error: command line is NULL\n");
		return -1;
	}

	(void) strncpy(buf, cmdline, MAXLINE);
	endbuf = buf + strlen(buf);

	tok->infile = NULL;
	tok->outfile = NULL;

	/* Build the argv list */
	parsing_state = ST_NORMAL;
	tok->argc = 0;

	while (buf < endbuf) {
		/* Skip the white-spaces */
		buf += strspn (buf, delims);
		if (buf >= endbuf) break;

		/* Check for I/O redirection specifiers */
		if (*buf == '<') {
			if (tok->infile) {
				(void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
				return -1;
			}
			parsing_state |= ST_INFILE;
			buf++;
			continue;
		}
		if (*buf == '>') {
			if (tok->outfile) {
				(void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
				return -1;
			}
			parsing_state |= ST_OUTFILE;
			buf ++;
			continue;
		}

		if (*buf == '\'' || *buf == '\"') {
			/* Detect quoted tokens */
			buf++;
			next = strchr (buf, *(buf-1));
		} else {
			/* Find next delimiter */
			next = buf + strcspn (buf, delims);
		}

		if (next == NULL) {
			/* Returned by strchr(); this means that the closing
			   quote was not found. */
			(void) fprintf (stderr, "Error: unmatched %c.\n", *(buf-1));
			return -1;
		}

		/* Terminate the token */
		*next = '\0';

		/* Record the token as either the next argument or the input/output file */
		switch (parsing_state) {
			case ST_NORMAL:
				tok->argv[tok->argc++] = buf;
				break;
			case ST_INFILE:
				tok->infile = buf;
				break;
			case ST_OUTFILE:
				tok->outfile = buf;
				break;
			default:
				(void) fprintf(stderr, "Error: Ambiguous I/O redirection\n");
				return -1;
		}
		parsing_state = ST_NORMAL;

		/* Check if argv is full */
		if (tok->argc >= MAXARGS-1) break;

		buf = next + 1;
	}

	if (parsing_state != ST_NORMAL) {
		(void) fprintf(stderr, "Error: must provide file name for redirection\n");
		return -1;
	}

	/* The argument list must end with a NULL pointer */
	tok->argv[tok->argc] = NULL;

	if (tok->argc == 0)  /* ignore blank line */
		return 1;

	if (!strcmp(tok->argv[0], "quit")) {                 /* quit command */
		tok->builtins = BUILTIN_QUIT;
	} else if (!strcmp(tok->argv[0], "jobs")) {          /* jobs command */
		tok->builtins = BUILTIN_JOBS;
	} else if (!strcmp(tok->argv[0], "bg")) {            /* bg command */
		tok->builtins = BUILTIN_BG;
	} else if (!strcmp(tok->argv[0], "fg")) {            /* fg command */
		tok->builtins = BUILTIN_FG;
	} else {
		tok->builtins = BUILTIN_NONE;
	}

	/* Should the job run in the background? */
	if ((is_bg = (*tok->argv[tok->argc-1] == '&')) != 0)
		tok->argv[--tok->argc] = NULL;

	return is_bg;
}


/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU signal. The 
 *     handler reaps all available zombie children, but doesn't wait 
 *     for any other currently running children to terminate.  
 */
	void 
sigchld_handler(int sig) 
{       
	int status;
	pid_t pid;
	//Reap zombie processes
	while((pid = waitpid(-1,&status,WNOHANG)) > 0) {

		if(WIFSIGNALED(status))
			print_sigint_job(job_list,pid,WTERMSIG(status),STDOUT_FILENO);       //Print message that job/pid was terminated by a signal 

		deletejob(job_list,pid);
	}

	if(errno!=ECHILD)
		unix_error("waitpid error"); 

	return;
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job. 
 *    If the SIGINT is sent to shell then forward it to the fg job else get 
 *    pid of current process and kill itself.
 *
 */


	void 
sigint_handler(int sig) 
{       

	//Delete fg job from list and send SIGINT to all processes in fg group 
	//Only to be done when parent shell or fg job is sent this signal.
	//OBackground jobs reaped by SIGCHLD handler

	pid_t pid; 
	pid = getpid();
	if ( (pid == tsh_pid) || (pid == fg_pid) )
		Kill(-fg_pid,SIGINT);

	return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
	void 
sigtstp_handler(int sig) 
{        

	//Delete fg job from list and send SIGTSTP to all processes in fg group 
	//Only to be done when parent shell or fg job is sent this signal.
	//Else just send SIGTSTP to process with id "pid"

	pid_t pid; 
	pid = getpid();
	if ( (pid == tsh_pid) || (pid == fg_pid) )
		Kill(-fg_pid,SIGTSTP);

	else
		Kill(pid,SIGTSTP);

	return;
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void 
clearjob(struct job_t *job) {
	job->pid = 0;
	job->jid = 0;
	job->state = UNDEF;
	job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void 
initjobs(struct job_t *job_list) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		clearjob(&job_list[i]);
}

/* maxjid - Returns largest allocated job ID */
	int 
maxjid(struct job_t *job_list) 
{
	int i, max=0;

	for (i = 0; i < MAXJOBS; i++)
		if (job_list[i].jid > max)
			max = job_list[i].jid;
	return max;
}

/* addjob - Add a job to the job list */
	int 
addjob(struct job_t *job_list, pid_t pid, int state, char *cmdline) 
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].pid == 0) {
			job_list[i].pid = pid;
			job_list[i].state = state;
			job_list[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(job_list[i].cmdline, cmdline);
			if(verbose){
				printf("Added job [%d] %d %s\n", job_list[i].jid, job_list[i].pid, job_list[i].cmdline);
			}
			return 1;
		}
	}
	printf("Tried to create too many jobs\n");
	return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
	int 
deletejob(struct job_t *job_list, pid_t pid) 
{
	int i;

	if (pid < 1)
		return 0;

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].pid == pid) {
			clearjob(&job_list[i]);
			nextjid = maxjid(job_list)+1;
			return 1;
		}
	}
	return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t 
fgpid(struct job_t *job_list) {
	int i;

	for (i = 0; i < MAXJOBS; i++)
		if (job_list[i].state == FG)
			return job_list[i].pid;
	return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t 
*getjobpid(struct job_t *job_list, pid_t pid) {
	int i;

	if (pid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (job_list[i].pid == pid)
			return &job_list[i];
	return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *job_list, int jid) 
{
	int i;

	if (jid < 1)
		return NULL;
	for (i = 0; i < MAXJOBS; i++)
		if (job_list[i].jid == jid)
			return &job_list[i];
	return NULL;
}

/* pid2jid - Map process ID to job ID */
	int 
pid2jid(pid_t pid) 
{
	int i;

	if (pid < 1)
		return 0;
	for (i = 0; i < MAXJOBS; i++)
		if (job_list[i].pid == pid) {
			return job_list[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
	void 
listjobs(struct job_t *job_list, int output_fd) 
{
	int i;
	char buf[MAXLINE];



	for (i = 0; i < MAXJOBS; i++) {
		memset(buf, '\0', MAXLINE);
		if (job_list[i].pid != 0) {
			sprintf(buf, "[%d] (%d) ", job_list[i].jid, job_list[i].pid);
			if(write(output_fd, buf, strlen(buf)) < 0) {
				fprintf(stderr, "Error writing to output file\n");
				exit(1);
			}
			memset(buf, '\0', MAXLINE);
			switch (job_list[i].state) {
				case BG:
					sprintf(buf, "Running    ");
					break;
				case FG:
					sprintf(buf, "Foreground ");
					break;
				case ST:
					sprintf(buf, "Stopped    ");
					break;
				default:
					sprintf(buf, "listjobs: Internal error: job[%d].state=%d ",
							i, job_list[i].state);
			}
			if(write(output_fd, buf, strlen(buf)) < 0) {
				fprintf(stderr, "Error writing to output file\n");
				exit(1);
			}
			memset(buf, '\0', MAXLINE);
			sprintf(buf, "%s\n", job_list[i].cmdline);
			if(write(output_fd, buf, strlen(buf)) < 0) {
				fprintf(stderr, "Error writing to output file\n");
				exit(1);
			}
		}
	}
	if(output_fd != STDOUT_FILENO)
		close(output_fd);
}


/* Prints [jid] (pid) jobname 
 * Arg - pid_t pid 
 */

void printjob(pid_t pid,int output_fd){   

	int i;
	char buf[MAXLINE];
	int job_id ;


	job_id = pid2jid(pid);

	if(job_id == 0) {
		fprintf(stderr, "Job not present in job list\n");
		exit(1);
	}

	memset(buf, '\0', MAXLINE);
	sprintf(buf, "[%d] (%d) ", job_id, pid);
	if(write(output_fd, buf, strlen(buf)) < 0) {
		fprintf(stderr, "Error writing to output file\n");
		exit(1);
	}

	memset(buf, '\0', MAXLINE);

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].jid == job_id) {
			sprintf(buf,"%s\n",job_list[i].cmdline);
			break;
		}

	}

	if(write(output_fd, buf, strlen(buf)) < 0) {
		fprintf(stderr, "Error writing to output file\n");
		exit(1);
	}

	if(output_fd != STDOUT_FILENO)
		close(output_fd);

}


//Print message that fg job was terminated by SIGINT signal 
void print_sigint_job(struct job_t *job_list,pid_t pid,int signal,int output_fd) {

	int i;
	char buf[MAXLINE];
	int job_id ;


	job_id = pid2jid(pid);


	if(job_id == 0) {
		fprintf(stderr, "Job not present in job list\n");
		exit(1);
	}

	memset(buf, '\0', MAXLINE);
	sprintf(buf, "Job [%d] (%d) ", job_id, pid);
	if(write(output_fd, buf, strlen(buf)) < 0) {
		fprintf(stderr, "Error writing to output file\n");
		exit(1);
	}

	memset(buf, '\0', MAXLINE);

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].jid == job_id) {
			sprintf(buf,"terminated by signal %d\n",signal);
			break;
		}

	}

	if(write(output_fd, buf, strlen(buf)) < 0) {
		fprintf(stderr, "Error writing to output file\n");
		exit(1);
	}

	if(output_fd != STDOUT_FILENO)
		close(output_fd);

	return;

}        

//Print message that job was stpped by SIGSTP signal 
void print_sigtstp_job(struct job_t *job_list,pid_t pid,int signal,int output_fd) {

	int i;
	char buf[MAXLINE];
	int job_id ;

	job_id = pid2jid(pid);

	if(job_id == 0) {
		fprintf(stderr, "Job not present in job list\n");
		exit(1);
	}

	memset(buf, '\0', MAXLINE);
	sprintf(buf, "Job [%d] (%d) ", job_id, pid);
	if(write(output_fd, buf, strlen(buf)) < 0) {
		fprintf(stderr, "Error writing to output file\n");
		exit(1);
	}

	memset(buf, '\0', MAXLINE);

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].jid == job_id) {
			sprintf(buf,"stopped by signal %d\n",signal);
			break;
		}

	}

	if(write(output_fd, buf, strlen(buf)) < 0) {
		fprintf(stderr, "Error writing to output file\n");
		exit(1);
	}

	if(output_fd != STDOUT_FILENO)
		close(output_fd);

	return;
} 


//Changes job with id = pid to new_state in job_list
void change_job_state(struct job_t *job_list,pid_t pid,int new_state) {

	int i;
	int job_id ;
	job_id = pid2jid(pid);
	if(job_id == 0) {
		fprintf(stderr, "Job not present in job list\n");
		exit(1);
	}

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].jid == job_id) {
			job_list[i].state = new_state;
			break;
		}

	}

	return;

}




//Changes job with job id = jid to new_state in job_list
void change_job_state_jid(struct job_t *job_list,int jid,int new_state) {

	int i;
	int job_id ;
	job_id = jid;

	if(job_id == 0) {
		fprintf(stderr, "Job not present in job list\n");
		exit(1);
	}

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].jid == job_id) {
			job_list[i].state = new_state;
			break;
		}

	}

	return;

}
/* Returns pid of a job given its jid
 * Returns 0 if job not found
 * Arg - int jid 
 */

pid_t jid2pid(int jid){   

	int i;

	for (i = 0; i < MAXJOBS; i++) {
		if (job_list[i].jid == jid) {
			return job_list[i].pid;
		}

	}

	return 0;


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
	void 
usage(void) 
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
	void 
unix_error(char *msg)
{
	fprintf(stdout, "%s: %s\n", msg, strerror(errno));
	exit(1);
}

/*
 * app_error - application-style error routine
 */
	void 
app_error(char *msg)
{
	fprintf(stdout, "%s\n", msg);
	exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
	handler_t 
*Signal(int signum, handler_t *handler) 
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
	void 
sigquit_handler(int sig) 
{
	printf("Terminating after receipt of SIGQUIT signal\n");
	exit(1);
}



//System call wrappers with error handling

//System call wrapper for fork()
pid_t Fork() {
	return fork();
}

//System call wrapper for exec()
void Execve(char *program,char **argv,char **env) {
	execve(program,argv,env);
}


//System call wrapper for sigemptyset(sigset_t set)
int Sigemptyset(sigset_t *set) {
	return sigemptyset(set);
}


//System call wrapper for sigaddset(sigset_t set,int signum)
int Sigaddset(sigset_t *set,int signum) {
	return sigaddset(set,signum);
}

//System call wrapper for sigemptyset(sigset_t set)
int Sigprocmask(int how,const sigset_t *set,sigset_t *oldset) {
	return sigprocmask(how,set,oldset);
}

//System call wrapper for kill(pid_t pid,int signal)
int Kill(pid_t pid,int signal) {
	return kill(pid,signal);
}
