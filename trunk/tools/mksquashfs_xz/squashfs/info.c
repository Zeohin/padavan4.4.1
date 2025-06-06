/*
 * Create a squashfs filesystem.  This is a highly compressed read only
 * filesystem.
 *
 * Copyright (c) 2013, 2014, 2019, 2021, 2022, 2023, 2024, 2025
 * Phillip Lougher <phillip@squashfs.org.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * info.c
 */

#include <pthread.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <stdio.h>
#include <math.h>
#include <stdarg.h>
#include <errno.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "squashfs_fs.h"
#include "mksquashfs.h"
#include "mksquashfs_error.h"
#include "progressbar.h"
#include "caches-queues-lists.h"
#include "signals.h"
#include "reader.h"
#include "thread.h"

static struct dir_ent *ent = NULL;

static pthread_t info_thread;


void disable_info()
{
	ent = NULL;
}


void update_info(struct dir_ent *dir_ent)
{
	ent = dir_ent;
}


static void print_filename()
{
	struct dir_ent *dir_ent = ent;

	if(dir_ent == NULL)
		return;

	if(dir_ent->our_dir->subpath[0] != '\0')
		progressbar_info("%s/%s\n", dir_ent->our_dir->subpath, dir_ent->name);
	else
		progressbar_info("/%s\n", dir_ent->name);
}


static void dump_state()
{
	int i, reader_threads;
	struct reader *reader;

	disable_progress_bar();

	printf("Queues, caches and threads status dump\n");
	printf("======================================\n");

	printf("file buffer queue (reader thread -> deflate thread(s))\n");
	dump_block_read_queue(to_deflate);

	printf("uncompressed fragment queue (reader thread -> fragment"
						" thread(s))\n");
	dump_read_queue(to_process_frag);

	printf("processed fragment queue (fragment thread(s) -> main"
						" thread)\n");
	dump_seq_queue(to_main, 1);

	printf("compressed block queue (deflate thread(s) -> main thread)\n");
	dump_seq_queue(to_main, 0);

	printf("uncompressed packed fragment queue (main thread -> fragment"
						" deflate thread(s))\n");
	dump_queue(to_frag);

	printf("compressed fragment queue (fragment deflate threads(s) ->"
					"fragment order thread)\n");

	dump_seq_queue(to_order, 0);

	printf("compressed block queue (main & fragment order threads ->"
					" writer thread)\n");
	dump_queue(to_writer);

	reader = get_readers(&reader_threads);
	for(i = 0; i < reader_threads; i++) {
		printf("%s read cache %d (uncompressed blocks read by reader thread %d)\n", reader[i].type, i + 1, i + 1);
		dump_cache(reader[i].buffer);
	}

	dump_write_cache(bwriter_buffer);

	printf("fragment write cache (compressed fragments waiting for the"
						" writer thread)\n");
	dump_cache(fwriter_buffer);

	printf("fragment cache (frags waiting to be compressed by fragment"
						" deflate thread(s))\n");
	dump_cache(fragment_buffer);

	printf("fragment reserve cache (avoids pipeline stall if frag cache"
						" full in dup check)\n");
	dump_cache(reserve_cache);

	dump_threads();

	enable_progress_bar();
}


static void *info_thrd(void *arg)
{
	sigset_t sigmask;
	int sig, waiting = 0;

	sigemptyset(&sigmask);
	sigaddset(&sigmask, SIGQUIT);
	sigaddset(&sigmask, SIGHUP);

	while(1) {
		sig = wait_for_signal(&sigmask, &waiting);

		if(sig == SIGQUIT && !waiting) {
			print_filename();

			/* set one second interval period, if ^\ received
			   within then, dump queue and cache status */
			waiting = 1;
		} else
			dump_state();
	}

	return NULL;
}


void init_info()
{
	pthread_create(&info_thread, NULL, info_thrd, NULL);
}
