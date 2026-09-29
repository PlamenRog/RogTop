#include "rogtop.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int fuzzy_score_text(const char* text, const char* query) {
	if (!query[0]) {
		return 0;
	}

	int score = 0;
	int consecutive = 0;
	int first_match = -1;
	int last_match = -2;
	size_t qi = 0;

	for (size_t ti = 0; text[ti] && query[qi]; ti++) {
		unsigned char tc = (unsigned char)text[ti];
		unsigned char qc = (unsigned char)query[qi];
		if (tolower(tc) != tolower(qc)) {
			continue;
		}

		if (first_match < 0) {
			first_match = (int)ti;
		}
		if ((int)ti == last_match + 1) {
			consecutive++;
			score += 12 + consecutive * 3;
		}
		else {
			consecutive = 0;
			score += 10;
		}

		if (ti == 0 || text[ti - 1] == '/' || text[ti - 1] == '-' ||
			text[ti - 1] == '_' || text[ti - 1] == ' ' || text[ti - 1] == '\t') {
			score += 18;
		}

		last_match = (int)ti;
		qi++;
	}

	if (query[qi]) {
		return -1;
	}

	score -= first_match >= 0 ? first_match : 0;
	if (last_match >= first_match) {
		score -= (last_match - first_match) / 2;
	}

	// prioritize exact substring matches
	size_t qlen = strlen(query);
	for (size_t ti = 0; text[ti]; ti++) {
		size_t j = 0;
		while (j < qlen && text[ti + j] &&
			   tolower((unsigned char)text[ti + j]) == tolower((unsigned char)query[j])) {
			j++;
		}
		if (j == qlen) {
			score += 120 - ((int)ti < 100 ? (int)ti : 100);
			break;
		}
	}
	return score;
}

static int process_match_score(const Process* p, const char* filter) {
	if (!filter[0]) {
		return 0;
	}

	char searchable[CMD_LEN + 96];
	snprintf(searchable, sizeof(searchable), "%d %s %s", p->pid, p->user, p->command);
	return fuzzy_score_text(searchable, filter);
}

static int cmp_visible_entry(const void* a, const void* b) {
	const VisibleEntry* va = a;
	const VisibleEntry* vb = b;
	if (va->score != vb->score) {
		return (vb->score > va->score) - (vb->score < va->score);
	}
	return (va->index > vb->index) - (va->index < vb->index);
}

VisibleEntry* build_visible_map(Process* procs, size_t n, const char* filter, int* out_n) {
	VisibleEntry* map = malloc((n ? n : 1) * sizeof(*map));
	if (!map) {
		*out_n = 0;
		return NULL;
	}

	int fn = 0;
	for (size_t i = 0; i < n; i++) {
		int score = process_match_score(&procs[i], filter);
		if (score < 0) {
			continue;
		}
		map[fn].index = (int)i;
		map[fn].score = score;
		fn++;
	}
	if (filter[0] && fn > 1) {
		qsort(map, (size_t)fn, sizeof(*map), cmp_visible_entry);
	}
	*out_n = fn;
	return map;
}

Process* selected_process(Process* procs, size_t n, int selected, const char* filter) {
	if (!procs || selected < 0) {
		return NULL;
	}
	int fn = 0;
	VisibleEntry* map = build_visible_map(procs, n, filter, &fn);
	if (!map || selected >= fn) {
		free(map);
		return NULL;
	}
	Process* p = &procs[map[selected].index];
	free(map);
	return p;
}
