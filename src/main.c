#include "rogtop.h"

#include <ctype.h>
#include <math.h>
#include <ncurses.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>

static double timespec_diff_seconds(const struct timespec* a, const struct timespec* b) {
	return (double)(a->tv_sec - b->tv_sec) + (double)(a->tv_nsec - b->tv_nsec) / 1e9;
}

static struct timespec timespec_add_seconds(struct timespec t, double seconds) {
	time_t whole = (time_t)seconds;
	long nanos = (long)((seconds - (double)whole) * 1e9);

	t.tv_sec += whole;
	t.tv_nsec += nanos;
	if (t.tv_nsec >= 1000000000L) {
		t.tv_sec++;
		t.tv_nsec -= 1000000000L;
	}
	return t;
}

static int milliseconds_until(const struct timespec* deadline, const struct timespec* now) {
	double seconds = timespec_diff_seconds(deadline, now);
	if (seconds <= 0.0) {
		return 0;
	}

	double ms = ceil(seconds * 1000.0);
	if (ms > INT_MAX) {
		return INT_MAX;
	}
	return (int)ms;
}

int main(void) {
	CpuMeter cpu[MAX_CORES + 1];
	memset(cpu, 0, sizeof(cpu));
	int core_count = 0;
	unsigned long long total_delta = 0;

	History cpu_h = { 0 }, mem_h = { 0 }, load_h = { 0 };
	History rx_h = { 0 }, tx_h = { 0 }, rd_h = { 0 }, wr_h = { 0 };

	PrevProc* prev = NULL;
	size_t prev_n = 0;
	Process* procs = NULL;
	size_t proc_n = 0;

	long page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0) {
		page_size = 4096;
	}

	unsigned long long old_rx = 0, old_tx = 0, old_rd = 0, old_wr = 0;
	struct timespec last_sample_ts;
	clock_gettime(CLOCK_MONOTONIC, &last_sample_ts);
	bool have_rates = false;

	unsigned long long mem_total = 0, mem_used = 0, swap_total = 0, swap_used = 0;
	double l1 = 0, l5 = 0, l15 = 0, uptime = 0;
	double rx_rate = 0, tx_rate = 0, rd_rate = 0, wr_rate = 0;

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	if (has_colors()) {
		start_color();
		use_default_colors();
		init_pair(CP_GREEN, COLOR_GREEN, -1);
		init_pair(CP_YELLOW, COLOR_YELLOW, -1);
		init_pair(CP_RED, COLOR_RED, -1);
		init_pair(CP_CYAN, COLOR_CYAN, -1);
		init_pair(CP_MAGENTA, COLOR_MAGENTA, -1);
		init_pair(CP_BLUE, COLOR_BLUE, -1);
		init_pair(CP_HEADER, COLOR_BLACK, COLOR_CYAN);
		init_pair(CP_DIM, COLOR_WHITE, -1);
	}

	double interval = 1.0;
	int selected = 0, scroll = 0;
	char filter[FILTER_LEN] = { 0 };
	char search_original[FILTER_LEN] = { 0 };
	bool search_mode = false;
	char action_status[256] = { 0 };
	bool running = true;

	struct timespec next_sample = last_sample_ts; // first sample is immidiate

	while (running) {
		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);

		// resampling is timer driven
		if (timespec_diff_seconds(&now, &next_sample) >= 0.0) {
			unsigned long long rx = 0, tx = 0, rd = 0, wr = 0;

			read_cpu(cpu, &core_count, &total_delta);
			read_mem(&mem_total, &mem_used, &swap_total, &swap_used);
			read_load(&l1, &l5, &l15);
			uptime = read_uptime();
			read_net(&rx, &tx);
			read_disk(&rd, &wr);

			double elapsed = timespec_diff_seconds(&now, &last_sample_ts);
			rx_rate = tx_rate = rd_rate = wr_rate = 0.0;
			if (have_rates && elapsed > 0.001) {
				rx_rate = rx >= old_rx ? (double)(rx - old_rx) / elapsed : 0.0;
				tx_rate = tx >= old_tx ? (double)(tx - old_tx) / elapsed : 0.0;
				rd_rate = rd >= old_rd ? (double)(rd - old_rd) / elapsed : 0.0;
				wr_rate = wr >= old_wr ? (double)(wr - old_wr) / elapsed : 0.0;
			}
			old_rx = rx;
			old_tx = tx;
			old_rd = rd;
			old_wr = wr;
			last_sample_ts = now;
			have_rates = true;

			double mem_pct = mem_total ? 100.0 * (double)mem_used / (double)mem_total : 0.0;
			history_push(&cpu_h, cpu[0].pct);
			history_push(&mem_h, mem_pct);
			history_push(&load_h, l1);
			history_push(&rx_h, rx_rate);
			history_push(&tx_h, tx_rate);
			history_push(&rd_h, rd_rate);
			history_push(&wr_h, wr_rate);

			if (prev && prev_n > 1) {
				qsort(prev, prev_n, sizeof(*prev), cmp_prev_pid);
			}
			size_t new_proc_n = 0;
			Process* new_procs = read_processes(&new_proc_n, prev, prev_n, total_delta, core_count, mem_total, page_size);
			if (!new_procs) {
				new_proc_n = 0;
			}

			free(prev);
			prev = NULL;
			prev_n = new_proc_n;
			if (new_proc_n > 0) {
				prev = malloc(new_proc_n * sizeof(*prev));
				if (prev) {
					for (size_t i = 0; i < new_proc_n; i++) {
						prev[i].pid = new_procs[i].pid;
						prev[i].ticks = new_procs[i].cpu_ticks;
					}
				}
				else {
					prev_n = 0;
				}
			}

			free(procs);
			procs = new_procs;
			proc_n = new_proc_n;
			process_sort(procs, proc_n);

			next_sample = timespec_add_seconds(now, interval);
		}

		draw_dashboard(cpu, core_count, mem_total, mem_used, swap_total, swap_used, l1, l5, l15, uptime, rx_rate, tx_rate, rd_rate, wr_rate, &cpu_h, &mem_h, &load_h, &rx_h, &tx_h, &rd_h, &wr_h, interval, filter, search_mode);
		int rows, cols;
		getmaxyx(stdscr, rows, cols);
		int start_y = dashboard_end_row(core_count, cols, swap_total > 0, filter[0] != '\0' || search_mode, rows);
		draw_processes(procs, proc_n, start_y, &selected, &scroll, filter, action_status, search_mode);
		refresh();

		clock_gettime(CLOCK_MONOTONIC, &now);
		timeout(milliseconds_until(&next_sample, &now));
		int ch = getch();

		/* ERR means the timer expired; the next loop performs the sample. */
		if (ch == ERR) {
			continue;
		}

		int page = rows - start_y - 2;
		if (page < 1) {
			page = 1;
		}

		if (search_mode) {
			size_t len = strlen(filter);
			bool changed = false;

			if (ch == '\n' || ch == '\r') {
				search_mode = false;
				if (filter[0]) {
					snprintf(action_status, sizeof(action_status), "Fuzzy filter active: %s", filter);
				}
				else {
					snprintf(action_status, sizeof(action_status), "Fuzzy filter cleared.");
				}
			}
			else if (ch == 27) {
				snprintf(filter, sizeof(filter), "%s", search_original);
				search_mode = false;
				selected = scroll = 0;
				snprintf(action_status, sizeof(action_status), "Search cancelled.");
			}
			else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
				if (len > 0) {
					filter[len - 1] = '\0';
					changed = true;
				}
			}
			else if (ch == 21) { /* Ctrl-U */
				if (filter[0]) {
					filter[0] = '\0';
					changed = true;
				}
			}
			else if (ch >= 0 && ch <= 255 && isprint((unsigned char)ch)) {
				if (len + 1 < sizeof(filter)) {
					filter[len] = (char)ch;
					filter[len + 1] = '\0';
					changed = true;
				}
			}

			if (changed) {
				selected = scroll = 0;
				action_status[0] = '\0';
			}
			continue;
		}

		switch (ch) {
		case 'q':
		case 'Q':
			running = false;
			break;
		case KEY_UP:
		case 'k':
			if (selected > 0) {
				selected--;
			}
			action_status[0] = '\0';
			break;
		case KEY_DOWN:
		case 'j':
			selected++;
			action_status[0] = '\0';
			break;
		case KEY_PPAGE:
			selected -= page;
			if (selected < 0) {
				selected = 0;
			}
			action_status[0] = '\0';
			break;
		case KEY_NPAGE:
			selected += page;
			action_status[0] = '\0';
			break;
		case 'c':
		case 'C':
			process_set_sort(SORT_CPU, true);
			process_sort(procs, proc_n);
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case 'm':
		case 'M':
			process_set_sort(SORT_MEM, true);
			process_sort(procs, proc_n);
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case 'p':
		case 'P':
			process_set_sort(SORT_PID, false);
			process_sort(procs, proc_n);
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case 'n':
		case 'N':
			process_set_sort(SORT_NAME, false);
			process_sort(procs, proc_n);
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case 's':
		case 'S':
			process_toggle_sort_direction();
			process_sort(procs, proc_n);
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case '/':
			snprintf(search_original, sizeof(search_original), "%s", filter);
			search_mode = true;
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case 'x':
		case 'X':
			filter[0] = '\0';
			selected = scroll = 0;
			action_status[0] = '\0';
			break;
		case 'a':
		case 'A':
		case KEY_F(9):
			snprintf(action_status, sizeof(action_status), "Opening actions for selected process...");
			show_signal_menu(selected_process(procs, proc_n, selected, filter),
							 action_status, sizeof(action_status));
			break;
		case '+':
		case '=': {
			interval -= 0.25;
			if (interval < 0.25) {
				interval = 0.25;
			}
			struct timespec changed_at;
			clock_gettime(CLOCK_MONOTONIC, &changed_at);
			next_sample = timespec_add_seconds(changed_at, interval);
			action_status[0] = '\0';
			break;
		}
		case '-':
		case '_': {
			interval += 0.25;
			if (interval > 5.0) {
				interval = 5.0;
			}
			struct timespec changed_at;
			clock_gettime(CLOCK_MONOTONIC, &changed_at);
			next_sample = timespec_add_seconds(changed_at, interval);
			action_status[0] = '\0';
			break;
		}
		case '?':
		case 'h':
		case 'H':
			show_help();
			break;
		default:
			break;
		}
	}

	free(procs);
	free(prev);
	endwin();
	return 0;
}
