#include "clar_libgit2.h"
#include "apply_helpers.h"

static git_repository *repo;

#define TEST_REPO_PATH "merge-recursive"

void test_apply_workdir__initialize(void)
{
	git_oid oid;
	git_commit *commit;

	repo = cl_git_sandbox_init(TEST_REPO_PATH);

	git_oid_fromstr(&oid, "539bd011c4822c560c1d17cab095006b7a10f707");
	cl_git_pass(git_commit_lookup(&commit, repo, &oid));
	cl_git_pass(git_reset(repo, (git_object *)commit, GIT_RESET_HARD, NULL));
	git_commit_free(commit);
}

void test_apply_workdir__cleanup(void)
{
	cl_git_sandbox_cleanup();
}

void test_apply_workdir__simple_calculated(void)
{
	git_oid a_oid, b_oid;
	git_commit *a_commit, *b_commit;
	git_tree *a_tree, *b_tree;
	git_diff *diff;
	git_diff_options opts = GIT_DIFF_OPTIONS_INIT;

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "ffb36e513f5fdf8a6ba850a20142676a2ac4807d", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "c4e6cca3ec6ae0148ed231f97257df8c311e015f", 0, "gravy.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "a7b066537e6be7109abfe4ff97b675d4e077da20", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
		sizeof(struct merge_index_entry);

	git_oid_fromstr(&a_oid, "539bd011c4822c560c1d17cab095006b7a10f707");
	git_oid_fromstr(&b_oid, "7c7bf85e978f1d18c0566f702d2cb7766b9c8d4f"); cl_git_pass(git_commit_lookup(&a_commit, repo, &a_oid));
	cl_git_pass(git_commit_lookup(&b_commit, repo, &b_oid));

	cl_git_pass(git_commit_tree(&a_tree, a_commit));
	cl_git_pass(git_commit_tree(&b_tree, b_commit));

	cl_git_pass(git_diff_tree_to_tree(&diff, repo, a_tree, b_tree, &opts));

	cl_git_pass(git_apply(repo, diff, NULL));

	validate_index_unchanged(repo);
	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_diff_free(diff);
	git_tree_free(a_tree);
	git_tree_free(b_tree);
	git_commit_free(a_commit);
	git_commit_free(b_commit);
}

void test_apply_workdir__simple_parsed(void)
{
	git_diff *diff;

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "ffb36e513f5fdf8a6ba850a20142676a2ac4807d", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "c4e6cca3ec6ae0148ed231f97257df8c311e015f", 0, "gravy.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "a7b066537e6be7109abfe4ff97b675d4e077da20", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
	    sizeof(struct merge_index_entry);

	cl_git_pass(git_diff_from_buffer(&diff,
		DIFF_MODIFY_TWO_FILES, strlen(DIFF_MODIFY_TWO_FILES)));
	cl_git_pass(git_apply(repo, diff, NULL));

	validate_index_unchanged(repo);
	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_diff_free(diff);
}

void test_apply_workdir__simple_removes_file(void)
{
	git_diff *diff;

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "f51658077d85f2264fa179b4d0848268cb3475c3", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "94d2c01087f48213bd157222d54edfefd77c9bba", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
	    sizeof(struct merge_index_entry);

	cl_git_pass(git_diff_from_buffer(&diff, DIFF_DELETE_FILE,
		strlen(DIFF_DELETE_FILE)));
	cl_git_pass(git_apply(repo, diff, NULL));

	validate_index_unchanged(repo);
	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_diff_free(diff);
}

void test_apply_workdir__simple_adds_file(void)
{
	git_diff *diff;

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "f51658077d85f2264fa179b4d0848268cb3475c3", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "c4e6cca3ec6ae0148ed231f97257df8c311e015f", 0, "gravy.txt" },
		{ 0100644, "6370543fcfedb3e6516ec53b06158f3687dc1447", 0, "newfile.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "94d2c01087f48213bd157222d54edfefd77c9bba", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
	    sizeof(struct merge_index_entry);

	cl_git_pass(git_diff_from_buffer(&diff,
		DIFF_ADD_FILE, strlen(DIFF_ADD_FILE)));
	cl_git_pass(git_apply(repo, diff, NULL));

	validate_index_unchanged(repo);
	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_diff_free(diff);
}

void test_apply_workdir__modified_index_with_unmodified_workdir_is_ok(void)
{
	git_index *index;
	git_index_entry idx_entry = {{0}};
	git_diff *diff;

	const char *diff_file = DIFF_MODIFY_TWO_FILES;

	struct merge_index_entry index_expected[] = {
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "c4e6cca3ec6ae0148ed231f97257df8c311e015f", 0, "gravy.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "ffb36e513f5fdf8a6ba850a20142676a2ac4807d", 0, "veal.txt" }
	};
	size_t index_expected_cnt = sizeof(index_expected) /
		sizeof(struct merge_index_entry);

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "ffb36e513f5fdf8a6ba850a20142676a2ac4807d", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "c4e6cca3ec6ae0148ed231f97257df8c311e015f", 0, "gravy.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "a7b066537e6be7109abfe4ff97b675d4e077da20", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
		sizeof(struct merge_index_entry);

	/* mutate the index and leave the workdir matching HEAD */
	git_repository_index(&index, repo);

	idx_entry.mode = 0100644;
	idx_entry.path = "veal.txt";
	cl_git_pass(git_oid_fromstr(&idx_entry.id, "ffb36e513f5fdf8a6ba850a20142676a2ac4807d"));

	git_index_add(index, &idx_entry);
	git_index_remove(index, "asparagus.txt", 0);
	cl_git_pass(git_index_write(index));

	cl_git_pass(git_diff_from_buffer(&diff, diff_file, strlen(diff_file)));
	cl_git_pass(git_apply(repo, diff, NULL));

	validate_apply_index(repo, index_expected, index_expected_cnt);
	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_index_free(index);
	git_diff_free(diff);
}

void test_apply_workdir__application_failure_leaves_workdir_unmodified(void)
{
	git_diff *diff;

	const char *diff_file = DIFF_MODIFY_TWO_FILES;

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "f51658077d85f2264fa179b4d0848268cb3475c3", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "c4e6cca3ec6ae0148ed231f97257df8c311e015f", 0, "gravy.txt" },
		{ 0100644, "68af1fc7407fd9addf1701a87eb1c95c7494c598", 0, "oyster.txt" },
		{ 0100644, "8684724651336001c5dbce74bed6736d2443958d", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
		sizeof(struct merge_index_entry);

	/* mutate the workdir */
	cl_git_rewritefile("merge-recursive/veal.txt",
	    "This is a modification.\n");

	cl_git_pass(git_diff_from_buffer(&diff, diff_file, strlen(diff_file)));
	cl_git_fail_with(GIT_EAPPLYFAIL, git_apply(repo, diff, NULL));

	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_diff_free(diff);
}

void test_apply_workdir__keeps_nonconflicting_changes(void)
{
	git_diff *diff;

	struct merge_index_entry workdir_expected[] = {
		{ 0100644, "ffb36e513f5fdf8a6ba850a20142676a2ac4807d", 0, "asparagus.txt" },
		{ 0100644, "68f6182f4c85d39e1309d97c7e456156dc9c0096", 0, "beef.txt" },
		{ 0100644, "4b7c5650008b2e747fe1809eeb5a1dde0e80850a", 0, "bouilli.txt" },
		{ 0100644, "f75ba05f340c51065cbea2e1fdbfe5fe13144c97", 0, "gravy.txt" },
		{ 0100644, "a7b066537e6be7109abfe4ff97b675d4e077da20", 0, "veal.txt" },
	};
	size_t workdir_expected_cnt = sizeof(workdir_expected) /
	    sizeof(struct merge_index_entry);

	cl_git_rmfile("merge-recursive/oyster.txt");
	cl_git_rewritefile("merge-recursive/gravy.txt", "Hello, world.\n");

	cl_git_pass(git_diff_from_buffer(&diff,
		DIFF_MODIFY_TWO_FILES, strlen(DIFF_MODIFY_TWO_FILES)));
	cl_git_pass(git_apply(repo, diff, NULL));

	validate_index_unchanged(repo);
	validate_apply_workdir(repo, workdir_expected, workdir_expected_cnt);

	git_diff_free(diff);
}
