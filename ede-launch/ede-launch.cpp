#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Pixmap.H>
#include <edelib/Run.h>
#include <edelib/Window.h>
#include <edelib/Nls.h>

#include "icons/run.xpm"

/* 
 * Window from X11 is alread included with Fl.H so we can't use 
 * EDELIB_NS_USING(Window) here. Stupid C++ namespaces
 */
#define LaunchWindow edelib::Window

EDELIB_NS_USING(run_program_fmt)

static Fl_Pixmap image_run(run_xpm);
static Fl_Input* dialog_input;
static int       ret;

void help(void) {
	puts("Usage: ede-launch [OPTIONS] program");
	puts("EDE program launcher");
}

static char** cmd_split(const char* cmd) {
	int   sz = 10;
	int   i = 0;
	char* c = strdup(cmd);

	char** arr = (char**)malloc(sizeof(char*) * sz);

	for(char* p = strtok(c, " "); p; p = strtok(NULL, " ")) {
		if(i >= sz) {
			sz *= 2;
			arr = (char**)realloc(arr, sizeof(char*) * sz);
		}
		arr[i++] = strdup(p);
	}

	arr[i] = NULL;
	free(c);

	return arr;
}

static void start_crasher(const char* cmd, int sig) {
	/* 
	 * call edelib implementation instead start_child_process()
	 * to prevents loops if 'ede-crasher' crashes
	 */
	run_program_fmt(true, "ede-crasher --apppath %s --signal %i", cmd, sig);
}

static int start_child_process(const char* cmd) {
	int pid, in[2], out[2], err[2];
	char** params = cmd_split(cmd);

	pipe(in);
	pipe(out);
	pipe(err);

	signal(SIGCHLD, SIG_DFL);

	pid = fork();
	switch(pid) {
		case 0:
			/* child process */
			close(0);
			dup(in[0]);
			close(in[0]);
			close(in[1]);

			close(1);
			dup(out[1]);
			close(out[0]);
			close(out[1]);

			close(2);
			dup(err[1]);
			close(err[0]);
			close(err[1]);

			errno = 0;

			/* start it */
			execvp(params[0], params);

			if(errno == 2)
				_exit(199);
			else
				_exit(errno);
			break;
		case -1:
			fprintf(stderr, "ede-launcher: fork() failed\n");
			/* close the pipes */
			close(in[0]);
			close(in[1]);

			close(out[0]);
			close(out[1]);

			close(err[0]);
			close(err[1]);
			break;
		default:
			/* perent */
			close(in[0]);
			close(out[1]);
			close(err[1]);
			break;
	}

	int status;
	if(waitpid(pid, &status, 0) < 0) {
		fprintf(stderr, "ede-launcher: waitpid() failed\n");
		return 1;
	}

	if(WIFEXITED(status)) {
		/*int v = WEXITSTATUS(status);
		fprintf(stderr, "ede-launcher: child exited with '%i'\n", v); */
	} else if(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV) {
		start_crasher(cmd, SIGSEGV);
	} else {
		fprintf(stderr, "ede-launcher: child '%s' killed!\n", cmd);
	}

	return 0;
}

static void start_child_process_with_core(const char* cmd) {
	struct rlimit r;
	if(getrlimit(RLIMIT_CORE, &r) == -1) {
		ret = -1; /* TODO: core dump error message */
		return;
	}

	rlim_t old = r.rlim_cur;
	r.rlim_cur = RLIM_INFINITY;

	if(setrlimit(RLIMIT_CORE, &r) == -1) {
		ret = -1; /* TODO: core dump error message */
		return;
	}

	ret = start_child_process(cmd);

	r.rlim_cur = old;
	if(setrlimit(RLIMIT_CORE, &r) == -1) {
		ret = -1; /* TODO: core dump error message */
		return;
	}
}

static void cancel_cb(Fl_Widget*, void* w) {
	LaunchWindow* win = (LaunchWindow*)w;
	win->hide();
}

static void ok_cb(Fl_Widget*, void* w) {
	LaunchWindow* win = (LaunchWindow*)w;
	const char* cmd = dialog_input->value();
	win->hide();

	/* do not block dialog when program is starting */
	Fl::check();

	/* TODO: is 'cmd' safe to use here? */
	start_child_process_with_core(cmd);
}

static void start_dialog(int argc, char** argv) {
	LaunchWindow* win = new LaunchWindow(370, 160, _("Run Command"));
	win->begin();
		Fl_Box* icon = new Fl_Box(10, 10, 55, 55);
		icon->image(image_run);

		Fl_Box* txt = new Fl_Box(70, 10, 290, 69, _("Enter the name of the application you would like to run or the URL you would like to view"));
		txt->align(132|FL_ALIGN_INSIDE);

		dialog_input = new Fl_Input(70, 90, 290, 25, _("Open:"));

		Fl_Button* ok = new Fl_Button(175, 125, 90, 25, _("&OK"));
		ok->callback(ok_cb, win);
		Fl_Button* cancel = new Fl_Button(270, 125, 90, 25, _("&Cancel"));
		cancel->callback(cancel_cb, win);
	win->end();
	win->show(argc, argv);

	while(win->shown())
		Fl::wait();
}

int main(int argc, char** argv) {
	if(argc == 1)
		start_dialog(argc, argv);
	else if(argc != 2) {
		help();
		return 0;
	} else {
		start_child_process_with_core(argv[1]);
	}

	return ret;
}