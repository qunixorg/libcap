#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/psx_syscall.h>
#include <sys/syscall.h>

static void say_hello_expecting(const char *title, int n, int kept) {
    int keeper = prctl(PR_GET_KEEPCAPS);
    printf("hello, %s<%d> %lx (keepcaps=%d vs. want=%d)\n",
	   title, n, pthread_self(), keeper, kept);
    if (keeper != kept) {
	printf("--> FAILURE %s thread=%lx has wrong keepcaps: got=%d want=%d\n",
	       title, pthread_self(), keeper, kept);
	exit(1);
    }
}

pthread_mutex_t mu;
pthread_cond_t cond;

int global_kept = 0;
int step = 0;
int replies = 0;
int launched = 0;
int started = 0;

static void *say_hello(void *args) {
    int count = 0;

    pthread_mutex_lock(&mu);
    started++;
    pthread_cond_broadcast(&cond);

    int this_step = step+1;
    do {
	while (this_step > step) {
	    pthread_cond_wait(&cond, &mu);
	}
	this_step++;

	say_hello_expecting("thread", count, global_kept);

	replies++;
	pthread_cond_broadcast(&cond);
    } while (++count != 3);

    pthread_mutex_unlock(&mu);

    return NULL;
}

int main(int argc, char **argv) {
    pthread_t tid[3];

    for (int i = 0; i<10; i++) {
	printf("iteration: %d\n", i);

	pthread_mutex_lock(&mu);
	global_kept = !global_kept;
	replies = 0;
	step = i;
	pthread_mutex_unlock(&mu);

	psx_syscall(SYS_prctl, PR_SET_KEEPCAPS, global_kept);
	step++;
	pthread_cond_broadcast(&cond);

	say_hello_expecting("main", i, global_kept);

	pthread_mutex_lock(&mu);
	while (replies < launched) {
	    pthread_cond_wait(&cond, &mu);
	}
	pthread_mutex_unlock(&mu);

	if (i < 3) {
	    launched++;
	    if (i == 1) {
		// Confirm this works whether or not we are WRAPPING.
		psx_pthread_create(&tid[i], NULL, say_hello, NULL);
	    } else if (i < 3) {
#ifdef NOWRAP
		psx_pthread_create(&tid[i], NULL, say_hello, NULL);
#else
		pthread_create(&tid[i], NULL, say_hello, NULL);
#endif
	    }
	    // Confirm that the thread is started.
	    pthread_mutex_lock(&mu);
	    while (started < launched) {
		pthread_cond_wait(&cond, &mu);
	    }
	    pthread_mutex_unlock(&mu);
	} else if (i < 6) {
	    // Confirm one thread has finished.
	    pthread_join(tid[i-3], NULL);
	    launched--;
	}
    }

    printf("%s PASSED\n", argv[0]);
    exit(0);
}

#ifdef NOWRAP
PSX_NO_LINKER_WRAPPING
#endif
