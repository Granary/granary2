/* Copyright 2012-2014 Peter Goodman, all rights reserved. */
/*
 * syscall.cc
 *
 *  Created on: Sep 8, 2014
 *      Author: Peter Goodman
 */

#include "clients/user/syscall.h"

// Register a function to be called before a system call is made.
void OnSystemCallEntry(SysCallEntryHook *hook) {

}

// Register a function to be called after a system call is made.
void OnSystemCallExit(SysCallExitHook *hook) {

}

