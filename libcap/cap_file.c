/*
 * Copyright (c) 1997 Andrew G Morgan <morgan@kernel.org>
 *
 * This file deals with setting capabilities on files.
 */

#include "libcap.h"

/*
 * Get the capabilities of an open file, as specified by its file
 * descriptor.
 */

cap_t cap_get_fd(int fildes)
{
    cap_t result;

    /* allocate a new capability set */
    result = cap_init();
    if (result) {
	_cap_debug("getting fildes capabilities");

	/* fill the capability sets via a system call */
	if (_fgetfilecap(fildes, sizeof(struct __cap_s),
			      &result->set[CAP_INHERITABLE],
			      &result->set[CAP_PERMITTED],
			      &result->set[CAP_EFFECTIVE] )) {
	    cap_free(result);
	    result = NULL;
	}
    }

    return result;
}

/*
 * Set the capabilities on a named file.
 */

cap_t cap_get_file(const char *filename)
{
    cap_t result;

    /* allocate a new capability set */
    result = cap_init();
    if (result) {
	_cap_debug("getting named file capabilities");

	/* fill the capability sets via a system call */
	if (_getfilecap(filename, sizeof(struct __cap_s),
			     &result->set[CAP_INHERITABLE],
			     &result->set[CAP_PERMITTED],
			     &result->set[CAP_EFFECTIVE] ))
	    cap_free(result);
	    result = NULL;
    }

    return result;
}

/*
 * Set the capabilities of an open file, as specified by its file
 * descriptor.
 */

int cap_set_fd(int fildes, cap_t cap_d)
{
    if (!good_cap_t(cap_d)) {
	errno = EINVAL;
	return -1;
    }

    _cap_debug("setting fildes capabilities");
    return _fsetfilecap(fildes, sizeof(struct __cap_s),
			  &cap_d->set[CAP_INHERITABLE],
			  &cap_d->set[CAP_PERMITTED],
			  &cap_d->set[CAP_EFFECTIVE] );
}

/*
 * Set the capabilities of a named file.
 */

int cap_set_file(const char *filename, cap_t cap_d)
{
    if (!good_cap_t(cap_d)) {
	errno = EINVAL;
	return -1;
    }

    _cap_debug("setting filename capabilities");
    return _setfilecap(filename, sizeof(struct __cap_s),
			  &cap_d->set[CAP_INHERITABLE],
			  &cap_d->set[CAP_PERMITTED],
			  &cap_d->set[CAP_EFFECTIVE] );
}
