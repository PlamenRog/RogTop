#include "rogtop.h"

#include <stdlib.h>
#include <strings.h>

static SortKey s_sort_key = SORT_CPU;
static bool s_sort_desc = true;

void process_set_sort(SortKey key, bool descending) {
	s_sort_key = key;
	s_sort_desc = descending;
}

void process_toggle_sort_direction(void) {
	s_sort_desc = !s_sort_desc;
}

const char* process_sort_name(void) {
	switch (s_sort_key) {
	case SORT_CPU:
		return "CPU";
	case SORT_MEM:
		return "MEM";
	case SORT_PID:
		return "PID";
	case SORT_NAME:
		return "NAME";
	}
	return "?";
}

bool process_sort_descending(void) {
	return s_sort_desc;
}

static int cmp_process(const void* a, const void* b) {
	const Process *pa = a, *pb = b;
	int r = 0;
	switch (s_sort_key) {
	case SORT_CPU:
		r = (pa->cpu_pct < pb->cpu_pct) - (pa->cpu_pct > pb->cpu_pct);
		break;
	case SORT_MEM:
		r = (pa->mem_pct < pb->mem_pct) - (pa->mem_pct > pb->mem_pct);
		break;
	case SORT_PID:
		r = (pa->pid > pb->pid) - (pa->pid < pb->pid);
		break;
	case SORT_NAME:
		r = strcasecmp(pa->command, pb->command);
		break;
	}
	if (r == 0) {
		r = (pa->pid > pb->pid) - (pa->pid < pb->pid);
	}
	return s_sort_desc ? -r : r;
}

void process_sort(Process* procs, size_t n) {
	if (procs && n > 1) {
		qsort(procs, n, sizeof(*procs), cmp_process);
	}
}
