#include "rogtop.h"

#include <stdio.h>

void history_push(History* h, double value) {
	h->v[h->head] = value;
	h->head = (h->head + 1) % HISTORY;
	if (h->count < HISTORY) {
		h->count++;
	}
}

double history_get(const History* h, int idx_from_oldest) {
	if (h->count <= 0) {
		return 0.0;
	}
	int start = (h->head - h->count + HISTORY) % HISTORY;
	return h->v[(start + idx_from_oldest) % HISTORY];
}

void fmt_bytes(double bytes, char* buf, size_t n) {
	const char* u[] = { "B", "K", "M", "G", "T", "P" };
	int i = 0;
	while (bytes >= 1024.0 && i < 5) {
		bytes /= 1024.0;
		i++;
	}
	if (bytes >= 100.0 || i == 0) {
		snprintf(buf, n, "%.0f%s", bytes, u[i]);
	}
	else if (bytes >= 10.0) {
		snprintf(buf, n, "%.1f%s", bytes, u[i]);
	}
	else {
		snprintf(buf, n, "%.2f%s", bytes, u[i]);
	}
}

void fmt_rate(double bytes_per_sec, char* buf, size_t n) {
	char tmp[24];
	fmt_bytes(bytes_per_sec, tmp, sizeof(tmp));
	snprintf(buf, n, "%s/s", tmp);
}

void fmt_uptime(double seconds, char* buf, size_t n) {
	long s = (long)seconds;
	long days = s / 86400;
	s %= 86400;
	long hours = s / 3600;
	s %= 3600;
	long mins = s / 60;
	if (days > 0) {
		snprintf(buf, n, "%ldd %02ld:%02ld", days, hours, mins);
	}
	else {
		snprintf(buf, n, "%02ld:%02ld", hours, mins);
	}
}
