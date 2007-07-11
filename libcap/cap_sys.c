/*
 * Copyright (c) 1997-8 Andrew G. Morgan   <morgan@kernel.org>
 *
 * This file contains the system calls for getting and setting
 * capabilities
 */

#include <linux/unistd.h>
#include <linux/capability.h>

_syscall2(int, capget,
	  cap_user_header_t, header,
	  cap_user_data_t, data)

_syscall2(int, capset,
	  cap_user_header_t, header,
	  const cap_user_data_t, data)
