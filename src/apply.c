/*
 * Copyright (C) the libgit2 contributors. All rights reserved.
 *
 * This file is part of libgit2, distributed under the GNU GPL v2 with
 * a Linking Exception. For full terms see the included COPYING file.
 */

#include "apply.h"

#include <assert.h>

#include "git2/apply.h"
#include "git2/patch.h"
#include "git2/filter.h"
#include "git2/blob.h"
#include "git2/index.h"
#include "git2/checkout.h"
#include "git2/repository.h"
#include "array.h"
#include "patch.h"
#include "fileops.h"
#include "delta.h"
#include "zstream.h"
#include "reader.h"

#define apply_err(...) \
	( giterr_set(GITERR_PATCH, __VA_ARGS__), GIT_EAPPLYFAIL )

typedef struct {
	/* The lines that we allocate ourself are allocated out of the pool.
	 * (Lines may have been allocated out of the diff.)
	 */
	git_pool pool;
	git_vector lines;
} patch_image;

static void patch_line_init(
	git_diff_line *out,
	const char *in,
	size_t in_len,
	size_t in_offset)
{
	out->content = in;
	out->content_len = in_len;
	out->content_offset = in_offset;
}

#define PATCH_IMAGE_INIT { GIT_POOL_INIT, GIT_VECTOR_INIT }

static int patch_image_init_fromstr(
	patch_image *out, const char *in, size_t in_len)
{
	git_diff_line *line;
	const char *start, *end;

	memset(out, 0x0, sizeof(patch_image));

	git_pool_init(&out->pool, sizeof(git_diff_line));

	for (start = in; start < in + in_len; start = end) {
		end = memchr(start, '\n', in_len);

		if (end == NULL)
			end = in + in_len;

		else if (end < in + in_len)
			end++;

		line = git_pool_mallocz(&out->pool, 1);
		GITERR_CHECK_ALLOC(line);

		if (git_vector_insert(&out->lines, line) < 0)
			return -1;

		patch_line_init(line, start, (end - start), (start - in));
	}

	return 0;
}

static void patch_image_free(patch_image *image)
{
	if (image == NULL)
		return;

	git_pool_clear(&image->pool);
	git_vector_free(&image->lines);
}

static bool match_hunk(
	patch_image *image,
	patch_image *preimage,
	size_t linenum)
{
	bool match = 0;
	size_t i;

	/* Ensure this hunk is within the image boundaries. */
	if (git_vector_length(&preimage->lines) + linenum >
		git_vector_length(&image->lines))
		return 0;

	match = 1;

	/* Check exact match. */
	for (i = 0; i < git_vector_length(&preimage->lines); i++) {
		git_diff_line *preimage_line = git_vector_get(&preimage->lines, i);
		git_diff_line *image_line = git_vector_get(&image->lines, linenum + i);

		if (preimage_line->content_len != image_line->content_len ||
			memcmp(preimage_line->content, image_line->content, image_line->content_len) != 0) {
			match = 0;
			break;
		}
	}

	return match;
}

static bool find_hunk_linenum(
	size_t *out,
	patch_image *image,
	patch_image *preimage,
	size_t linenum)
{
	size_t max = git_vector_length(&image->lines);
	bool match;

	if (linenum > max)
		linenum = max;

	match = match_hunk(image, preimage, linenum);

	*out = linenum;
	return match;
}

static int update_hunk(
	patch_image *image,
	unsigned int linenum,
	patch_image *preimage,
	patch_image *postimage)
{
	size_t postlen = git_vector_length(&postimage->lines);
	size_t prelen = git_vector_length(&preimage->lines);
	size_t i;
	int error = 0;

	if (postlen > prelen)
		error = git_vector_insert_null(
			&image->lines, linenum, (postlen - prelen));
	else if (prelen > postlen)
		error = git_vector_remove_range(
			&image->lines, linenum, (prelen - postlen));

	if (error) {
		giterr_set_oom();
		return -1;
	}

	for (i = 0; i < git_vector_length(&postimage->lines); i++) {
		image->lines.contents[linenum + i] =
			git_vector_get(&postimage->lines, i);
	}

	return 0;
}

static int apply_hunk(
	patch_image *image,
	git_patch *patch,
	git_patch_hunk *hunk)
{
	patch_image preimage = PATCH_IMAGE_INIT, postimage = PATCH_IMAGE_INIT;
	size_t line_num, i;
	int error = 0;

	for (i = 0; i < hunk->line_count; i++) {
		size_t linenum = hunk->line_start + i;
		git_diff_line *line = git_array_get(patch->lines, linenum);

		if (!line) {
			error = apply_err("preimage does not contain line %"PRIuZ, linenum);
			goto done;
		}

		if (line->origin == GIT_DIFF_LINE_CONTEXT ||
			line->origin == GIT_DIFF_LINE_DELETION) {
			if ((error = git_vector_insert(&preimage.lines, line)) < 0)
				goto done;
		}

		if (line->origin == GIT_DIFF_LINE_CONTEXT ||
			line->origin == GIT_DIFF_LINE_ADDITION) {
			if ((error = git_vector_insert(&postimage.lines, line)) < 0)
				goto done;
		}
	}

	line_num = hunk->hunk.new_start ? hunk->hunk.new_start - 1 : 0;

	if (!find_hunk_linenum(&line_num, image, &preimage, line_num)) {
		error = apply_err("hunk at line %d did not apply",
			hunk->hunk.new_start);
		goto done;
	}

	error = update_hunk(image, line_num, &preimage, &postimage);

done:
	patch_image_free(&preimage);
	patch_image_free(&postimage);

	return error;
}

static int apply_hunks(
	git_buf *out,
	const char *source,
	size_t source_len,
	git_patch *patch)
{
	git_patch_hunk *hunk;
	git_diff_line *line;
	patch_image image;
	size_t i;
	int error = 0;

	if ((error = patch_image_init_fromstr(&image, source, source_len)) < 0)
		goto done;

	git_array_foreach(patch->hunks, i, hunk) {
		if ((error = apply_hunk(&image, patch, hunk)) < 0)
			goto done;
	}

	git_vector_foreach(&image.lines, i, line)
		git_buf_put(out, line->content, line->content_len);

done:
	patch_image_free(&image);

	return error;
}

static int apply_binary_delta(
	git_buf *out,
	const char *source,
	size_t source_len,
	git_diff_binary_file *binary_file)
{
	git_buf inflated = GIT_BUF_INIT;
	int error = 0;

	/* no diff means identical contents */
	if (binary_file->datalen == 0)
		return git_buf_put(out, source, source_len);

	error = git_zstream_inflatebuf(&inflated,
		binary_file->data, binary_file->datalen);

	if (!error && inflated.size != binary_file->inflatedlen) {
		error = apply_err("inflated delta does not match expected length");
		git_buf_dispose(out);
	}

	if (error < 0)
		goto done;

	if (binary_file->type == GIT_DIFF_BINARY_DELTA) {
		void *data;
		size_t data_len;

		error = git_delta_apply(&data, &data_len, (void *)source, source_len,
			(void *)inflated.ptr, inflated.size);

		out->ptr = data;
		out->size = data_len;
		out->asize = data_len;
	}
	else if (binary_file->type == GIT_DIFF_BINARY_LITERAL) {
		git_buf_swap(out, &inflated);
	}
	else {
		error = apply_err("unknown binary delta type");
		goto done;
	}

done:
	git_buf_dispose(&inflated);
	return error;
}

static int apply_binary(
	git_buf *out,
	const char *source,
	size_t source_len,
	git_patch *patch)
{
	git_buf reverse = GIT_BUF_INIT;
	int error = 0;

	if (!patch->binary.contains_data) {
		error = apply_err("patch does not contain binary data");
		goto done;
	}

	if (!patch->binary.old_file.datalen && !patch->binary.new_file.datalen)
		goto done;

	/* first, apply the new_file delta to the given source */
	if ((error = apply_binary_delta(out, source, source_len,
			&patch->binary.new_file)) < 0)
		goto done;

	/* second, apply the old_file delta to sanity check the result */
	if ((error = apply_binary_delta(&reverse, out->ptr, out->size,
			&patch->binary.old_file)) < 0)
		goto done;

	if (source_len != reverse.size ||
		memcmp(source, reverse.ptr, source_len) != 0) {
		error = apply_err("binary patch did not apply cleanly");
		goto done;
	}

done:
	if (error < 0)
		git_buf_dispose(out);

	git_buf_dispose(&reverse);
	return error;
}

int git_apply__patch(
	git_buf *contents_out,
	char **filename_out,
	unsigned int *mode_out,
	const char *source,
	size_t source_len,
	git_patch *patch)
{
	char *filename = NULL;
	unsigned int mode = 0;
	int error = 0;

	assert(contents_out && filename_out && mode_out && (source || !source_len) && patch);

	*filename_out = NULL;
	*mode_out = 0;

	if (patch->delta->status != GIT_DELTA_DELETED) {
		const git_diff_file *newfile = &patch->delta->new_file;

		filename = git__strdup(newfile->path);
		mode = newfile->mode ?
			newfile->mode : GIT_FILEMODE_BLOB;
	}

	if (patch->delta->flags & GIT_DIFF_FLAG_BINARY)
		error = apply_binary(contents_out, source, source_len, patch);
	else if (patch->hunks.size)
		error = apply_hunks(contents_out, source, source_len, patch);
	else
		error = git_buf_put(contents_out, source, source_len);

	if (error)
		goto done;

	if (patch->delta->status == GIT_DELTA_DELETED &&
		git_buf_len(contents_out) > 0) {
		error = apply_err("removal patch leaves file contents");
		goto done;
	}

	*filename_out = filename;
	*mode_out = mode;

done:
	if (error < 0)
		git__free(filename);

	return error;
}

static int apply_one(
	git_repository *repo,
	git_reader *preimage_reader,
	git_index *postimage,
	git_diff *diff,
	size_t i)
{
	git_patch *patch = NULL;
	git_buf pre_contents = GIT_BUF_INIT, post_contents = GIT_BUF_INIT;
	const git_diff_delta *delta;
	char *filename = NULL;
	unsigned int mode;
	git_oid blob_id;
	git_index_entry index_entry;
	int error;

	if ((error = git_patch_from_diff(&patch, diff, i)) < 0)
		goto done;

	delta = git_diff_get_delta(diff, i);

	if (delta->status == GIT_DELTA_DELETED)
		goto done;

	if (delta->status != GIT_DELTA_ADDED) {
		if ((error = git_reader_read(&pre_contents,
		    preimage_reader, delta->old_file.path)) < 0) {

			/* ENOTFOUND is really an application error */
			if (error == GIT_ENOTFOUND)
				error = GIT_EAPPLYFAIL;

			goto done;
		}
	}

	if ((error = git_apply__patch(&post_contents, &filename, &mode,
			pre_contents.ptr, pre_contents.size, patch)) < 0 ||
		(error = git_blob_create_frombuffer(&blob_id, repo,
			post_contents.ptr, post_contents.size)) < 0)
		goto done;

	memset(&index_entry, 0, sizeof(git_index_entry));
	index_entry.path = filename;
	index_entry.mode = mode;
	git_oid_cpy(&index_entry.id, &blob_id);

	if ((error = git_index_add(postimage, &index_entry)) < 0)
		goto done;

done:
	git_buf_free(&pre_contents);
	git_buf_free(&post_contents);
	git__free(filename);
	git_patch_free(patch);

	return error;
}

int git_apply_tree(
	git_index **out,
	git_repository *repo,
	git_tree *preimage,
	git_diff *diff)
{
	git_index *postimage = NULL;
	git_reader *pre_reader = NULL;
	const git_diff_delta *delta;
	size_t i;
	int error = 0;

	assert(out && repo && preimage && diff);

	*out = NULL;

	if ((error = git_reader_for_tree(&pre_reader, preimage)) < 0)
		goto done;

	/* put the current tree into the postimage as-is - the diff will
	 * replace any entries contained therein
	 */
	if ((error = git_index_new(&postimage)) < 0 ||
		(error = git_index_read_tree(postimage, preimage)) < 0)
		goto done;

	/*
	 * Remove the old paths from the index before applying diffs -
	 * we need to do a full pass to remove them before adding deltas,
	 * in order to handle rename situations.
	 */
	for (i = 0; i < git_diff_num_deltas(diff); i++) {
		delta = git_diff_get_delta(diff, i);

		if ((error = git_index_remove(postimage,
				delta->old_file.path, 0)) < 0)
			goto done;
	}

	for (i = 0; i < git_diff_num_deltas(diff); i++) {
		if ((error = apply_one(repo, pre_reader, postimage, diff, i)) < 0)
			goto done;
	}

	*out = postimage;

done:
	if (error < 0)
		git_index_free(postimage);

	git_reader_free(pre_reader);

	return error;
}

static int git_apply__to_workdir(
    git_repository *repo,
    git_diff *diff,
    git_index *postimage,
    git_apply_options *opts)
{
	git_vector paths = GIT_VECTOR_INIT;
	git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
	const git_diff_delta *delta;
	size_t i;
	int error;

	/*
	 * Limit checkout to the paths affected by the diff; this ensures
	 * that other modifications in the working directory are unaffected.
	 */
	if ((error = git_vector_init(&paths, git_diff_num_deltas(diff), NULL)) < 0)
		goto done;

	for (i = 0; i < git_diff_num_deltas(diff); i++) {
		delta = git_diff_get_delta(diff, i);

		if ((error = git_vector_insert(&paths, (void *)delta->old_file.path)) < 0)
			goto done;

		if (strcmp(delta->old_file.path, delta->new_file.path) &&
		    (error = git_vector_insert(&paths, (void *)delta->new_file.path)) < 0)
			goto done;
	}

	checkout_opts.checkout_strategy |= GIT_CHECKOUT_SAFE;
	checkout_opts.checkout_strategy |= GIT_CHECKOUT_DISABLE_PATHSPEC_MATCH;

	if (opts->location == GIT_APPLY_LOCATION_WORKDIR)
		checkout_opts.checkout_strategy |= GIT_CHECKOUT_DONT_UPDATE_INDEX;

	checkout_opts.paths.strings = (char **)paths.contents;
	checkout_opts.paths.count = paths.length;

	error = git_checkout_index(repo, postimage, &checkout_opts);

done:
	git_vector_free(&paths);
	return error;
}

static int git_apply__to_index(
    git_repository *repo,
    git_diff *diff,
    git_index *postimage,
    git_apply_options *opts)
{
	git_index *index = NULL;
	const git_diff_delta *delta;
	const git_index_entry *entry;
	size_t i;
	int error;

	GIT_UNUSED(opts);

	if ((error = git_repository_index(&index, repo)) < 0)
		goto done;

	/* Collect the paths to remove from the index. */
	for (i = 0; i < git_diff_num_deltas(diff); i++) {
		delta = git_diff_get_delta(diff, i);

		if (delta->status == GIT_DELTA_DELETED ||
		    delta->status == GIT_DELTA_RENAMED) {
			if ((error = git_index_remove(index, delta->old_file.path, 0)) < 0)
				goto done;
		}
	}

	/* Then add the changes back to the index. */
	for (i = 0; i < git_index_entrycount(postimage); i++) {
		entry = git_index_get_byindex(postimage, i);

		if ((error = git_index_add(index, entry)) < 0)
			goto done;
	}

	error = git_index_write(index);

done:
	git_index_free(index);
	return error;
}

/*
 * Handle the three application options ("locations"):
 *
 * GIT_APPLY_LOCATION_WORKDIR: the default, emulates `git apply`.
 * Applies the diff only to the workdir items and ignores the index
 * entirely.
 *
 * GIT_APPLY_LOCATION_INDEX: emulates `git apply --cached`.
 * Applies the diff only to the index items and ignores the workdir
 * completely.
 *
 * GIT_APPLY_LOCATION_BOTH: emulates `git apply --index`.
 * Applies the diff to both the index items and the working directory
 * items.
 */

int git_apply(
	git_repository *repo,
	git_diff *diff,
	git_apply_options *given_opts)
{
	git_index *postimage = NULL;
	git_reader *pre_reader = NULL;
	git_apply_options opts = GIT_APPLY_OPTIONS_INIT;
	size_t i;
	int error;

	assert(repo && diff);

	GITERR_CHECK_VERSION(
		given_opts, GIT_APPLY_OPTIONS_VERSION, "git_apply_options");

	if (given_opts)
		memcpy(&opts, given_opts, sizeof(git_apply_options));

	/*
	 * by default, we apply a patch directly to the working directory;
	 * in `--cached` or `--index` mode, we apply to the contents already
	 * in the index.
	 */
	if (opts.location == GIT_APPLY_LOCATION_WORKDIR)
		error = git_reader_for_workdir(&pre_reader, repo);
	else
		error = git_reader_for_index(&pre_reader, repo, NULL);

	if (error < 0)
		goto done;

	/*
	 * Build the postimage differences.  Note that this is not the
	 * complete postimage, it only contains the new files created
	 * during the application.  We will limit checkout to only write
	 * the files affected by this diff.
	 */
	if ((error = git_index_new(&postimage)) < 0)
		goto done;

	for (i = 0; i < git_diff_num_deltas(diff); i++) {
		delta = git_diff_get_delta(diff, i);

		if ((error = apply_one(repo, pre_reader, postimage, diff, i)) < 0)
			goto done;
	}

	if (opts.location == GIT_APPLY_LOCATION_INDEX)
		error = git_apply__to_index(repo, diff, postimage, &opts);
	else
		error = git_apply__to_workdir(repo, diff, postimage, &opts);

done:
	git_index_free(postimage);
	git_reader_free(pre_reader);

	return error;
}
