// Copyright (c) 2013-2014 Cloudozer LLP. All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// 
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
// 
// * Redistributions in any form must be accompanied by information on how to
// obtain complete source code for the LING software and any accompanying
// software that uses the LING software. The source code must either be included
// in the distribution or be available for no more than the cost of distribution
// plus a nominal fee, and must be freely redistributable under reasonable
// conditions.  For an executable file, complete source code means the source
// code for all modules it contains. It does not include source code for modules
// or files that typically accompany the major components of the operating
// system on which the executable file runs.
// 
// THIS SOFTWARE IS PROVIDED BY CLOUDOZER LLP ``AS IS'' AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, OR NON-INFRINGEMENT, ARE
// DISCLAIMED. IN NO EVENT SHALL CLOUDOZER LLP BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "heap.h"

#include "ling_common.h"

#include "string.h"
#include "assert.h"

// Tunable parameters
int gc_model_size_multiplier = 2;
int gc_model_yield_up = 1;
int gc_model_yield_down = 5;
int gc_model_wait_up = 1;
int gc_model_wait_down = 5;

static void collect(heap_t *hp, region_t *root_regs, int nr_regs);
static void collect_cohort(heap_t *hp, int ch, region_t *root_regs, int nr_regs);

void gc_opt_init(void)
{
}

void gc_hook(int gc_loc, term_t pid, heap_t *hp, region_t *root_regs, int nr_regs)
{
	if (gc_loc == GC_LOC_TEST_HEAP || gc_loc == GC_LOC_IDLE)
		collect(hp, root_regs, nr_regs);
	else if (gc_loc == GC_LOC_PROC_YIELD)
	{
		hp->gc_yield_tally += gc_model_yield_up;
		if (hp->gc_yield_tally >= gc_model_yield_down)
		{
			collect(hp, root_regs, nr_regs);
			hp->gc_yield_tally -= gc_model_yield_down;
		}
	}
	else if (gc_loc == GC_LOC_PROC_WAIT)
	{
		hp->gc_wait_tally += gc_model_wait_up;
		if (hp->gc_wait_tally >= gc_model_wait_down)
		{
			collect(hp, root_regs, nr_regs);
			hp->gc_wait_tally -= gc_model_wait_down;
		}
	}
}

static void collect(heap_t *hp, region_t *root_regs, int nr_regs)
{
	memnode_t *node = hp->nodes;
	int ch = 0;
	int size = 0;
	int max_ch = 0;
	int max_size = 0;
	int weight = 1;
	while (ch < GC_COHORTS)
	{
		while (ch < GC_COHORTS && node == hp->gc_cohorts[ch])
		{
			if (size >= max_size * weight)
			{
				max_size = size;
				max_ch = ch;
			}
			ch++;
			weight *= gc_model_size_multiplier;	//NB
			size = 0;
		}
		if (node == 0)
			break;
		size++;
		node = node->next;
	}

	if (max_size > 0)	
		collect_cohort(hp, max_ch, root_regs, nr_regs);
}

static void collect_cohort(heap_t *hp, int ch, region_t *root_regs, int nr_regs)
{
	memnode_t *top = hp->gc_cohorts[ch];
	memnode_t *prev = (ch == 0) ?hp->nodes :hp->gc_cohorts[ch-1];
	if (prev == top)
		return;	// the previous cohort is empty
	while (prev->next != top)
		prev = prev->next;

	memnode_t **ref = &hp->nodes;
	while (*ref != prev)
		ref = &(*ref)->next;

	hp->gc_cohorts[ch] = prev; // may become invalid; fix after GC

	int ok = heap_gc_generational_N(hp, prev, root_regs, nr_regs);
	assert(ok == 0);

	for (int i = 0; i <= ch; i++)
		if (hp->gc_cohorts[i] == prev)
			hp->gc_cohorts[i] = *ref;
}

