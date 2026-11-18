// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 R5OS Contributors

#include <OS.h>
#include <stdio.h>
#include <stdlib.h>

struct PingState {
	sem_id semA;
	sem_id semB;
	int rounds;
	int doneA;
	int doneB;
};

static int32
pong_thread(void* arg)
{
	PingState* state = static_cast<PingState*>(arg);
	for (int i = 0; i < state->rounds; i++) {
		status_t status = acquire_sem_etc(state->semB, 1, B_RELATIVE_TIMEOUT, 5000000);
		if (status != B_OK) {
			fprintf(stderr, "pong timeout at round %d: %ld\n", i, (long)status);
			return 1;
		}
		state->doneB++;
		release_sem(state->semA);
	}
	return 0;
}

int
main()
{
	PingState state = {};
	state.semA = create_sem(0, "r5_ping_a");
	state.semB = create_sem(0, "r5_ping_b");
	state.rounds = 20000;
	if (state.semA < 0 || state.semB < 0) {
		fprintf(stderr, "create_sem failed\n");
		return 1;
	}

	thread_id tid = spawn_thread(pong_thread, "r5_pong", B_NORMAL_PRIORITY, &state);
	if (tid < 0 || resume_thread(tid) != B_OK) {
		fprintf(stderr, "spawn_thread failed\n");
		return 1;
	}

	bigtime_t start = system_time();
	for (int i = 0; i < state.rounds; i++) {
		release_sem(state.semB);
		status_t status = acquire_sem_etc(state.semA, 1, B_RELATIVE_TIMEOUT, 5000000);
		if (status != B_OK) {
			fprintf(stderr, "ping timeout at round %d: %ld\n", i, (long)status);
			return 1;
		}
		state.doneA++;
	}

	status_t exitVal = 1;
	wait_for_thread(tid, &exitVal);
	delete_sem(state.semA);
	delete_sem(state.semB);

	if (exitVal != 0 || state.doneA != state.rounds || state.doneB != state.rounds) {
		fprintf(stderr, "mismatch exit=%ld A=%d B=%d\n",
			(long)exitVal, state.doneA, state.doneB);
		return 1;
	}

	bigtime_t elapsed = system_time() - start;
	printf("r5_idle_pingpong PASS rounds=%d us=%lld\n",
		state.rounds, (long long)elapsed);
	return 0;
}
