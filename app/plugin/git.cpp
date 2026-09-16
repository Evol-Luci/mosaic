#include "app/plugin/git.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace mosaic::app
{
namespace
{

// Runs `args` (args[0] must be "git") via fork()+execvp(), never through a
// shell -- both the URL and destination path passed to git_clone/git_pull are
// effectively user input, and system()/popen() pass their argument through
// /bin/sh -c, letting shell metacharacters (`, $(), ;, |) execute arbitrary
// commands. The child sets GIT_TERMINAL_PROMPT=0 so an auth prompt against a
// private repo fails immediately instead of hanging.
GitResult run_git(const std::vector<std::string>& args)
{
    std::array<int, 2> pipe_fds{-1, -1};
    if (pipe(pipe_fds.data()) != 0)
    {
        return GitResult{false, std::string("pipe() failed: ") + std::strerror(errno)};
    }

    const pid_t pid = fork();
    if (pid < 0)
    {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return GitResult{false, std::string("fork() failed: ") + std::strerror(errno)};
    }

    if (pid == 0)
    {
        // Child: redirect stdout+stderr into the pipe's write end, then exec git.
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);

        setenv("GIT_TERMINAL_PROMPT", "0", 1);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args)
        {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp("git", argv.data());
        _exit(127); // execvp only returns on failure
    }

    // Parent: read the child's combined output to EOF, then reap it.
    close(pipe_fds[1]);
    std::string           output;
    std::array<char, 4096> buf{};
    ssize_t n = 0;
    while ((n = read(pipe_fds[0], buf.data(), buf.size())) > 0)
    {
        output.append(buf.data(), static_cast<std::size_t>(n));
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    return GitResult{ok, output};
}

} // namespace

bool is_git_url(const std::string& value)
{
    static constexpr std::array<const char*, 4> kSchemes = {"https://", "http://", "ssh://", "git@"};
    for (const char* scheme : kSchemes)
    {
        if (value.rfind(scheme, 0) == 0)
        {
            return true;
        }
    }
    return value.size() > 4 && value.compare(value.size() - 4, 4, ".git") == 0;
}

GitResult git_clone(const std::string& url, const std::filesystem::path& dest)
{
    std::error_code ec;
    if (std::filesystem::exists(dest, ec))
    {
        return GitResult{false, "destination already exists: " + dest.string()};
    }
    return run_git({"git", "clone", url, dest.string()});
}

GitResult git_pull(const std::filesystem::path& repo_dir)
{
    return run_git({"git", "-C", repo_dir.string(), "pull"});
}

} // namespace mosaic::app
