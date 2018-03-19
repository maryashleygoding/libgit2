/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */
#ifndef INCLUDE_reader_h__
#define INCLUDE_reader_h__

#include "common.h"

typedef struct git_reader git_reader;

struct git_reader {
	int (*read)(git_buf *out, git_reader *reader, const char *filename);
	void (*free)(git_reader *reader);
};

extern int git_reader_for_nothing(
	git_reader **out);

extern int git_reader_for_tree(
	git_reader **out,
	git_tree *tree);

extern int git_reader_for_index(
	git_reader **out,
	git_repository *repo,
	git_index *index);

extern int git_reader_for_workdir(
	git_reader **out,
	git_repository *repo);

extern int git_reader_read(
	git_buf *out,
	git_reader *reader,
	const char *filename);

extern void git_reader_free(git_reader *reader);

#endif
