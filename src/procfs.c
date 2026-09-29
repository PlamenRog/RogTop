#include "rogtop.h"

#include <ctype.h>
#include <dirent.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool read_cpu(CpuMeter* meters, int* core_count, unsigned long long* agg_delta_total) {
	FILE* f = fopen("/proc/stat", "r");
	if (!f) {
		return false;
	}
	char line[512];
	int idx = 0;
	*agg_delta_total = 0;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, "cpu", 3) != 0) {
			break;
		}
		if (idx > MAX_CORES) {
			break;
		}

		char name[16];
		unsigned long long user = 0, nicev = 0, sys = 0, idle = 0, iowait = 0, irq = 0, softirq = 0, steal = 0;
		int n = sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu", name, &user, &nicev, &sys, &idle, &iowait, &irq, &softirq, &steal);
		if (n < 5) {
			continue;
		}
		unsigned long long idle_all = idle + iowait;
		unsigned long long total = user + nicev + sys + idle + iowait + irq + softirq + steal;
		unsigned long long dt = total - meters[idx].prev_total;
		unsigned long long di = idle_all - meters[idx].prev_idle;
		if (meters[idx].prev_total != 0 && dt > 0) {
			meters[idx].pct = 100.0 * (double)(dt - di) / (double)dt;
		}
		else {
			meters[idx].pct = 0.0;
		}
		meters[idx].prev_total = total;
		meters[idx].prev_idle = idle_all;
		if (idx == 0) {
			*agg_delta_total = dt;
		}
		idx++;
	}
	fclose(f);
	*core_count = idx > 0 ? idx - 1 : 0;
	return idx > 0;
}

bool read_mem(unsigned long long* mem_total, unsigned long long* mem_used, unsigned long long* swap_total, unsigned long long* swap_used) {
	FILE* f = fopen("/proc/meminfo", "r");
	if (!f) {
		return false;
	}
	char line[256];
	char key[64];
	unsigned long long val;
	unsigned long long total = 0, avail = 0, st = 0, sf = 0;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%63s %llu", key, &val) != 2) {
			continue;
		}
		if (strcmp(key, "MemTotal:") == 0) {
			total = val * 1024ULL;
		}
		else if (strcmp(key, "MemAvailable:") == 0) {
			avail = val * 1024ULL;
		}
		else if (strcmp(key, "SwapTotal:") == 0) {
			st = val * 1024ULL;
		}
		else if (strcmp(key, "SwapFree:") == 0) {
			sf = val * 1024ULL;
		}
	}
	fclose(f);
	*mem_total = total;
	*mem_used = total > avail ? total - avail : 0;
	*swap_total = st;
	*swap_used = st > sf ? st - sf : 0;
	return total > 0;
}

bool read_load(double* l1, double* l5, double* l15) {
	FILE* f = fopen("/proc/loadavg", "r");
	if (!f) {
		return false;
	}
	int ok = fscanf(f, "%lf %lf %lf", l1, l5, l15) == 3;
	fclose(f);
	return ok;
}

double read_uptime(void) {
	FILE* f = fopen("/proc/uptime", "r");
	if (!f) {
		return 0.0;
	}
	double u = 0.0;
	fscanf(f, "%lf", &u);
	fclose(f);
	return u;
}

bool read_net(unsigned long long* rx, unsigned long long* tx) {
	FILE* f = fopen("/proc/net/dev", "r");
	if (!f) {
		return false;
	}
	char line[512];
	int line_no = 0;
	unsigned long long total_rx = 0, total_tx = 0;
	while (fgets(line, sizeof(line), f)) {
		if (++line_no <= 2) {
			continue;
		}
		char iface[64];
		unsigned long long rxb = 0, txb = 0;
		unsigned long long a, b, c, d, e, g, h;
		int n = sscanf(line, " %63[^:]: %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu", iface, &rxb, &a, &b, &c, &d, &e, &g, &h, &txb, &a, &b, &c, &d, &e, &g, &h);
		if (n == 17 && strcmp(iface, "lo") != 0) {
			total_rx += rxb;
			total_tx += txb;
		}
	}
	fclose(f);
	*rx = total_rx;
	*tx = total_tx;
	return true;
}

static bool is_partition(const char* name) {
	char path[256];
	snprintf(path, sizeof(path), "/sys/class/block/%s/partition", name);
	return access(path, F_OK) == 0;
}

bool read_disk(unsigned long long* read_bytes, unsigned long long* write_bytes) {
	FILE* f = fopen("/proc/diskstats", "r");
	if (!f) {
		return false;
	}
	char line[512];
	unsigned long long rb = 0, wb = 0;
	while (fgets(line, sizeof(line), f)) {
		unsigned major, minor;
		char name[64];
		unsigned long long reads, rmerge, rsect, rms;
		unsigned long long writes, wmerge, wsect, wms;
		int n = sscanf(line, "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu", &major, &minor, name, &reads, &rmerge, &rsect, &rms, &writes, &wmerge, &wsect, &wms);
		(void)major;
		(void)minor;
		(void)reads;
		(void)rmerge;
		(void)rms;
		(void)writes;
		(void)wmerge;
		(void)wms;
		if (n < 11) {
			continue;
		}
		if (strncmp(name, "loop", 4) == 0 || strncmp(name, "ram", 3) == 0 ||
			strncmp(name, "dm-", 3) == 0 || strncmp(name, "md", 2) == 0) {
			continue;
		}
		if (is_partition(name)) {
			continue;
		}
		rb += rsect * 512ULL;
		wb += wsect * 512ULL;
	}
	fclose(f);
	*read_bytes = rb;
	*write_bytes = wb;
	return true;
}

static PrevProc* find_prev(PrevProc* arr, size_t n, pid_t pid) {
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (arr[mid].pid == pid) {
			return &arr[mid];
		}
		if (arr[mid].pid < pid) {
			lo = mid + 1;
		}
		else {
			hi = mid;
		}
	}
	return NULL;
}

int cmp_prev_pid(const void* a, const void* b) {
	const PrevProc *pa = a, *pb = b;
	return (pa->pid > pb->pid) - (pa->pid < pb->pid);
}

static bool read_cmdline(pid_t pid, char* buf, size_t n) {
	char path[64];
	snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
	FILE* f = fopen(path, "r");
	if (!f) {
		return false;
	}
	size_t len = fread(buf, 1, n - 1, f);
	fclose(f);
	if (len == 0) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\0') {
			buf[i] = ' ';
		}
	}
	while (len > 0 && isspace((unsigned char)buf[len - 1])) {
		len--;
	}
	buf[len] = '\0';
	return len > 0;
}

static bool read_process(pid_t pid, Process* p, PrevProc* prev, size_t prev_n, unsigned long long total_delta, int core_count, unsigned long long mem_total, long page_size) {
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

	char* lp = strchr(line, '(');
	char* rp = strrchr(line, ')');
	if (!lp || !rp || rp <= lp) {
		return false;
	}

	char comm[CMD_LEN];
	size_t comm_len = (size_t)(rp - lp - 1);
	if (comm_len >= sizeof(comm)) {
		comm_len = sizeof(comm) - 1;
	}
	memcpy(comm, lp + 1, comm_len);
	comm[comm_len] = '\0';

	char rest[4096];
	snprintf(rest, sizeof(rest), "%s", rp + 2);
	char* save = NULL;
	char* tok = strtok_r(rest, " ", &save);
	int field = 3;
	char state = '?';
	unsigned long long utime = 0, stime = 0;
	unsigned long long start_time_ticks = 0;
	long nicev = 0;
	long long rss_pages = 0;
	while (tok) {
		if (field == 3) {
			state = tok[0];
		}
		else if (field == 14) {
			utime = strtoull(tok, NULL, 10);
		}
		else if (field == 15) {
			stime = strtoull(tok, NULL, 10);
		}
		else if (field == 19) {
			nicev = strtol(tok, NULL, 10);
		}
		else if (field == 22) {
			start_time_ticks = strtoull(tok, NULL, 10);
		}
		else if (field == 24) {
			rss_pages = strtoll(tok, NULL, 10);
		}
		tok = strtok_r(NULL, " ", &save);
		field++;
	}
	if (field <= 24) {
		return false;
	}

	memset(p, 0, sizeof(*p));
	p->pid = pid;
	p->state = state;
	p->nice = nicev;
	p->cpu_ticks = utime + stime;
	p->start_time_ticks = start_time_ticks;
	if (rss_pages < 0) {
		rss_pages = 0;
	}
	p->rss_bytes = (unsigned long long)rss_pages * (unsigned long long)page_size;
	p->mem_pct = mem_total ? (100.0 * (double)p->rss_bytes / (double)mem_total) : 0.0;

	PrevProc* old = find_prev(prev, prev_n, pid);
	if (old && total_delta > 0 && p->cpu_ticks >= old->ticks) {
		unsigned long long dp = p->cpu_ticks - old->ticks;
		p->cpu_pct = 100.0 * (double)dp / (double)total_delta * (double)(core_count > 0 ? core_count : 1);
	}

	struct stat st;
	snprintf(path, sizeof(path), "/proc/%d", pid);
	if (stat(path, &st) == 0) {
		struct passwd* pw = getpwuid(st.st_uid);
		if (pw) {
			snprintf(p->user, sizeof(p->user), "%s", pw->pw_name);
		}
		else {
			snprintf(p->user, sizeof(p->user), "%u", (unsigned)st.st_uid);
		}
	}
	else {
		snprintf(p->user, sizeof(p->user), "?");
	}

	if (!read_cmdline(pid, p->command, sizeof(p->command))) {
		snprintf(p->command, sizeof(p->command), "[%.252s]", comm);
	}
	return true;
}

Process* read_processes(size_t* out_n, PrevProc* prev, size_t prev_n, unsigned long long total_delta, int core_count, unsigned long long mem_total, long page_size) {
	DIR* d = opendir("/proc");
	if (!d) {
		return NULL;
	}
	size_t cap = 256, n = 0;
	Process* arr = malloc(cap * sizeof(*arr));
	if (!arr) {
		closedir(d);
		return NULL;
	}
	struct dirent* de;
	while ((de = readdir(d)) != NULL) {
		if (!isdigit((unsigned char)de->d_name[0])) {
			continue;
		}
		char* end = NULL;
		long v = strtol(de->d_name, &end, 10);
		if (!end || *end != '\0' || v <= 0) {
			continue;
		}
		if (n == cap) {
			cap *= 2;
			Process* tmp = realloc(arr, cap * sizeof(*arr));
			if (!tmp) {
				break;
			}
			arr = tmp;
		}
		if (read_process((pid_t)v, &arr[n], prev, prev_n, total_delta, core_count, mem_total, page_size)) {
			n++;
		}
	}
	closedir(d);
	*out_n = n;
	return arr;
}
