#!/usr/bin/env python3
"""Rebuild kyle-develop from upstream main, kyle-patches, and open fork PRs.

Reads the public GitHub API. No token is required. Prints one JSON object on
stdout as its last output. Progress goes to stderr.

  tools/kyle-develop/rebuild.sh
  tools/kyle-develop/rebuild.sh --dry-run

The marker commit format is documented in AGENTS.md.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import urllib.request
from pathlib import Path

UPSTREAM_REPO = "SSunnKing/Open-Nectar---Pikmin-Native-PC-Mobile-Port"
FORK_REPO = "knakamura13/native-macos-pikmin-1"
UPSTREAM_URL = f"https://github.com/{UPSTREAM_REPO}.git"
FORK_URL = f"https://github.com/{FORK_REPO}.git"
MARKER_MAGIC = "kyle-develop-rebuild-v1"
BRANCH = "kyle-develop"
WORKTREE = Path("/tmp/kyle-develop-rebuild")
API = f"https://api.github.com/repos/{UPSTREAM_REPO}/pulls?state=open&per_page=100"
BRANCH_RE = re.compile(r"^[A-Za-z0-9._/-]+$")


class RebuildError(Exception):
    pass


def eprint(*args):
    print(*args, file=sys.stderr)


def git(args, cwd, check=True, env=None):
    run_env = os.environ.copy()
    run_env.setdefault("GIT_EDITOR", "true")
    run_env.setdefault("GIT_MERGE_AUTOEDIT", "no")
    run_env.setdefault("GIT_TERMINAL_PROMPT", "0")
    if env:
        run_env.update(env)
    result = subprocess.run(
        ["git", *args],
        cwd=cwd,
        text=True,
        capture_output=True,
        env=run_env,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip()
        raise RebuildError(f"git {' '.join(args)} failed: {detail}")
    return result


def one_line(text):
    text = " ".join((text or "").split())
    return text or "unspecified"


def valid_branch(name):
    if not name or name.startswith(("-", "/")) or name.endswith("/"):
        return False
    if ".." in name or "@{" in name or "//" in name:
        return False
    return BRANCH_RE.fullmatch(name) is not None


def parse_list(text):
    names = []
    for raw in (text or "").splitlines():
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        if not valid_branch(line):
            raise RebuildError(f"invalid branch name in list file: {line!r}")
        names.append(line)
    return names


def git_show(repo, ref, path):
    result = git(["show", f"{ref}:{path}"], repo, check=False)
    if result.returncode != 0:
        raise RebuildError(f"missing {path} on {ref}")
    return result.stdout


def http_json(url):
    request = urllib.request.Request(
        url,
        headers={
            "Accept": "application/vnd.github+json",
            "User-Agent": "kyle-develop-rebuild",
            "X-GitHub-Api-Version": "2022-11-28",
        },
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        body = response.read().decode()
        link = response.headers.get("Link")
    return json.loads(body), link


def next_link(header):
    if not header:
        return None
    for part in header.split(","):
        if 'rel="next"' in part:
            url = part.split(";", 1)[0].strip()
            if url.startswith("<") and url.endswith(">"):
                return url[1:-1]
    return None


def open_fork_prs():
    url = API
    found = []
    while url:
        batch, link = http_json(url)
        if not isinstance(batch, list):
            raise RebuildError("GitHub pulls API did not return a list")
        for pr in batch:
            head = pr.get("head") or {}
            repo = (head.get("repo") or {}).get("full_name")
            if repo != FORK_REPO:
                continue
            number = int(pr["number"])
            ref = head.get("ref") or ""
            sha = head.get("sha") or ""
            if not valid_branch(ref):
                raise RebuildError(f"PR #{number} has an unsafe head ref {ref!r}")
            if not re.fullmatch(r"[0-9a-f]{40}", sha):
                raise RebuildError(f"PR #{number} has no full head SHA")
            found.append({"number": number, "ref": ref, "sha": sha})
        url = next_link(link)
    found.sort(key=lambda item: item["number"])
    return found


def ensure_upstream_remote(repo):
    result = git(["remote", "get-url", "upstream"], repo, check=False)
    if result.returncode != 0:
        git(["remote", "add", "upstream", UPSTREAM_URL], repo)
        eprint(f"added remote upstream {UPSTREAM_URL}")


def conflict_files(cwd):
    result = git(["diff", "--name-only", "--diff-filter=U"], cwd, check=False)
    return sorted({line.strip() for line in result.stdout.splitlines() if line.strip()})


def reason_conflict(files, fallback):
    if files:
        return "conflict: " + " ".join(files)
    return "conflict: " + one_line(fallback)[:300]


def abort_op(cwd, kind):
    git([kind, "--abort"], cwd, check=False)
    # A failed abort leaves the worktree unusable. Reset hard if needed.
    status = git(["status", "--porcelain"], cwd, check=False)
    if status.returncode != 0 or "UU " in status.stdout or status.stdout.startswith(("UU", "AA", "DD")):
        git(["reset", "--hard", "HEAD"], cwd, check=False)


def cherry_pick(cwd, sha):
    result = git(["cherry-pick", "--no-edit", sha], cwd, check=False)
    if result.returncode == 0:
        return "applied", ""
    combined = (result.stdout or "") + (result.stderr or "")
    if "now empty" in combined or "empty commit" in combined:
        skipped = git(["cherry-pick", "--skip"], cwd, check=False)
        if skipped.returncode != 0:
            abort_op(cwd, "cherry-pick")
        return "empty", "already applied (empty cherry-pick)"
    files = conflict_files(cwd)
    reason = reason_conflict(files, combined)
    abort_op(cwd, "cherry-pick")
    return "conflict", reason


def keep_both_text(text):
    """Join each conflict hunk as ours followed by theirs.

    Returns None when a side is empty (a real delete) or the markers are not
    the plain two-way form. Both sides of an adjacent insertion, such as two
    new test blocks in CMakeLists.txt, are kept. A later test failure still
    drops the step.
    """
    lines = text.splitlines(keepends=True)
    out = []
    i = 0
    hunks = 0
    while i < len(lines):
        if not lines[i].startswith("<<<<<<<"):
            out.append(lines[i])
            i += 1
            continue
        i += 1
        ours = []
        while i < len(lines) and not lines[i].startswith("======="):
            if lines[i].startswith("<<<<<<<") or lines[i].startswith(">>>>>>>"):
                return None
            ours.append(lines[i])
            i += 1
        if i >= len(lines) or not lines[i].startswith("======="):
            return None
        i += 1
        if i < len(lines) and lines[i].startswith("|||||||"):
            return None
        theirs = []
        while i < len(lines) and not lines[i].startswith(">>>>>>>"):
            if lines[i].startswith("<<<<<<<") or lines[i].startswith("======="):
                return None
            theirs.append(lines[i])
            i += 1
        if i >= len(lines) or not lines[i].startswith(">>>>>>>"):
            return None
        i += 1
        if not "".join(ours).strip() or not "".join(theirs).strip():
            return None
        out.extend(ours)
        out.extend(theirs)
        hunks += 1
    if hunks == 0:
        return None
    result = "".join(out)
    if "<<<<<<<" in result or ">>>>>>>" in result:
        return None
    return result


def resolve_keep_both(cwd):
    files = conflict_files(cwd)
    if not files:
        return None
    rewritten = {}
    root = Path(cwd)
    for name in files:
        path = root / name
        data = path.read_bytes()
        if b"\0" in data:
            return None
        try:
            text = data.decode()
        except UnicodeDecodeError:
            return None
        updated = keep_both_text(text)
        if updated is None:
            return None
        rewritten[name] = updated
    for name, updated in rewritten.items():
        (root / name).write_text(updated)
        git(["add", "--", name], cwd)
    committed = git(["commit", "--no-edit"], cwd, check=False)
    if committed.returncode != 0:
        abort_op(cwd, "merge")
        return None
    return files


def merge_sha(cwd, sha, message):
    result = git(["merge", "--no-ff", "--no-edit", "-m", message, sha], cwd, check=False)
    combined = (result.stdout or "") + (result.stderr or "")
    if result.returncode == 0:
        if "Already up to date" in combined:
            return "already", ""
        return "applied", ""
    resolved = resolve_keep_both(cwd)
    if resolved is not None:
        return "resolved", resolved
    files = conflict_files(cwd)
    reason = reason_conflict(files, combined)
    abort_op(cwd, "merge")
    return "conflict", reason


def fetch_sha(repo, url, refspec):
    result = git(["fetch", "--no-tags", url, refspec], repo, check=False)
    if result.returncode != 0:
        detail = one_line((result.stderr or result.stdout or "fetch failed"))
        return None, detail
    shown = git(["rev-parse", "FETCH_HEAD"], repo)
    return shown.stdout.strip(), ""


def prepare_worktree(repo, sha):
    listed = git(["worktree", "list", "--porcelain"], repo).stdout
    if str(WORKTREE) in listed:
        git(["worktree", "remove", "--force", str(WORKTREE)], repo, check=False)
    if WORKTREE.exists():
        shutil.rmtree(WORKTREE)
    git(["worktree", "add", "--detach", str(WORKTREE), sha], repo)
    return WORKTREE


def reset_worktree(worktree, sha):
    git(["checkout", "--detach", sha], worktree)
    git(["reset", "--hard", sha], worktree)
    git(["clean", "-fd", "-e", "build"], worktree)


def jobs():
    chosen = os.environ.get("KYLE_BUILD_JOBS")
    if chosen:
        return chosen
    return str(os.cpu_count() or 2)


def build_and_ctest(worktree, source, build_name):
    build = worktree / "build" / build_name
    configure = [
        "cmake",
        "-S",
        str(worktree / source),
        "-B",
        str(build),
        "-DCMAKE_BUILD_TYPE=Release",
    ]
    if shutil.which("ccache"):
        configure += [
            "-DCMAKE_C_COMPILER_LAUNCHER=ccache",
            "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache",
        ]
    steps = []
    if not (build / "CMakeCache.txt").exists():
        steps.append(configure)
    steps.append(["cmake", "--build", str(build), "-j", jobs()])
    steps.append(["ctest", "--test-dir", str(build), "--output-on-failure"])
    logs = []
    for command in steps:
        eprint("+", " ".join(command))
        result = subprocess.run(command, cwd=worktree, text=True, capture_output=True)
        text = (result.stdout or "") + (result.stderr or "")
        logs.append(text)
        for line in text.splitlines():
            if "tests passed" in line or "tests failed" in line:
                eprint(line)
        if result.returncode != 0:
            tail = "\n".join(text.splitlines()[-40:])
            eprint(tail)
            return False, "\n".join(logs)
    return True, "\n".join(logs)


def tests_pass(worktree, sha, cache):
    if sha in cache:
        eprint(f"tests cached {sha[:12]} -> {'pass' if cache[sha] else 'fail'}")
        return cache[sha]
    eprint(f"testing {sha}")
    reset_worktree(worktree, sha)
    ok1, _ = build_and_ctest(worktree, "games/pikmin1", "pikmin1")
    ok2 = False
    if ok1:
        ok2, _ = build_and_ctest(worktree, "games/pikmin2", "pikmin2")
    cache[sha] = ok1 and ok2
    eprint(f"tests {sha[:12]} pikmin1={'pass' if ok1 else 'fail'} pikmin2={'pass' if ok2 else 'fail'}")
    return cache[sha]


def first_failing_index(worktree, checkpoints, cache):
    """checkpoints[0] is the upstream base. Return the index of the first bad
    step, or 0 when the base itself fails. Assumes a later step does not repair
    an earlier failure; the caller retests the full tree after omitting it."""
    if not tests_pass(worktree, checkpoints[0][1], cache):
        return 0
    lo = 0
    hi = len(checkpoints) - 1
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if tests_pass(worktree, checkpoints[mid][1], cache):
            lo = mid
        else:
            hi = mid
    return hi


class Assembly:
    def __init__(self):
        self.head = None
        self.checkpoints = []  # (step or None, sha) ; index 0 is the base
        self.applied_patches = []
        self.merged_prs = []
        self.merged_includes = []
        self.resolved = []
        self.skipped = []


def assemble(worktree, upstream_sha, steps):
    reset_worktree(worktree, upstream_sha)
    asm = Assembly()
    asm.checkpoints.append((None, upstream_sha))
    for step in steps:
        if step.get("omit"):
            asm.skipped.append(step["skip"])
            continue
        kind = step["kind"]
        if kind == "patch":
            status, reason = cherry_pick(worktree, step["sha"])
            if status == "applied":
                asm.applied_patches.append(step["sha"])
                asm.checkpoints.append((step, git(["rev-parse", "HEAD"], worktree).stdout.strip()))
            else:
                asm.skipped.append({
                    "kind": "patch",
                    "id": step["sha"],
                    "sha": step["sha"],
                    "reason": reason,
                })
            continue
        if kind == "pr":
            message = f"Merge upstream PR #{step['number']} ({step['ref']})\n\nHead {step['sha']}"
        else:
            message = f"Merge fork branch {step['ref']}\n\nHead {step['sha']}"
        status, reason = merge_sha(worktree, step["sha"], message)
        if status in ("applied", "already", "resolved"):
            if kind == "pr":
                asm.merged_prs.append({"number": step["number"], "sha": step["sha"], "ref": step["ref"]})
                if status == "resolved":
                    asm.resolved.append({
                        "number": step["number"],
                        "sha": step["sha"],
                        "files": list(reason),
                    })
            else:
                asm.merged_includes.append({"branch": step["ref"], "sha": step["sha"]})
            if status in ("applied", "resolved"):
                asm.checkpoints.append((step, git(["rev-parse", "HEAD"], worktree).stdout.strip()))
        else:
            ident = str(step["number"]) if kind == "pr" else step["ref"]
            asm.skipped.append({
                "kind": kind,
                "id": ident,
                "sha": step["sha"],
                "reason": reason,
            })
    asm.head = git(["rev-parse", "HEAD"], worktree).stdout.strip()
    return asm


def marker_message(upstream_sha, patches_sha, asm):
    lines = [MARKER_MAGIC, "", f"upstream_main {upstream_sha}", f"kyle_patches {patches_sha}"]
    for pr in sorted(asm.merged_prs, key=lambda item: item["number"]):
        lines.append(f"merged_pr {pr['number']} {pr['sha']}")
    for item in sorted(asm.resolved, key=lambda item: item["number"]):
        files = " ".join(item["files"])
        lines.append(f"resolved_pr {item['number']} {item['sha']} both-sides: {files}")
    for item in sorted(asm.merged_includes, key=lambda item: item["branch"]):
        lines.append(f"merged_include {item['branch']} {item['sha']}")
    skipped_prs = [s for s in asm.skipped if s["kind"] == "pr"]
    skipped_includes = [s for s in asm.skipped if s["kind"] == "include"]
    skipped_patches = [s for s in asm.skipped if s["kind"] == "patch"]
    for item in sorted(skipped_prs, key=lambda item: int(item["id"])):
        lines.append(f"skipped_pr {item['id']} {item['sha']} {one_line(item['reason'])}")
    for item in sorted(skipped_includes, key=lambda item: item["id"]):
        sha = item["sha"] or "none"
        lines.append(f"skipped_include {item['id']} {sha} {one_line(item['reason'])}")
    for item in sorted(skipped_patches, key=lambda item: item["sha"]):
        lines.append(f"skipped_patch {item['sha']} {one_line(item['reason'])}")
    return "\n".join(lines) + "\n"


def summary_object(upstream_sha, patches_sha, asm, final_sha, pushed, dry_run, tests):
    return {
        "format": MARKER_MAGIC,
        "upstream_main": upstream_sha,
        "kyle_patches": patches_sha,
        "merged_prs": [
            {"number": pr["number"], "sha": pr["sha"], "ref": pr["ref"]}
            for pr in sorted(asm.merged_prs, key=lambda item: item["number"])
        ],
        "resolved_prs": [
            {"number": item["number"], "sha": item["sha"], "files": item["files"]}
            for item in sorted(asm.resolved, key=lambda item: item["number"])
        ],
        "merged_includes": sorted(asm.merged_includes, key=lambda item: item["branch"]),
        "applied_patches": list(asm.applied_patches),
        "skipped": [
            {"kind": item["kind"], "id": item["id"], "sha": item["sha"], "reason": one_line(item["reason"])}
            for item in asm.skipped
        ],
        "tests": tests,
        "kyle_develop": final_sha,
        "pushed": pushed,
        "dry_run": dry_run,
    }


def refuse_in_progress(repo):
    git_dir = Path(git(["rev-parse", "--git-path", "."], repo).stdout.strip())
    if not git_dir.is_absolute():
        git_dir = (Path(repo) / git_dir).resolve()
    for name in ("MERGE_HEAD", "CHERRY_PICK_HEAD", "REBASE_HEAD", "BISECT_LOG"):
        if (git_dir / name).exists():
            raise RebuildError(f"this checkout is busy ({name}); finish or abort it first")


def is_ancestor(repo, older, newer):
    return git(["merge-base", "--is-ancestor", older, newer], repo, check=False).returncode == 0


def is_upstream_sync(repo, parents, upstream_sha):
    """A merge whose every non-first parent is already in upstream main."""
    return all(is_ancestor(repo, parent, upstream_sha) for parent in parents[1:])


def check_patch_merges(repo, base, patches_sha, upstream_sha):
    """Refuse kyle-patches history that a non-merge replay would get wrong.

    Allowed merge commits:
      - a merge of upstream main (every non-first parent is in upstream main);
      - a PR merge whose merged-in side holds only plain commits and merges of
        upstream main.
    Anything else, such as a branch that merged older kyle-patches or other fork
    branches into itself, raises before anything is built or pushed.
    """
    listed = git(
        ["rev-list", "--merges", "--parents", f"{base}..{patches_sha}"], repo
    ).stdout.splitlines()
    bad = []
    for line in listed:
        merge, *parents = line.split()
        if is_upstream_sync(repo, parents, upstream_sha):
            continue
        side_ok = True
        for parent in parents[1:]:
            side = git(
                ["rev-list", "--merges", "--parents", f"{parents[0]}..{parent}"], repo
            ).stdout.splitlines()
            for inner in side:
                _, *inner_parents = inner.split()
                if not is_upstream_sync(repo, inner_parents, upstream_sha):
                    side_ok = False
                    break
            if not side_ok:
                break
        if not side_ok:
            bad.append(merge)
    if bad:
        raise RebuildError(
            "kyle-patches contains merge commits that are not merges of upstream main "
            "or of a linear PR branch: "
            + " ".join(sha[:12] for sha in bad)
            + ". Replace them with plain commits; not pushing"
        )


def build_steps(repo, patches_sha, upstream_sha, exclude, include_names):
    base = git(["merge-base", patches_sha, upstream_sha], repo).stdout.strip()
    check_patch_merges(repo, base, patches_sha, upstream_sha)
    # Merge commits carry no changes of their own here: the plain commits they
    # bring in are replayed one by one, and upstream main is the base.
    patch_commits = git(
        ["rev-list", "--reverse", "--no-merges", f"{base}..{patches_sha}"], repo
    ).stdout.split()
    steps = []
    for sha in patch_commits:
        steps.append({"kind": "patch", "sha": sha, "ident": f"patch:{sha}"})

    prs = open_fork_prs()
    eprint(f"open fork PRs: {', '.join('#' + str(pr['number']) for pr in prs) or '(none)'}")
    seen_refs = set()
    for pr in prs:
        seen_refs.add(pr["ref"])
        if pr["ref"] in exclude:
            steps.append({
                "kind": "note",
                "ident": f"pr:{pr['number']}",
                "omit": True,
                "skip": {
                    "kind": "pr",
                    "id": str(pr["number"]),
                    "sha": pr["sha"],
                    "reason": "excluded",
                },
            })
            continue
        fetched, err = fetch_sha(repo, UPSTREAM_URL, f"refs/pull/{pr['number']}/head")
        if not fetched:
            steps.append({
                "kind": "note",
                "ident": f"pr:{pr['number']}",
                "omit": True,
                "skip": {
                    "kind": "pr",
                    "id": str(pr["number"]),
                    "sha": pr["sha"],
                    "reason": f"fetch failed: {err}",
                },
            })
            continue
        if fetched != pr["sha"]:
            eprint(f"PR #{pr['number']} moved {pr['sha'][:12]} -> {fetched[:12]}; using fetched head")
        steps.append({
            "kind": "pr",
            "number": pr["number"],
            "ref": pr["ref"],
            "sha": fetched,
            "ident": f"pr:{pr['number']}",
        })

    for name in include_names:
        if name in exclude:
            steps.append({
                "kind": "note",
                "ident": f"include:{name}",
                "omit": True,
                "skip": {
                    "kind": "include",
                    "id": name,
                    "sha": None,
                    "reason": "excluded",
                },
            })
            continue
        fetched, err = fetch_sha(repo, FORK_URL, f"refs/heads/{name}")
        if name in seen_refs:
            # The open-PR pass already merges this branch. Record it so the
            # marker still names the include, and the scheduler can see its SHA.
            steps.append({
                "kind": "note",
                "ident": f"include:{name}",
                "omit": True,
                "skip": {
                    "kind": "include",
                    "id": name,
                    "sha": fetched,
                    "reason": "already merged as an open upstream PR" if fetched else f"fetch failed: {err}",
                },
            })
            continue
        if not fetched:
            steps.append({
                "kind": "note",
                "ident": f"include:{name}",
                "omit": True,
                "skip": {
                    "kind": "include",
                    "id": name,
                    "sha": None,
                    "reason": f"fetch failed: {err}",
                },
            })
            continue
        steps.append({
            "kind": "include",
            "ref": name,
            "sha": fetched,
            "ident": f"include:{name}",
        })
    return steps


def omit_step(steps, ident, reason):
    for step in steps:
        if step.get("ident") != ident or step.get("omit"):
            continue
        if step["kind"] == "pr":
            skip = {
                "kind": "pr",
                "id": str(step["number"]),
                "sha": step["sha"],
                "reason": reason,
            }
        elif step["kind"] == "include":
            skip = {
                "kind": "include",
                "id": step["ref"],
                "sha": step["sha"],
                "reason": reason,
            }
        else:
            skip = {
                "kind": "patch",
                "id": step["sha"],
                "sha": step["sha"],
                "reason": reason,
            }
        step["omit"] = True
        step["skip"] = skip
        return True
    return False


def push_kyle_develop(repo, new_sha, dry_run):
    if dry_run:
        eprint(f"dry-run: not pushing {new_sha}")
        return False
    git(["fetch", "--no-tags", "origin", BRANCH], repo)
    expected = git(["rev-parse", f"refs/remotes/origin/{BRANCH}"], repo).stdout.strip()
    eprint(f"force-with-lease {BRANCH} {expected[:12]} -> {new_sha[:12]}")
    git(
        [
            "push",
            f"--force-with-lease=refs/heads/{BRANCH}:{expected}",
            "origin",
            f"{new_sha}:refs/heads/{BRANCH}",
        ],
        repo,
    )
    # Move the local branch only when it is not the checkout in use.
    current = git(["rev-parse", "--abbrev-ref", "HEAD"], repo, check=False).stdout.strip()
    if current != BRANCH:
        git(["branch", "-f", BRANCH, new_sha], repo, check=False)
    return True


def main():
    parser = argparse.ArgumentParser(description="Rebuild and force-push kyle-develop")
    parser.add_argument("--dry-run", action="store_true", help="do everything except push")
    parser.add_argument("--patches-ref", default="origin/kyle-patches")
    args = parser.parse_args()

    repo = git(["rev-parse", "--show-toplevel"], ".").stdout.strip()
    try:
        refuse_in_progress(repo)
        ensure_upstream_remote(repo)
        eprint("fetching upstream main and origin kyle-patches")
        git(["fetch", "--no-tags", "upstream", "main"], repo)
        git(["fetch", "--no-tags", "origin", "kyle-patches"], repo)
        upstream_sha = git(["rev-parse", "upstream/main"], repo).stdout.strip()
        patches_ref = args.patches_ref
        if patches_ref == "origin/kyle-patches":
            patches_sha = git(["rev-parse", "refs/remotes/origin/kyle-patches"], repo).stdout.strip()
        else:
            patches_sha = git(["rev-parse", patches_ref], repo).stdout.strip()

        exclude = set(parse_list(git_show(repo, patches_sha, "tools/kyle-develop/exclude.txt")))
        include_names = parse_list(git_show(repo, patches_sha, "tools/kyle-develop/include.txt"))
        eprint(f"upstream_main {upstream_sha}")
        eprint(f"kyle_patches {patches_sha}")
        eprint(f"exclude {sorted(exclude) or '(empty)'}")
        eprint(f"include {include_names or '(empty)'}")

        steps = build_steps(repo, patches_sha, upstream_sha, exclude, include_names)
        worktree = prepare_worktree(repo, upstream_sha)
        cache = {}
        asm = None
        guard = 0
        while True:
            guard += 1
            if guard > len(steps) + 2:
                raise RebuildError("could not find a test-passing subset")
            eprint(f"assembling (pass {guard})")
            asm = assemble(worktree, upstream_sha, steps)
            if tests_pass(worktree, asm.head, cache):
                break
            if len(asm.checkpoints) < 2:
                raise RebuildError("upstream main failed the test suites; not pushing")
            index = first_failing_index(worktree, asm.checkpoints, cache)
            if index == 0:
                raise RebuildError("upstream main failed the test suites; not pushing")
            step = asm.checkpoints[index][0]
            eprint(f"tests failed at {step['ident']}; leaving it out")
            if not omit_step(steps, step["ident"], "tests failed when this step was included"):
                raise RebuildError(f"could not omit {step['ident']}")

        reset_worktree(worktree, asm.head)
        message = marker_message(upstream_sha, patches_sha, asm)
        git(["commit", "--allow-empty", "-m", message], worktree)
        final_sha = git(["rev-parse", "HEAD"], worktree).stdout.strip()
        eprint(f"marker {final_sha}")
        eprint(message.rstrip())
        pushed = push_kyle_develop(repo, final_sha, args.dry_run)
        if pushed:
            git(["worktree", "remove", "--force", str(WORKTREE)], repo, check=False)
            if WORKTREE.exists():
                shutil.rmtree(WORKTREE, ignore_errors=True)
        else:
            eprint(f"dry-run worktree: {WORKTREE}")
        payload = summary_object(
            upstream_sha,
            patches_sha,
            asm,
            final_sha,
            pushed,
            args.dry_run,
            {"pikmin1": "pass", "pikmin2": "pass"},
        )
        print(json.dumps(payload, indent=2))
        return 0
    except RebuildError as exc:
        eprint(f"error: {exc}")
        payload = {
            "format": MARKER_MAGIC,
            "pushed": False,
            "dry_run": args.dry_run,
            "kyle_develop": None,
            "error": str(exc),
        }
        print(json.dumps(payload, indent=2))
        return 1
    finally:
        # Keep the worktree on failure so the tree can be inspected. Remove it
        # after a successful push. A dry-run keeps it at the result commit.
        pass


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
