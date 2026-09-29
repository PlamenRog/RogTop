#include "rogtop.h"

#include <errno.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool read_process_start_time(pid_t pid, unsigned long long* start_time_ticks) {
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	FILE* f = fopen(path, "r");
	if (!f) {
		return false;
	}

	char line[4096];
	if (!fgets(line, sizeof(line), f)) {
		fclose(f);
		return false;
	}
	fclose(f);

	char* rp = strrchr(line, ')');
	if (!rp || rp[1] != ' ') {
		return false;
	}

	char rest[4096];
	snprintf(rest, sizeof(rest), "%s", rp + 2);
	char* save = NULL;
	char* tok = strtok_r(rest, " ", &save);
	int field = 3;
	while (tok) {
		if (field == 22) {
			*start_time_ticks = strtoull(tok, NULL, 10);
			return true;
		}
		tok = strtok_r(NULL, " ", &save);
		field++;
	}
	return false;
}

static bool process_identity_still_matches(const Process* p) {
	unsigned long long current_start = 0;
	return p && read_process_start_time(p->pid, &current_start) && current_start == p->start_time_ticks;
}

static const char* signal_description(int sig) {
	switch (sig) {
	case SIGTERM:
		return "graceful termination request";
	case SIGKILL:
		return "immediate, uncatchable termination";
	case SIGINT:
		return "interrupt request";
	case SIGHUP:
		return "hangup / reload request";
	case SIGSTOP:
		return "unconditional process stop";
	case SIGCONT:
		return "continue a stopped process";
	default:
		return "signal";
	}
}

static bool confirm_process_signal(const Process* p, int sig, const char* sig_name) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int w = cols > 78 ? 76 : cols - 4;
	int h = 9;
	if (w < 46 || rows < h + 2) {
		return false;
	}

	int x = (cols - w) / 2;
	int y = (rows - h) / 2;
	WINDOW* win = newwin(h, w, y, x);
	if (!win) {
		return false;
	}
	keypad(win, TRUE);
	box(win, 0, 0);

	wattron(win, A_BOLD);
	mvwprintw(win, 1, 2, "Confirm signal");
	wattroff(win, A_BOLD);
	mvwprintw(win, 3, 2, "%s (%d) -> PID %d / %.36s", sig_name, sig, p->pid, p->command);
	mvwprintw(win, 4, 2, "%s", signal_description(sig));
	if (sig == SIGKILL) {
		wattron(win, COLOR_PAIR(CP_RED) | A_BOLD);
		mvwprintw(win, 5, 2, "SIGKILL cannot be caught, blocked, or cleaned up by the target.");
		wattroff(win, COLOR_PAIR(CP_RED) | A_BOLD);
	}
	mvwprintw(win, 7, 2, "Press y to send; any other key cancels");
	wrefresh(win);
	int ch = wgetch(win);
	delwin(win);
	touchwin(stdscr);
	return ch == 'y' || ch == 'Y';
}

static void send_process_signal(const Process* p, int sig, const char* sig_name, bool needs_confirmation, char* status, size_t status_n) {
	if (!p) {
		snprintf(status, status_n, "No process selected.");
		return;
	}

	if (!process_identity_still_matches(p)) {
		snprintf(status, status_n, "PID %d exited or changed before the signal could be sent.", p->pid);
		return;
	}

	if (needs_confirmation && !confirm_process_signal(p, sig, sig_name)) {
		snprintf(status, status_n, "%s to PID %d cancelled.", sig_name, p->pid);
		return;
	}

	// kill(2) is the syscall interface for delivering signals
	if (kill(p->pid, sig) == 0) {
		snprintf(status, status_n, "Sent %s (%d) to PID %d (%s).", sig_name, sig, p->pid, p->user);
		return;
	}

	int e = errno;
	if (e == EPERM) {
		snprintf(status, status_n, "Cannot signal PID %d: permission denied (try appropriate privileges).", p->pid);
	}
	else if (e == ESRCH) {
		snprintf(status, status_n, "Cannot signal PID %d: process no longer exists.", p->pid);
	}
	else {
		snprintf(status, status_n, "Cannot signal PID %d with %s: %s.", p->pid, sig_name, strerror(e));
	}
}

void show_signal_menu(Process* p, char* status, size_t status_n) {
	if (!p) {
		snprintf(status, status_n, "No process selected.");
		return;
	}

	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int w = cols > 76 ? 74 : cols - 4;
	int h = 15;
	if (w < 46 || rows < h + 2) {
		snprintf(status, status_n, "Terminal is too small for the signal menu.");
		return;
	}

	int x = (cols - w) / 2;
	int y = (rows - h) / 2;
	WINDOW* win = newwin(h, w, y, x);
	if (!win) {
		snprintf(status, status_n, "Unable to create signal menu.");
		return;
	}
	keypad(win, TRUE);
	box(win, 0, 0);
	wattron(win, A_BOLD);
	mvwprintw(win, 1, 2, "Signal PID %d", p->pid);
	wattroff(win, A_BOLD);
	mvwprintw(win, 2, 2, "%.60s", p->command);

	mvwprintw(win, 4, 2, "t  SIGTERM  terminate gracefully");
	mvwprintw(win, 5, 2, "k  SIGKILL  force kill immediately");
	mvwprintw(win, 6, 2, "i  SIGINT   interrupt");
	mvwprintw(win, 7, 2, "h  SIGHUP   hangup / app-defined reload");
	mvwprintw(win, 8, 2, "s  SIGSTOP  pause unconditionally");
	mvwprintw(win, 9, 2, "c  SIGCONT  resume a stopped process");
	mvwprintw(win, 11, 2, "Esc/q      cancel");
	mvwprintw(win, 13, 2, "Signals are subject to normal Linux ownership/capability checks.");
	wrefresh(win);

	int ch = wgetch(win);
	delwin(win);
	touchwin(stdscr);

	switch (ch) {
	case 't':
	case 'T':
		send_process_signal(p, SIGTERM, "SIGTERM", true, status, status_n);
		break;
	case 'k':
	case 'K':
		send_process_signal(p, SIGKILL, "SIGKILL", true, status, status_n);
		break;
	case 'i':
	case 'I':
		send_process_signal(p, SIGINT, "SIGINT", true, status, status_n);
		break;
	case 'h':
	case 'H':
		send_process_signal(p, SIGHUP, "SIGHUP", true, status, status_n);
		break;
	case 's':
	case 'S':
		send_process_signal(p, SIGSTOP, "SIGSTOP", true, status, status_n);
		break;
	case 'c':
	case 'C':
		send_process_signal(p, SIGCONT, "SIGCONT", false, status, status_n);
		break;
	default:
		snprintf(status, status_n, "Signal action cancelled.");
		break;
	}
}
