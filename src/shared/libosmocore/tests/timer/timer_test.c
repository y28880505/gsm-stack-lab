/*
 * (C) 2008 by Holger Hans Peter Freyther <zecke@selfish.org>
 * (C) 2011 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
 *
 * Authors: Holger Hans Peter Freyther <zecke@selfish.org>
 *	    Pablo Neira Ayuso <pablo@gnumonks.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>

#include <osmocom/core/talloc.h>
#include <osmocom/core/timer.h>
#include <osmocom/core/select.h>
#include <osmocom/core/linuxlist.h>

#include "../../config.h"

static void main_timer_fired(void *data);
static void secondary_timer_fired(void *data);

static unsigned int main_timer_step = 0;
static struct osmo_timer_list main_timer = {
	.cb = main_timer_fired,
	.data = &main_timer_step,
};

static LLIST_HEAD(timer_test_list);

struct test_timer {
	struct llist_head head;
	struct osmo_timer_list timer;
	struct timeval start;
	struct timeval stop;
};

/* number of test steps. We add fact(steps) timers in the whole test. */
#define MAIN_TIMER_NSTEPS	16

/* time between two steps, in secs. */
#define TIME_BETWEEN_STEPS	1

/* timer imprecision that we accept for this test: 10 milliseconds. */
#define TIMER_PRES_SECS		0
#define TIMER_PRES_USECS	10000

static unsigned int expired_timers = 0;
static unsigned int total_timers = 0;
static unsigned int too_late = 0;

static void main_timer_fired(void *data)
{
	unsigned int *step = data;
	unsigned int add_in_this_step;
	int i;

	if (*step == MAIN_TIMER_NSTEPS) {
		printf("Main timer has finished, please, wait a bit for the "
		       "final report.\n");
		return;
	}
	/* add 2^step pair of timers per step. */
	add_in_this_step = (1 << *step);

	for (i=0; i<add_in_this_step; i++) {
		struct test_timer *v;

		v = talloc_zero(NULL, struct test_timer);
		if (v == NULL) {
			fprintf(stderr, "timer_test: OOM!\n");
			return;
		}
		gettimeofday(&v->start, NULL);
		v->timer.cb = secondary_timer_fired;
		v->timer.data = v;
		unsigned int seconds = (random() % 10) + 1;
		v->stop.tv_sec = v->start.tv_sec + seconds;
		osmo_timer_schedule(&v->timer, seconds, 0);
		llist_add(&v->head, &timer_test_list);
	}
	printf("added %d timers in step %u (expired=%u)\n",
		add_in_this_step, *step, expired_timers);
	total_timers += add_in_this_step;
	osmo_timer_schedule(&main_timer, TIME_BETWEEN_STEPS, 0);
	(*step)++;
}

static void secondary_timer_fired(void *data)
{
	struct test_timer *v = data, *this, *tmp;
	struct timeval current, res, precision = { 1, 0 };

	gettimeofday(&current, NULL);

	timersub(&current, &v->stop, &res);
	if (timercmp(&res, &precision, >)) {
		printf("ERROR: timer %p has expired too late!\n", v->timer);
		too_late++;
	}

	llist_del(&v->head);
	talloc_free(data);
	expired_timers++;
	if (expired_timers == total_timers) {
		printf("test over: added=%u expired=%u too_late=%u \n",
			total_timers, expired_timers, too_late);
		exit(EXIT_SUCCESS);
	}

	/* randomly (10%) deletion of timers. */
	llist_for_each_entry_safe(this, tmp, &timer_test_list, head) {
		if ((random() % 100) < 10) {
			osmo_timer_del(&this->timer);
			llist_del(&this->head);
			talloc_free(this);
			expired_timers++;
		}
	}
}

int main(int argc, char** argv)
{
	printf("Running timer test for %u steps, accepting imprecision "
	       "of %u.%.6u seconds\n",
		MAIN_TIMER_NSTEPS, TIMER_PRES_SECS, TIMER_PRES_USECS);

	osmo_timer_schedule(&main_timer, 1, 0);

#ifdef HAVE_SYS_SELECT_H
	while (1) {
		osmo_select_main(0);
	}
#else
	printf("Select not supported on this platform!\n");
#endif
}
