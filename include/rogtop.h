#ifndef ROGTOP_H
#define ROGTOP_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define MAX_CORES 256
#define HISTORY 160
#define FILTER_LEN 64
#define CMD_LEN 256

#define CP_GREEN 1
#define CP_YELLOW 2
#define CP_RED 3
#define CP_CYAN 4
#define CP_MAGENTA 5
#define CP_BLUE 6
#define CP_HEADER 7
#define CP_DIM 8

typedef struct {
	unsigned long long prev_total;
	unsigned long long prev_idle;
	double pct;
} CpuMeter;

typedef struct {
	double v[HISTORY];
	int count;
	int head;
} History;

typedef struct {
	pid_t pid;
	char user[20];
	char command[CMD_LEN];
	char state;
	long nice;
	unsigned long long cpu_ticks;
	unsigned long long start_time_ticks;
	unsigned long long rss_bytes;
	double cpu_pct;
	double mem_pct;
} Process;

typedef struct {
	pid_t pid;
	unsigned long long ticks;
} PrevProc;

typedef enum {
	SORT_CPU,
	SORT_MEM,
	SORT_PID,
	SORT_NAME
} SortKey;

typedef struct {
	int index;
	int score;
} VisibleEntry;

// util.c
void history_push(History* h, double value);
double history_get(const History* h, int idx_from_oldest);
void fmt_bytes(double bytes, char* buf, size_t n);
void fmt_rate(double bytes_per_sec, char* buf, size_t n);
void fmt_uptime(double seconds, char* buf, size_t n);

// procfs.c
bool read_cpu(CpuMeter* meters, int* core_count, unsigned long long* agg_delta_total);
bool read_mem(unsigned long long* mem_total, unsigned long long* mem_used, unsigned long long* swap_total, unsigned long long* swap_used);
bool read_load(double* l1, double* l5, double* l15);
double read_uptime(void);
bool read_net(unsigned long long* rx, unsigned long long* tx);
bool read_disk(unsigned long long* read_bytes, unsigned long long* write_bytes);
int cmp_prev_pid(const void* a, const void* b);
Process* read_processes(size_t* out_n, PrevProc* prev, size_t prev_n,
						unsigned long long total_delta, int core_count,
						unsigned long long mem_total, long page_size);

// process.c
void process_set_sort(SortKey key, bool descending);
void process_toggle_sort_direction(void);
const char* process_sort_name(void);
bool process_sort_descending(void);
void process_sort(Process* procs, size_t n);

// search.c
int fuzzy_score_text(const char* text, const char* query);
VisibleEntry* build_visible_map(Process* procs, size_t n, const char* filter, int* out_n);
Process* selected_process(Process* procs, size_t n, int selected, const char* filter);

// actions.c
void show_signal_menu(Process* p, char* status, size_t status_n);

// ui.c
void show_help(void);
void draw_dashboard(CpuMeter* cpu, int core_count,
					unsigned long long mem_total, unsigned long long mem_used,
					unsigned long long swap_total, unsigned long long swap_used,
					double l1, double l5, double l15, double uptime,
					double rx_rate, double tx_rate, double rd_rate, double wr_rate,
					const History* cpu_h, const History* mem_h, const History* load_h,
					const History* rx_h, const History* tx_h, const History* rd_h, const History* wr_h,
					double interval, const char* filter, bool search_mode);
int dashboard_end_row(int core_count, int cols, bool swap_present, bool filter_present, int rows);
void draw_processes(Process* procs, size_t n, int start_y, int* selected, int* scroll, const char* filter, const char* action_status, bool search_mode);

#endif
