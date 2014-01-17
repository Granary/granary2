/*
 * dcontext.c
 *
 *  Created on: 2012-11-09
 *      Author: pag
 *     Version: $Id$
 */

#include "dependencies/dynamorio/globals.h"
#include "dependencies/dynamorio/types.h"

dcontext_t DCONTEXT = {
    .x86_mode = false
};

dcontext_t *get_thread_private_dcontext(void) {
    return &DCONTEXT;
}
