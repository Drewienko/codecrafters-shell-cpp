#include "pipeline_executor.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include "fd_utils.hpp"

namespace
{
  bool bindPipelineInput(const UniqueFd &prevRead)
  {
    if (!prevRead)
      return true;
    if (dup2(prevRead.get(), STDIN_FILENO) < 0)
    {
      perror("dup2");
      return false;
    }
    return true;
  }

  bool bindPipelineOutput(const PipeFds &pipeFds, bool shouldPipeOutput)
  {
    if (!shouldPipeOutput)
      return true;
    if (dup2(pipeFds.write.get(), STDOUT_FILENO) < 0)
    {
      perror("dup2");
      return false;
    }
    return true;
  }

  void closeChildPipes(UniqueFd &prevRead, PipeFds &pipeFds, bool hasNext)
  {
    if (prevRead)
      prevRead.reset();
    if (hasNext)
    {
      pipeFds.read.reset();
      pipeFds.write.reset();
    }
  }

  void advanceParentPipe(UniqueFd &prevRead, PipeFds &pipeFds, bool hasNext)
  {
    if (prevRead)
      prevRead.reset();
    if (hasNext)
    {
      pipeFds.write.reset();
      prevRead = std::move(pipeFds.read);
    }
  }
}

int PipelineExecutor::run(const std::vector<ParsedCommand> &commands, const Runner &runner) const
{
  if (commands.empty())
    return 0;

  std::vector<pid_t> pids{};
  pids.reserve(commands.size());

  UniqueFd prevRead{};
  for (std::size_t i{}; i < commands.size(); ++i)
  {
    PipeFds pipeFds{};
    const bool hasNext{i + 1 < commands.size()};
    if (hasNext && !PipeFds::create(pipeFds))
    {
      perror("pipe");
      return 1;
    }

    pid_t pid{fork()};
    if (pid == 0)
    {
      if (!bindPipelineInput(prevRead))
        _exit(127);

      const bool shouldPipeOutput{hasNext && !commands[i].stdoutRedir.enabled};
      if (!bindPipelineOutput(pipeFds, shouldPipeOutput))
        _exit(127);

      closeChildPipes(prevRead, pipeFds, hasNext);

      int rc{runner(commands[i], ExecMode::Child)};
      _exit(rc);
    }
    else if (pid > 0)
    {
      pids.push_back(pid);
      advanceParentPipe(prevRead, pipeFds, hasNext);
    }
    else
    {
      perror("fork");
      closeChildPipes(prevRead, pipeFds, hasNext);
      return 127;
    }
  }

  if (prevRead)
    prevRead.reset();

  int lastStatus{0};
  for (std::size_t i{}; i < pids.size(); ++i)
  {
    int status{};
    waitpid(pids[i], &status, 0);
    if (i + 1 == pids.size())
      lastStatus = status;
  }

  return WIFEXITED(lastStatus) ? WEXITSTATUS(lastStatus) : 127;
}
