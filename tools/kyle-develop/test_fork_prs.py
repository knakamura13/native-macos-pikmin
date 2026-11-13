"""Fork-PR selection for the kyle-develop rebuild.

The fork was renamed from knakamura13/native-macos-pikmin-1 to
knakamura13/native-macos-pikmin. Selection has to keep pulls under either
name, follow origin, and refuse to drop a pull it cannot classify.
"""

import importlib.util
import pathlib
import unittest


def load_rebuild():
    path = pathlib.Path(__file__).resolve().parent / "rebuild.py"
    spec = importlib.util.spec_from_file_location("kyle_develop_rebuild", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


rebuild = load_rebuild()

SHA_A = "a" * 40
SHA_B = "b" * 40
SHA_C = "c" * 40
SHA_D = "d" * 40


def pull(number, full_name, ref, sha, owner=None, user=None, label=None, repo=True):
    if owner is None and full_name:
        owner = full_name.split("/", 1)[0]
    head = {"ref": ref, "sha": sha}
    if user is not None:
        head["user"] = {"login": user}
    elif owner:
        head["user"] = {"login": owner}
    if label is not None:
        head["label"] = label
    elif owner and ref:
        head["label"] = f"{owner}:{ref}"
    if repo and full_name:
        head["repo"] = {
            "full_name": full_name,
            "owner": {"login": owner} if owner else {},
        }
    else:
        head["repo"] = None
    return {"number": number, "head": head}


class ForkIdentityTest(unittest.TestCase):
    def test_origin_accepts_current_and_previous_names(self):
        ident = rebuild.fork_identity(
            "https://x-access-token:secret@github.com/knakamura13/native-macos-pikmin.git"
        )
        self.assertEqual(ident["owner"], "knakamura13")
        self.assertEqual(
            ident["url"], "https://github.com/knakamura13/native-macos-pikmin.git"
        )
        self.assertNotIn("secret", ident["url"])
        self.assertEqual(
            ident["repos"],
            {
                "knakamura13/native-macos-pikmin",
                "knakamura13/native-macos-pikmin-1",
            },
        )

    def test_old_origin_url_still_accepts_the_renamed_repo(self):
        ident = rebuild.fork_identity(
            "git@github.com:knakamura13/native-macos-pikmin-1.git"
        )
        self.assertIn("knakamura13/native-macos-pikmin", ident["repos"])
        self.assertIn("knakamura13/native-macos-pikmin-1", ident["repos"])
        self.assertEqual(
            ident["url"], "https://github.com/knakamura13/native-macos-pikmin-1.git"
        )

    def test_ssh_and_trailing_slash(self):
        ident = rebuild.fork_identity("ssh://git@github.com/knakamura13/native-macos-pikmin/")
        self.assertEqual(ident["owner"], "knakamura13")
        self.assertEqual(
            ident["url"], "https://github.com/knakamura13/native-macos-pikmin.git"
        )

    def test_another_owners_origin_does_not_inherit_these_names(self):
        ident = rebuild.fork_identity("https://github.com/other/native-macos-pikmin.git")
        self.assertEqual(ident["repos"], {"other/native-macos-pikmin"})

    def test_non_github_origin_is_an_error_without_credentials(self):
        with self.assertRaises(rebuild.RebuildError) as caught:
            rebuild.fork_identity("https://user:secret@gitlab.com/knakamura13/native-macos-pikmin.git")
        self.assertNotIn("secret", str(caught.exception))
        self.assertIn("gitlab.com", str(caught.exception))


class SelectForkPullsTest(unittest.TestCase):
    def setUp(self):
        ident = rebuild.fork_identity("https://github.com/knakamura13/native-macos-pikmin")
        self.owner = ident["owner"]
        self.repos = ident["repos"]

    def test_renamed_fork_keeps_341_and_342_and_lists_other_people(self):
        # The scan that printed "open fork PRs: (none)" dropped these because
        # head.repo.full_name was the new name and the script required the old one.
        pulls = [
            pull(342, "knakamura13/native-macos-pikmin", "knakamura/p2-pal-disc-id", SHA_B),
            pull(341, "knakamura13/native-macos-pikmin", "knakamura/launcher-version-badge", SHA_A),
            pull(10, "someone/their-fork", "feature", SHA_C),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual([item["number"] for item in selected], [341, 342])
        self.assertEqual(selected[0]["ref"], "knakamura/launcher-version-badge")
        self.assertEqual(others, [{
            "number": 10,
            "repo": "someone/their-fork",
            "ref": "feature",
            "owner": "someone",
        }])
        self.assertEqual(
            {item["number"] for item in selected} | {item["number"] for item in others},
            {341, 342, 10},
        )

    def test_previous_name_still_matches_when_origin_is_current(self):
        pulls = [
            pull(7, "knakamura13/native-macos-pikmin-1", "knakamura/old-remote", SHA_A),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual([item["number"] for item in selected], [7])
        self.assertEqual(others, [])

    def test_old_origin_url_still_selects_the_renamed_head(self):
        ident = rebuild.fork_identity(
            "https://github.com/knakamura13/native-macos-pikmin-1.git"
        )
        pulls = [
            pull(341, "knakamura13/native-macos-pikmin", "knakamura/launcher-version-badge", SHA_A),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, ident["owner"], ident["repos"])
        self.assertEqual([item["number"] for item in selected], [341])
        self.assertEqual(others, [])

    def test_missing_head_repo_with_matching_owner_is_kept(self):
        pulls = [
            pull(
                8,
                None,
                "knakamura/deleted-remote",
                SHA_A,
                owner="knakamura13",
                repo=False,
            ),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual(selected[0]["number"], 8)
        self.assertIsNone(selected[0]["repo"])
        self.assertEqual(others, [])

    def test_label_owner_counts_when_the_repo_object_is_gone(self):
        pulls = [{
            "number": 9,
            "head": {
                "label": "knakamura13:knakamura/from-label",
                "ref": "knakamura/from-label",
                "sha": SHA_A,
                "repo": None,
            },
        }]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual([item["number"] for item in selected], [9])
        self.assertEqual(others, [])

    def test_same_owner_under_an_unknown_repo_stops_the_rebuild(self):
        pulls = [
            pull(341, "knakamura13/native-macos-pikmin", "knakamura/launcher-version-badge", SHA_A),
            pull(99, "knakamura13/native-macos-pikmin-2", "knakamura/next-rename", SHA_B),
            pull(100, "knakamura13/another", "knakamura/side", SHA_C),
        ]
        with self.assertRaises(rebuild.RebuildError) as caught:
            rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        message = str(caught.exception)
        self.assertIn("#99", message)
        self.assertIn("knakamura13/native-macos-pikmin-2", message)
        self.assertIn("#100", message)
        self.assertIn("knakamura13/another", message)
        self.assertIn("knakamura13/native-macos-pikmin", message)

    def test_pull_with_no_head_identity_stops_the_rebuild(self):
        pulls = [{
            "number": 55,
            "head": {"ref": "mystery", "sha": SHA_A, "repo": None},
        }]
        with self.assertRaises(rebuild.RebuildError) as caught:
            rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertIn("#55", str(caught.exception))

    def test_deleted_third_party_fork_is_listed_not_fatal(self):
        pulls = [
            pull(3, None, "gone", SHA_A, owner="stranger", user="stranger", repo=False),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual(selected, [])
        self.assertEqual(others[0]["number"], 3)
        self.assertIsNone(others[0]["repo"])
        self.assertEqual(others[0]["owner"], "stranger")

    def test_case_difference_still_matches(self):
        pulls = [
            pull(4, "KNakamura13/native-macos-pikmin", "Fix/Name", SHA_A, owner="KNakamura13"),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual([item["number"] for item in selected], [4])
        self.assertEqual(others, [])

    def test_other_pull_with_a_weird_ref_does_not_abort(self):
        pulls = [
            pull(2, "someone/fork", "bad ref", SHA_A),
        ]
        selected, others = rebuild.select_fork_pulls(pulls, self.owner, self.repos)
        self.assertEqual(selected, [])
        self.assertEqual(others[0]["number"], 2)

    def test_fork_pull_still_requires_a_full_sha_and_a_safe_ref(self):
        with self.assertRaises(rebuild.RebuildError):
            rebuild.select_fork_pulls(
                [pull(1, "knakamura13/native-macos-pikmin", "ok", "abc")],
                self.owner,
                self.repos,
            )
        with self.assertRaises(rebuild.RebuildError):
            rebuild.select_fork_pulls(
                [pull(1, "knakamura13/native-macos-pikmin", "-oops", SHA_D)],
                self.owner,
                self.repos,
            )


if __name__ == "__main__":
    unittest.main()
