/*
 * Copyright (c) 1997,2007-8 Andrew G. Morgan  <morgan@kernel.org>
 *
 * This sets/verifies the capabilities of a given file.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/capability.h>
#include <unistd.h>

static void usage(void)
{
    fprintf(stderr,
	    "usage: setcap [-q] [-v] (-r|-|<caps>) <filename> "
	    "[ ... (-r|-|<capsN>) <filenameN> ]\n"
	    "\n"
	    " Note <filename> must be a regular (non-symlink) file.\n"
	);
    exit(1);
}

#define MAXCAP  2048

static int read_caps(int quiet, const char *filename, char *buffer)
{
    int i=MAXCAP;

    if (!quiet) {
	fprintf(stderr,	"Please enter caps for file [empty line to end]:\n");
    }
    while (i > 0) {
	int j = read(STDIN_FILENO, buffer, i);

	if (j < 0) {
	    fprintf(stderr, "\n[Error - aborting]\n");
	    exit(1);
	}

	if (j==0 || buffer[0] == '\n') {
	    /* we're done */
	    break;
	}

	/* move on... */

	i -= j;
	buffer += j;
    }

    /* <NUL> terminate */
    buffer[0] = '\0';

    return (i < MAXCAP ? 0:-1);
}

int main(int argc, char **argv)
{
    int tried_to_cap_setfcap = 0;
    char buffer[MAXCAP+1];
    int retval, quiet=0, verify=0;
    cap_t mycaps;
    cap_value_t capflag;

    if (argc < 3) {
	usage();
    }

    mycaps = cap_get_proc();
    if (mycaps == NULL) {
	fprintf(stderr, "warning - unable to get process capabilities"
		" (old libcap?)\n");
    }

    while (--argc > 0) {
	const char *text;
	cap_t cap_d;

	if (!strcmp(*++argv, "-q")) {
	    quiet = 1;
	    continue;
	}
	if (!strcmp(*argv, "-v")) {
	    verify = 1;
	    continue;
	}

	if (!strcmp(*argv, "-r")) {
	    cap_d = NULL;
	} else {
	    if (!strcmp(*argv,"-")) {
		retval = read_caps(quiet, *argv, buffer);
		if (retval)
		    usage();
		text = buffer;
	    } else {
		text = *argv;
	    }

	    cap_d = cap_from_text(text);
	    if (cap_d == NULL) {
		perror("fatal error");
		usage();
	    }
#ifdef DEBUG
	    {
		ssize_t length;
		const char *result;

		result = cap_to_text(cap_d, &length);
		fprintf(stderr, "caps set to: [%s]\n", result);
	    }
#endif
	}

	if (--argc <= 0)
	    usage();
	/*
	 * Set the filesystem capability for this file.
	 */
	if (verify) {
	    cap_t cap_on_file;
	    int cmp;

	    if (cap_d == NULL) {
		cap_d = cap_from_text("=");
	    }

	    cap_on_file = cap_get_file(*++argv);

	    if (cap_on_file == NULL) {
		cap_on_file = cap_from_text("=");
	    }

	    cmp = cap_compare(cap_on_file, cap_d);
	    cap_free(cap_on_file);

	    if (cmp != 0) {
		if (!quiet) {
		    printf("%s differs in [%s%s%s]\n", *argv,
			   CAP_DIFFERS(cmp, CAP_PERMITTED) ? "p" : "",
			   CAP_DIFFERS(cmp, CAP_INHERITABLE) ? "i" : "",
			   CAP_DIFFERS(cmp, CAP_EFFECTIVE) ? "e" : "");
		}
		exit(1);
	    }
	    if (!quiet) {
		printf("%s: OK\n", *argv);
	    }
	} else {
	    if (!tried_to_cap_setfcap) {
		capflag = CAP_SETFCAP;

		/*
		 * Raise the effective CAP_SETFCAP.
		 */
		if (cap_set_flag(mycaps, CAP_EFFECTIVE, 1, &capflag, CAP_SET)
		    != 0) {
		    perror("unable to manipulate CAP_SETFCAP - "
			   "try a newer libcap?");
		    exit(1);
		}
		if (cap_set_proc(mycaps) != 0) {
		    perror("unable to set CAP_SETFCAP effective capability");
		    exit(1);
		}
	    }
	    retval = cap_set_file(*++argv, cap_d);
	    if (retval != 0) {
		fprintf(stderr,
			"Failed to set capabilities on file `%s' (%s)\n",
			argv[0], strerror(errno));
		usage();
	    }
	}
	if (cap_d) {
	    cap_free(cap_d);
	}
    }

    return 0;
}
