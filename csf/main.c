#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <libgen.h>

#include "utils.h"
//#include "common.h"
#include "mod_conf.h"
#include "log.h"
#include "confparser.h"

#define WAIT_T           int

#define DELAY_ERROR       3000 * 1000
#define DELAY_RESTARTING   500 * 1000

static int graceful_restart = 0;

pid_t child_pid;

static int
process_wait(pid_t pid)
{
	pid_t	re;
	int		rc;
	int		exitcode = 0;

	while(1) {
		re = waitpid(pid, &exitcode, 0);
		if(re > 0)
			break;
		else if(errno == EINTR) 
			if(graceful_restart)
				break;
			else
				continue;
		else if(errno == ECHILD) 
			return (CSF_OK);
		else
			return (CSF_ERR);
	}

	if (WIFEXITED(exitcode)) {
		rc = WEXITSTATUS(exitcode);

		if (rc == 2)
			exit(2);

		/* Child terminated normally */ 
		printf("PID %d: exited rc=%d\n", pid, rc);
		if(rc != 0) 
			return (CSF_ERR);
	} else if(WIFSIGNALED(exitcode)) {
		/* XXX coredump->restart? */
		/* Child process terminated by a signal */
		printf("PID %d: received a signal=%d\n", pid, WTERMSIG(exitcode));
	}

	return (CSF_OK);
}


static void 
signals_handler(int sig, siginfo_t *si, void *context) 
{
	CSF_UNUSED_ARG(context);

	switch(sig) {
	case SIGUSR1:
		/* Restart: the tough way */
		kill(child_pid, SIGINT);
		process_wait(child_pid);
		break;

	case SIGHUP:
		/* Graceful restart */
		graceful_restart = 1;
		kill(child_pid, SIGHUP);
		break;

	case SIGINT:
	case SIGTERM:
		/* Kill child and exit */
		kill(child_pid, SIGTERM);
		process_wait(child_pid);
//		pid_file_clean(pid_file_path);
		exit(0);

	case SIGCHLD:
		/* Child exited */
		process_wait(si->si_pid);
		break;

	default:
		/* Forward the signal */
		kill(child_pid, sig);
	}
}


static void
set_signals(void)
{
	struct sigaction act;

	/* Signals it handles
	 */
	memset(&act, 0, sizeof(act));

	act.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &act, NULL);
	
	/* Signals it handles
	 */
	act.sa_sigaction = signals_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;

	sigaction(SIGHUP,  &act, NULL);
	sigaction(SIGINT,  &act, NULL);
	sigaction(SIGTERM, &act, NULL);
	sigaction(SIGUSR1, &act, NULL);
	sigaction(SIGUSR2, &act, NULL);
	sigaction(SIGCHLD, &act, NULL);
}


static pid_t
process_launch(char *path, char **argv)
{
	pid_t pid;

	/* Execute the server
	 */
	pid = fork();
	if(pid == 0) {
		argv[0] = path;
		execvp(path, argv);

		printf("ERROR: Could not execute %s\n", path);
		exit(1);
	}
	
	return pid;
}

char	*csf_worker;

static void
figure_worker_path (const char *arg0)
{
	pid_t       me;
	char        tmp[512];
	int         len, re, i;
	const char *d;
	const char *unix_paths[] = {"/proc/%d/exe",        /* Linux   */
				    "/proc/%d/path/a.out", /* Solaris */
				    "/proc/%d/file",       /* BSD     */
				    NULL};

	/* Invoked with the fullpath */
	if (arg0[0] == '/') {
		len = strlen(arg0) + sizeof("-worker") + 1;
		csf_worker = malloc (len);

		snprintf (csf_worker, len, "%s-worker", arg0);
		return;
	}

	/* Partial path work around */
	d = arg0;
	while (*d && *d != '/') d++;
		
	if ((arg0[0] == '.') || (*d == '/')) {
		d = getcwd (tmp, sizeof(tmp));
		len = strlen(arg0) + strlen(d) + sizeof("-worker") + 1;
		csf_worker = malloc (len);

		snprintf (csf_worker, len, "%s/%s-worker", d, arg0);
		return;
	}

	/* Deal with unixes
	 */
	me = getpid();

	for (i=0; unix_paths[i]; i++) {
		char link[512];

		snprintf (tmp, sizeof(tmp), unix_paths[i], me);
		re = readlink (tmp, link, sizeof(link));
		if (re > 0) {
			link[re] = '\0';
			len = re + sizeof("-worker") + 1;

			csf_worker = malloc (len);
			snprintf (csf_worker, len, "%s-worker", link);
			return;
		}
	}

	/* The very last option, use the default path
	 */
}


int
main(int argc, char **argv)
{
	int		ret;

	CSF_UNUSED_ARG(argc);
	
	figure_worker_path (argv[0]);
	
	set_signals();

	daemonize(1, 1);

	while(1) {
		graceful_restart = 0;
		child_pid = process_launch(csf_worker, argv);
		if (child_pid < 0) {
			printf("Couldn't launch \n");
			exit(1);
		}
		printf("\nCSF Child process id: %d\n", child_pid);

		ret = process_wait(child_pid);

		usleep ((ret == 0) ? 
			DELAY_RESTARTING : 
			DELAY_ERROR);
	}

	return (0);
}
