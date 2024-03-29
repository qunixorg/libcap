/*
 * Copyright (c) 2019 Andrew G Morgan <morgan@kernel.org>
 *
 * This file contains a collection of routines that perform thread
 * synchronization to ensure that a whole process is running as a
 * single privilege object - independent of the number of pthreads.
 *
 * The whole file would be unnecessary if glibc exported an explicit
 * psx_syscall()-like function that leveraged the nptl:setxid
 * mechanism to synchronize thread state over the whole process.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/psx_syscall.h>
#include <unistd.h>

/*
 * share_psx_syscall() is invoked to advertize the two functions
 * psx_syscall3() and psx_syscall6(). The linkage is weak here so some
 * code external to this library can override it transparently during
 * the linkage process.
 */
__attribute__((weak))
void share_psx_syscall(long int (*syscall_fn)(long int,
					      long int, long int, long int),
		       long int (*syscall6_fn)(long int,
					       long int, long int, long int,
					       long int, long int, long int))
{
}

/*
 * type to keep track of registered threads.
 */
typedef struct registered_thread_s {
    struct registered_thread_s *next, *prev;
    pthread_t thread;
} registered_thread_t;

static pthread_once_t psx_tracker_initialized = PTHREAD_ONCE_INIT;

/*
 * This global structure holds the global coordination state for
 * libcap's psx_posix_syscall() support.
 */
static struct psx_tracker_s {
    pthread_mutex_t mu;

    int initialized;
    int psx_sig;

    struct {
	pthread_mutex_t mu;
	pthread_cond_t cond;
	long syscall_nr;
	long arg1, arg2, arg3, arg4, arg5, arg6;
	int six;
	int active;
	int todo;
    } cmd;

    struct sigaction sig_action;
    registered_thread_t *root;
} psx_tracker;

/*
 * psx_posix_syscall_handler performs the system call on the targeted
 * thread and decreases the outstanding syscall counter.
 */
static void psx_posix_syscall_handler(int signum) {
    if (!psx_tracker.cmd.active || signum != psx_tracker.psx_sig) {
	return;
    }

    if (!psx_tracker.cmd.six) {
	(void) syscall(psx_tracker.cmd.syscall_nr,
		       psx_tracker.cmd.arg1,
		       psx_tracker.cmd.arg2,
		       psx_tracker.cmd.arg3);
    } else {
	(void) syscall(psx_tracker.cmd.syscall_nr,
		       psx_tracker.cmd.arg1,
		       psx_tracker.cmd.arg2,
		       psx_tracker.cmd.arg3,
		       psx_tracker.cmd.arg4,
		       psx_tracker.cmd.arg5,
		       psx_tracker.cmd.arg6);
    }

    pthread_mutex_lock(&psx_tracker.cmd.mu);
    if (! --psx_tracker.cmd.todo) {
	pthread_cond_signal(&psx_tracker.cmd.cond);
    }
    pthread_mutex_unlock(&psx_tracker.cmd.mu);
}

long int psx_syscall3(long int syscall_nr,
		      long int arg1, long int arg2, long int arg3) {
    return psx_syscall(syscall_nr, arg1, arg2, arg3);
}

long int psx_syscall6(long int syscall_nr,
		      long int arg1, long int arg2, long int arg3,
		      long int arg4, long int arg5, long int arg6) {
    return psx_syscall(syscall_nr, arg1, arg2, arg3, arg4, arg5, arg6);
}

/*
 * psx_syscall_start initializes the subsystem.
 */
static void psx_syscall_start(void) {
    psx_tracker.initialized = 1;

    psx_tracker.psx_sig = 42; /* default signal number for syscall syncing */
    psx_tracker.sig_action.sa_handler = psx_posix_syscall_handler;
    sigemptyset(&psx_tracker.sig_action.sa_mask);
    psx_tracker.sig_action.sa_flags = 0;

    sigaction(psx_tracker.psx_sig, &psx_tracker.sig_action, NULL);

    share_psx_syscall(psx_syscall3, psx_syscall6);
}

static void psx_do_registration(pthread_t thread) {
    int first_time = !psx_tracker.initialized;
    (void) pthread_once(&psx_tracker_initialized, psx_syscall_start);

    if (first_time) {
	// First invocation, use recursion to register main() thread.
	psx_do_registration(pthread_self());
    }

    registered_thread_t *node = calloc(1, sizeof(registered_thread_t));
    node->next = psx_tracker.root;
    if (node->next) {
	node->next->prev = node;
    }
    node->thread = thread;
    psx_tracker.root = node;
}

/*
 * psx_register registers the a thread as pariticipating in the single
 * (POSIX) pool of privilege used by the library.
 *
 * In normal, expected, use you should never need to call this because
 * the linker magic wrapping will arrange for this function to be
 * called implicitly every time a pthread is created with
 * pthread_create() or psx_pthread_create(). If, however, you are
 * unable to use the linker trick to wrap pthread_create(), you should
 * include this line in one and only one place in your code. Just
 * after the end of the main() function would be a good place, for
 * example:
 *
 *   int main(int argc, char **argv) {

 *      ...
 *   }
 *
 *   PSX_NO_LINKER_WRAPPING
 *
 * This is required for libpsx to link. It also requires the coder to
 * explicitly use psx_register() for all threads not started with
 * psx_pthread_create().
 *
 * Note, there is no need to ever register the main() process thread.
 */
void psx_register(pthread_t thread) {
    pthread_mutex_lock(&psx_tracker.mu);
    psx_do_registration(thread);
    pthread_mutex_unlock(&psx_tracker.mu);
}

/* provide a prototype */
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
			  void *(*start_routine) (void *), void *arg);

/*
 * psx_pthread_create is a wrapper for pthread_create() that registers
 * the newly created thread. If your threads are created already, they
 * can be individually registered with psx_register().
 */
int psx_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
		       void *(*start_routine) (void *), void *arg) {
    if (pthread_create == __wrap_pthread_create) {
	return __wrap_pthread_create(thread, attr, start_routine, arg);
    }

    pthread_mutex_lock(&psx_tracker.mu);
    int ret = pthread_create(thread, attr, start_routine, arg);
    if (ret != -1) {
	psx_do_registration(*thread);
    }
    pthread_mutex_unlock(&psx_tracker.mu);
    return ret;
}

/*
 * __wrap_pthread_create is the wrapped destination of all regular
 * pthread_create calls.
 */
int __wrap_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
			  void *(*start_routine) (void *), void *arg) {
    pthread_mutex_lock(&psx_tracker.mu);
    int ret = __real_pthread_create(thread, attr, start_routine, arg);
    if (ret != -1) {
	psx_do_registration(*thread);
    }
    pthread_mutex_unlock(&psx_tracker.mu);
    return ret;
}

/*
 * __psx_syscall performs the syscall on the current thread and if no
 * error is detected it ensures that the syscall is also performed on
 * all (other) registered threads. The return code is the value for
 * the first invocation. It uses a trick to figure out how many
 * arguments the user has supplied. The other half of the trick is
 * provided by the macro psx_syscall() in the <sys/psx_syscall.h>
 * file. The trick is the 7th optional argument (8th over all) to
 * __psx_syscall is the count of arguments supplied to psx_syscall.
 *
 * User:
 *                       psx_syscall(nr, a, b);
 * Expanded by macro to:
 *                       __psx_syscall(nr, a, b, 6, 5, 4, 3, 2, 1, 0);
 * The eighth arg is now ------------------------------------^
 */
long int __psx_syscall(long int syscall_nr, ...) {
    long int arg[7];

    va_list aptr;
    va_start(aptr, syscall_nr);
    for (int i = 0; i < 7; i++) {
	arg[i] = va_arg(aptr, long int);
    }
    va_end(aptr);

    int count = arg[6];
    if (count < 0 || count > 6) {
	errno = EINVAL;
	return -1;
    }

    pthread_mutex_lock(&psx_tracker.mu);
    long int ret;

    psx_tracker.cmd.syscall_nr = syscall_nr;
    psx_tracker.cmd.arg1 = count > 0 ? arg[0] : 0;
    psx_tracker.cmd.arg2 = count > 1 ? arg[1] : 0;
    psx_tracker.cmd.arg3 = count > 2 ? arg[2] : 0;
    if (count > 3) {
	psx_tracker.cmd.six = 1;
	psx_tracker.cmd.arg1 = arg[3];
	psx_tracker.cmd.arg2 = count > 4 ? arg[4] : 0;
	psx_tracker.cmd.arg3 = count > 5 ? arg[5] : 0;

	ret = syscall(syscall_nr,
		      psx_tracker.cmd.arg1,
		      psx_tracker.cmd.arg2,
		      psx_tracker.cmd.arg3,
		      psx_tracker.cmd.arg4,
		      psx_tracker.cmd.arg5,
		      psx_tracker.cmd.arg6);
    } else {
	psx_tracker.cmd.six = 0;

	ret = syscall(syscall_nr,
		      psx_tracker.cmd.arg1,
		      psx_tracker.cmd.arg2,
		      psx_tracker.cmd.arg3);
    }

    if (ret == -1 || !psx_tracker.initialized) {
	goto defer;
    }

    int restore_errno = errno;
    psx_tracker.cmd.active = 1;

    pthread_t self = pthread_self();
    registered_thread_t *next = NULL;
    for (registered_thread_t *ref = psx_tracker.root; ref; ref = next) {
	next = ref->next;
	if (ref->thread == self) {
	    continue;
	}
	if (pthread_kill(ref->thread, psx_tracker.psx_sig) == 0) {
	    pthread_mutex_lock(&psx_tracker.cmd.mu);
	    psx_tracker.cmd.todo++;
	    pthread_mutex_unlock(&psx_tracker.cmd.mu);
	    continue;
	}

	/* need to remove now invalid thread id from linked list */
	if (ref == psx_tracker.root) {
	    psx_tracker.root = next;
	} else if (ref->prev) {
	    ref->prev->next = next;
	}
	if (next) {
	    next->prev = ref->prev;
	}
	free(ref);
    }

    pthread_mutex_lock(&psx_tracker.cmd.mu);
    while (psx_tracker.cmd.todo) {
	pthread_cond_wait(&psx_tracker.cmd.cond, &psx_tracker.cmd.mu);
    }
    pthread_mutex_unlock(&psx_tracker.cmd.mu);

    errno = restore_errno;
defer:

    psx_tracker.cmd.active = 0;
    pthread_mutex_unlock(&psx_tracker.mu);

    return ret;
}
