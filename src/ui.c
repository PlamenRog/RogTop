#include "rogtop.h"

#include <math.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int color_for_pct(double p) {
	if (p >= 85.0) {
		return CP_RED;
	}
	if (p >= 60.0) {
		return CP_YELLOW;
	}
	return CP_GREEN;
}

static void safe_addnstr(int y, int x, const char* s, int n) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	if (y < 0 || y >= rows || x < 0 || x >= cols || n <= 0) {
		return;
	}
	if (x + n > cols) {
		n = cols - x;
	}
	if (n > 0) {
		mvaddnstr(y, x, s, n);
	}
}

static void draw_bar(int y, int x, int width, const char* label, double pct, int cp) {
	if (width < 20) {
		return;
	}
	if (pct < 0) {
		pct = 0;
	}
	double shown = pct;
	if (shown > 999.9) {
		shown = 999.9;
	}

	char left[24];
	snprintf(left, sizeof(left), "%-6s", label);
	safe_addnstr(y, x, left, 6);

	int value_w = 7;
	int bar_w = width - 6 - value_w - 2;
	if (bar_w < 4) {
		return;
	}
	int filled = (int)lround((pct > 100.0 ? 100.0 : pct) * bar_w / 100.0);

	mvaddch(y, x + 6, '[');
	for (int i = 0; i < bar_w; i++) {
		if (i < filled) {
			attron(COLOR_PAIR(cp) | A_BOLD);
			mvaddch(y, x + 7 + i, '#');
			attroff(COLOR_PAIR(cp) | A_BOLD);
		}
		else {
			attron(COLOR_PAIR(CP_DIM));
			mvaddch(y, x + 7 + i, '.');
			attroff(COLOR_PAIR(CP_DIM));
		}
	}
	mvaddch(y, x + 7 + bar_w, ']');
	char val[16];
	snprintf(val, sizeof(val), "%6.1f%%", shown);
	safe_addnstr(y, x + 8 + bar_w, val, value_w);
}

static void draw_history(int y, int x, int width, const char* label, const History* h, double floor_max, const char* value_text, int cp) {
	if (width < 24) {
		return;
	}
	int label_w = 8;
	int value_w = 13;
	int graph_w = width - label_w - value_w - 2;
	if (graph_w < 5) {
		return;
	}

	char lbuf[16];
	snprintf(lbuf, sizeof(lbuf), "%-7s", label);
	safe_addnstr(y, x, lbuf, label_w - 1);
	mvaddch(y, x + label_w - 1, '[');

	double maxv = floor_max;
	int start = h->count > graph_w ? h->count - graph_w : 0;
	for (int i = start; i < h->count; i++) {
		double v = history_get(h, i);
		if (v > maxv) {
			maxv = v;
		}
	}
	if (maxv <= 0.0) {
		maxv = 1.0;
	}

	static const char levels[] = " .:-=+*#%@";
	int level_count = (int)strlen(levels) - 1;
	int blank = graph_w - (h->count - start);
	for (int i = 0; i < blank; i++) {
		attron(COLOR_PAIR(CP_DIM));
		mvaddch(y, x + label_w + i, ' ');
		attroff(COLOR_PAIR(CP_DIM));
	}
	int pos = blank;
	for (int i = start; i < h->count && pos < graph_w; i++, pos++) {
		double v = history_get(h, i);
		int lev = (int)lround((v / maxv) * level_count);
		if (lev < 0) {
			lev = 0;
		}
		if (lev > level_count) {
			lev = level_count;
		}
		attron(COLOR_PAIR(cp) | (lev > level_count * 2 / 3 ? A_BOLD : 0));
		mvaddch(y, x + label_w + pos, levels[lev]);
		attroff(COLOR_PAIR(cp) | A_BOLD);
	}
	mvaddch(y, x + label_w + graph_w, ']');
	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), " %11s", value_text);
	safe_addnstr(y, x + label_w + graph_w + 1, vbuf, value_w);
}

static void help_row(WINDOW* win, int row, int width, const char* key, const char* description) {
	const int key_x = 2;
	const int key_width = 16;
	const int desc_x = key_x + key_width;
	int desc_width = width - desc_x - 2;

	if (desc_width <= 0) {
		return;
	}

	if (key && key[0] != '\0') {
		mvwaddnstr(win, row, key_x, key, key_width - 1);
	}
	mvwaddnstr(win, row, desc_x, description, desc_width);
}

void show_help(void) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	int w = cols > 78 ? 76 : cols - 4;
	int h = 21;
	if (w < 44 || rows < h + 2) {
		return;
	}
	int x = (cols - w) / 2;
	int y = (rows - h) / 2;
	WINDOW* win = newwin(h, w, y, x);
	if (!win) {
		return;
	}
	box(win, 0, 0);

	help_row(win, 3, w, "q", "quit");
	help_row(win, 4, w, "Up/Down or j/k", "select process");
	help_row(win, 5, w, "PgUp/PgDn", "page through processes");
	help_row(win, 6, w, "c/m/p/n", "sort CPU / memory / PID / name");
	help_row(win, 7, w, "s", "reverse sort order");
	help_row(win, 8, w, "/", "live fuzzy search PID/user/command");
	help_row(win, 9, w, "", "Backspace edits; Ctrl-U clears while searching");
	help_row(win, 10, w, "", "Enter accepts; Esc restores the previous filter");
	help_row(win, 11, w, "x", "clear the active filter");
	help_row(win, 12, w, "a or F9", "process actions / signals");
	help_row(win, 13, w, "  t", "SIGTERM (terminate gracefully)");
	help_row(win, 14, w, "  k", "SIGKILL (force kill)");
	help_row(win, 15, w, "  i/h/s/c", "SIGINT / SIGHUP / SIGSTOP / SIGCONT");
	help_row(win, 16, w, "+ / -", "faster / slower refresh");
	help_row(win, 17, w, "? or h", "this help");

	mvwaddnstr(win, 19, 2, "Press any key to close", w - 4);
	wrefresh(win);
	wgetch(win);
	delwin(win);
	touchwin(stdscr);
}

void draw_dashboard(CpuMeter* cpu, int core_count, unsigned long long mem_total,
                    unsigned long long mem_used, unsigned long long swap_total,
                    unsigned long long swap_used, double l1, double l5, double l15,
                    double uptime, double rx_rate, double tx_rate, double rd_rate,
                    double wr_rate, const History* cpu_h, const History* mem_h,
                    const History* load_h, const History* rx_h, const History* tx_h,
                    const History* rd_h, const History* wr_h, double interval, const char* filter, bool search_mode) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	erase();

	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	char tbuf[32], ubuf[32];
	strftime(tbuf, sizeof(tbuf), "%H:%M:%S", &tm);
	fmt_uptime(uptime, ubuf, sizeof(ubuf));

	attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
	mvhline(0, 0, ' ', cols);
	attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);
	char right[128];
	snprintf(right, sizeof(right), "%s  uptime %s  %.2fs", tbuf, ubuf, interval);
	int rx = cols - (int)strlen(right) - 1;
	if (rx > 12) {
		mvaddnstr(0, rx, right, cols - rx - 1);
	}

	int y = 1;
	draw_bar(y++, 1, cols - 2, "CPU", cpu[0].pct, color_for_pct(cpu[0].pct));

	double mem_pct = mem_total ? 100.0 * (double)mem_used / (double)mem_total : 0.0;
	draw_bar(y++, 1, cols - 2, "MEM", mem_pct, color_for_pct(mem_pct));
	if (swap_total > 0) {
		double sp = 100.0 * (double)swap_used / (double)swap_total;
		draw_bar(y++, 1, cols - 2, "SWAP", sp, color_for_pct(sp));
	}

	int core_rows = core_count > 0 ? (core_count + 3) / 4 : 0;
	if (core_rows > 2) {
		core_rows = 2;
	}
	if (cols >= 80 && core_rows > 0 && y + core_rows + 6 < rows) {
		int slots = 4;
		int slot_w = (cols - 2) / slots;
		int max_show = core_rows * slots;
		for (int i = 0; i < core_count && i < max_show; i++) {
			int cy = y + i / slots;
			int cx = 1 + (i % slots) * slot_w;
			char lab[12];
			snprintf(lab, sizeof(lab), "C%-3d", i);
			draw_bar(cy, cx, slot_w - 1, lab, cpu[i + 1].pct, color_for_pct(cpu[i + 1].pct));
		}
		y += core_rows;
	}

	char val[64], a[24], b[24];
	snprintf(val, sizeof(val), "%4.1f%%", cpu[0].pct);
	draw_history(y++, 1, cols - 2, "CPU hist", cpu_h, 100.0, val, CP_GREEN);

	snprintf(val, sizeof(val), "%4.1f%%", mem_pct);
	draw_history(y++, 1, cols - 2, "MEM hist", mem_h, 100.0, val, CP_CYAN);

	snprintf(val, sizeof(val), "%.2f %.2f %.2f", l1, l5, l15);
	draw_history(y++, 1, cols - 2, "LOAD", load_h, core_count > 0 ? core_count : 1, val, CP_YELLOW);

	fmt_rate(rx_rate, a, sizeof(a));
	fmt_rate(tx_rate, b, sizeof(b));
	snprintf(val, sizeof(val), "R %s", a);
	draw_history(y++, 1, cols - 2, "NET rx", rx_h, 1024.0, val, CP_BLUE);
	snprintf(val, sizeof(val), "T %s", b);
	draw_history(y++, 1, cols - 2, "NET tx", tx_h, 1024.0, val, CP_MAGENTA);

	fmt_rate(rd_rate, a, sizeof(a));
	fmt_rate(wr_rate, b, sizeof(b));
	snprintf(val, sizeof(val), "R %s", a);
	draw_history(y++, 1, cols - 2, "DISK rd", rd_h, 1024.0, val, CP_CYAN);
	snprintf(val, sizeof(val), "W %s", b);
	draw_history(y++, 1, cols - 2, "DISK wr", wr_h, 1024.0, val, CP_YELLOW);

	if ((filter[0] || search_mode) && y < rows - 3) {
		attron(COLOR_PAIR(CP_MAGENTA) | A_BOLD);
		if (search_mode) {
			mvprintw(y++, 1, "fuzzy search: %s_", filter);
		}
		else {
			mvprintw(y++, 1, "fuzzy filter: %s", filter);
		}
		attroff(COLOR_PAIR(CP_MAGENTA) | A_BOLD);
	}
}

int dashboard_end_row(int core_count, int cols, bool swap_present, bool filter_present, int rows) {
	int y = 1 + 2 + (swap_present ? 1 : 0);
	int core_rows = core_count > 0 ? (core_count + 3) / 4 : 0;
	if (core_rows > 2) {
		core_rows = 2;
	}
	if (cols >= 80 && core_rows > 0 && y + core_rows + 6 < rows) {
		y += core_rows;
	}
	y += 7;
	if (filter_present) {
		y += 1;
	}
	return y;
}

void draw_processes(Process* procs, size_t n, int start_y, int* selected, int* scroll, const char* filter, const char* action_status, bool search_mode) {
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	if (start_y >= rows - 2) {
		return;
	}

	int visible_cap = rows - start_y - 2;
	int fn = 0;
	VisibleEntry* map = build_visible_map(procs, n, filter, &fn);
	if (!map) {
		return;
	}

	if (fn == 0) {
		*selected = 0;
		*scroll = 0;
	}
	else {
		if (*selected < 0) {
			*selected = 0;
		}
		if (*selected >= fn) {
			*selected = fn - 1;
		}
		if (*scroll > *selected) {
			*scroll = *selected;
		}
		if (*selected >= *scroll + visible_cap) {
			*scroll = *selected - visible_cap + 1;
		}
		if (*scroll < 0) {
			*scroll = 0;
		}
		if (*scroll > fn - 1) {
			*scroll = fn - 1;
		}
	}

	attron(COLOR_PAIR(CP_HEADER) | A_BOLD);
	mvhline(start_y, 0, ' ', cols);
	char head[256];
	snprintf(head, sizeof(head), " PID    USER       CPU%%   MEM%%      RSS NI S  COMMAND");
	mvaddnstr(start_y, 0, head, cols);
	attroff(COLOR_PAIR(CP_HEADER) | A_BOLD);

	int cmd_x = 47;
	for (int row = 0; row < visible_cap; row++) {
		int fi = *scroll + row;
		int y = start_y + 1 + row;
		move(y, 0);
		clrtoeol();
		if (fi >= fn) {
			continue;
		}
		Process* p = &procs[map[fi].index];
		bool sel = fi == *selected;
		if (sel) {
			attron(A_REVERSE);
		}

		char rss[24];
		fmt_bytes((double)p->rss_bytes, rss, sizeof(rss));
		mvprintw(y, 0, "%6d  %-8.8s %6.1f %6.1f %8s %2ld %c  ", p->pid, p->user, p->cpu_pct, p->mem_pct, rss, p->nice, p->state);
		if (cmd_x < cols) {
			mvaddnstr(y, cmd_x, p->command, cols - cmd_x - 1);
		}
		if (sel) {
			attroff(A_REVERSE);
		}
	}

	attron(COLOR_PAIR(CP_HEADER));
	mvhline(rows - 1, 0, ' ', cols);
	char status[320];
	if (search_mode) {
		snprintf(status, sizeof(status), " fuzzy search live | type to filter | Enter accept | Esc cancel | Backspace edit | Ctrl-U clear | %d/%zu shown ", fn, n);
	}
	else if (action_status && action_status[0]) {
		snprintf(status, sizeof(status), " %s | j/k navigate | a/F9 actions | ? help ", action_status);
	}
	else {
		snprintf(status, sizeof(status), " q quit | j/k navigate | a/F9 actions | ? help | sort %s %s | / fuzzy search | %d/%zu shown | refresh +/- ", process_sort_name(), process_sort_descending() ? "desc" : "asc", fn, n);
	}
	mvaddnstr(rows - 1, 0, status, cols - 1);
	attroff(COLOR_PAIR(CP_HEADER));
	free(map);
}
