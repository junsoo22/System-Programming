/*
 *
 * tsh - A tiny shell program with job control
 *
 * Name: <김준수>
 * Student id: <2022031994>
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
#define MAXLINE    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

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
struct job_t jobs[MAXJOBS]; /* The job list */
/* End global variables */


/* Function prototypes */

/*----------------------------------------------------------------------------
 * Functions that you will implement
 */

void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigint_handler(int sig);
void sigtstp_handler(int sig);

/*----------------------------------------------------------------------------*/

/* These functions are already implemented for your convenience */
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

	/* This one provides a clean way to kill the shell */
	Signal(SIGQUIT, sigquit_handler);

	/* Initialize the job list */
	initjobs(jobs);

	/* Execute the shell's read/eval loop */
	while (1) {

		/* Read command line */
		if (emit_prompt) {
			printf("%s", prompt);
			fflush(stdout);
		}
		if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
			app_error("fgets error");
		if (feof(stdin)) { /* End of file (ctrl-d) */
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
 * eval - Evaluate the command line that the user has just typㅑed in
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
	int bg, status;
	pid_t pid;
	char *argv[MAXARGS];
	bg=parseline(cmdline,argv);
	
	if(argv[0] && !builtin_cmd(argv)){   //builtin command 아닌 경우 실행파일로 인식해서 fork하고 execve() 함수 필요
		//fork(), execv() 함수 필요
		if((pid=fork())==0){    //foreground 에서  자식 process가 동작함. 
			setpgid(0,0);  //pid기준으로 pgid가 setting이 됨. 
			 //음수니까 실행파일 못찾음
			if(execve(argv[0], argv, environ)<0){   //myspin 같은 프로그램 실행시키기 위함
				printf("%s: Command not found\n", argv[0]);
				exit(0);
			}
			
		}
		else if(pid>0){      //부모 process(shell 프로그램)
			//background면 BG, 아니면 FG
			addjob(jobs,pid,bg ? BG : FG,cmdline);   //jobs라는 커멘드입력됐을 때, 인자들 추가
			if(bg){
				printf("[%d] (%d) %s",pid2jid(pid),pid,cmdline);
			}

			
			//wait(&status);   //signal_handler 부분에서 reaping해주므로 여기서는 필요없음
			else
				waitfg(pid);    //fg동작에서는 기다릴 필요 잇으므로 waitfg로 대기함
		}
	}
	return;
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the uSER Has requested a BG job, false if
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
	buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
	while (*buf && (*buf == ' ')) /* ignore leading spaces */
		buf++;

	/* Build the argv list */
	argc = 0;
	if (*buf == '\'') {
		buf++;
		delim = strchr(buf, '\'');
	}
	else {
		delim = strchr(buf, ' ');
	}

	while (delim) {
		argv[argc++] = buf;
		*delim = '\0';
		buf = delim + 1;
		while (*buf && (*buf == ' ')) /* ignore spaces */
			buf++;

		if (*buf == '\'') {
			buf++;
			delim = strchr(buf, '\'');
		}
		else {
			delim = strchr(buf, ' ');
		}
	}
	argv[argc] = NULL;

	if (argc == 0)  /* ignore blank line */
		return 1;

	/* should the job run in the background? */
	if ((bg = (*argv[argc-1] == '&')) != 0) {
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

	
	if(argv[0]==NULL) //빈 문자인지 확인 (그냥 enter 쳤을 때)
		return 1;     //builtin command인걸로 처리해줌
	if(!strcmp(argv[0],"quit")){
		exit(0);
	}
	else if(!strcmp(argv[0], "jobs")){
		listjobs(jobs);
		return 1;
	}
	
	else if(!strcmp(argv[0],"fg")){
		
		if(argv[1]==NULL){
			printf("%s command requires PID or %%jobid argument\n",argv[0]);
			return;
		}
		do_bgfg(argv);
		return 1;
	}
	else if(!strcmp(argv[0],"bg")){

		if(argv[1]==NULL){
			printf("%s command requires PID or %%jobid argument\n",argv[0]);
			return;
		}
		do_bgfg(argv);
		return 1;
	}
	

	return 0;     /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv)
{	
			
	int jid=0;
	int pid=0;
	struct job_t* job;
	int idx=1;
	int flag;
	int bg;
	//int fg;
	int a=strlen(argv[1]);

	/*
	if(argv[1]==NULL){
		printf("%s command requires PID or %%jobid argument\n",argv[0]);
		return;
	}
	*/

	if(argv[1][0]=='%'){
		flag=1;   //% 가 입력되면 jid로 간주
		
		for(int i=a-1;i>0;i--){
	
			jid += (argv[1][i]-'0')*idx;
			idx*=10;
		}

		job=getjobjid(jobs,jid);
		if(!job){
			printf("%s: No such job\n", argv[1]);
			return;
		}

	}
	

	else if(isdigit(argv[1][0])){    //그냥 숫자가 오면 pid로 간주
		flag=0;

		for(int i=a-1;i>=0;i--){
	
			pid += (argv[1][i]-'0')*idx;
			idx*=10;
		}

		job=getjobpid(jobs,pid);
		if(!job){
			printf("(%s): No such process\n", argv[1]);
			return;
		}
	}
	else{

		printf("%s: argument must be a PID or %%jobid\n", argv[0]);
		return;
		
	}
	
	/*
	if(flag){
	//	job=getjobjid(jobs,jid);

		job=getjobjid(jobs,jid);
		if(!job){
			printf("%s: No such job", argv[1]);
			return;
		}
	}
	else{
	//	job=getjobpid(jobs,pid);


		job=getjobpid(jobs,pid);
		if(!job){
			printf("(%s): No such process", argv[1]);
			return;
		}
	}
	*/
	
	
	bg=!strcmp(argv[0],"bg");
	if(bg){
		job->state=BG;
		printf("[%d] (%d) %s",job->jid,job->pid,job->cmdline);
		kill(-(job->pid), SIGCONT);
	}
	else{
		job->state=FG;

		kill(-(job->pid), SIGCONT);

		waitfg(job->pid);
	}
	
	
	return;
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid)
{
	struct job_t* job=getjobpid(jobs,pid);
	if(job){
		while(job->pid==pid && job->state==FG)
			sleep(1);
	}
	return;
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
	int status;
	pid_t pid;
	struct job_t* job;
	//waitpid가 return 하는 값이 pid에 적히는데 이게 양수인 경우에만 반복문에 남아있음
	//그렇지 않은 경우는 핸들러부터 return해서 shell 원래 기능 할 수 있도록 나타냄
	while((pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0){    //waitpid에서 return을 해야하는데 waitpid에서 멈춰있음
		//job=getjobpid(jobs,pid);
		if(WIFEXITED(status)){     //정상 종료되었을 때 1 return, deletejob 해줘야함
			deletejob(jobs,pid);
		}
		
		else if(WIFSTOPPED(status)){

			job=getjobpid(jobs,pid);
			if(job){
				job->state=ST;
				fprintf(stderr,"Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(status));
			}
		}
		else if(WIFSIGNALED(status)){
			
			fprintf(stderr, "Job [%d] (%d) terminated by signal %d\n",pid2jid(pid),pid,WTERMSIG(status));
			
			deletejob(jobs,pid);
		}
	}	

	return;
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig)
{
	int status;
	pid_t pid=fgpid(jobs);
	if(pid>0)
		kill(-pid,SIGINT);
	return;
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig)
{
	int status;
	pid_t pid=fgpid(jobs);

	//fprintf(stderr,"Job [%d] (%d) stopped by signal %d\n",pid2jid(pid),pid,WSTOPSIG(status));
	if(pid>0)
		kill(-pid,SIGTSTP);
	
	return;
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
	int i, max=0;

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

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == 0) {
			jobs[i].pid = pid;
			jobs[i].state = state;
			jobs[i].jid = nextjid++;
			if (nextjid > MAXJOBS)
				nextjid = 1;
			strcpy(jobs[i].cmdline, cmdline);
			if(verbose){
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

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid == pid) {
			clearjob(&jobs[i]);
			nextjid = maxjid(jobs)+1;
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
		if (jobs[i].pid == pid) {
			return jobs[i].jid;
		}
	return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs)
{
	int i;

	for (i = 0; i < MAXJOBS; i++) {
		if (jobs[i].pid != 0) {
			printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
			switch (jobs[i].state) {
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
