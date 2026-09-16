#pragma once

#include <filesystem>
#include <string>

namespace mosaic::app
{

// True if `value` looks like a git remote rather than a local filesystem path: a
// URL scheme (https://, http://, ssh://), the git@host: SSH shorthand, or a
// trailing ".git". Anything else is treated as a literal local path by the
// caller (mirrors resolve_source()'s existing "unrecognized value is just a
// path" precedent).
bool is_git_url(const std::string& value);

// The result of running one git subprocess: whether it exited zero, and its
// combined stdout+stderr (for error messages and logging -- callers don't need
// to know which stream a given line came from).
struct GitResult
{
    bool        ok = false;
    std::string output;
};

// Clones `url` into `dest`, which must not already exist. Runs with
// GIT_TERMINAL_PROMPT=0 so an auth prompt against a private repo fails fast
// instead of hanging forever on a prompt Mosaic has no terminal to show.
GitResult git_clone(const std::string& url, const std::filesystem::path& dest);

// Runs `git pull` inside `repo_dir`, which must already be a git working tree.
GitResult git_pull(const std::filesystem::path& repo_dir);

} // namespace mosaic::app
