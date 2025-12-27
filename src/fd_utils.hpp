#pragma once

#include <unistd.h>

class UniqueFd
{
public:
  UniqueFd() = default;
  explicit UniqueFd(int fd)
      : fd{fd}
  {
  }

  ~UniqueFd()
  {
    reset();
  }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;

  UniqueFd(UniqueFd &&other) noexcept
      : fd{other.fd}
  {
    other.fd = -1;
  }

  UniqueFd &operator=(UniqueFd &&other) noexcept
  {
    if (this != &other)
    {
      reset();
      fd = other.fd;
      other.fd = -1;
    }
    return *this;
  }

  int get() const
  {
    return fd;
  }

  explicit operator bool() const
  {
    return fd >= 0;
  }

  int release()
  {
    int value{fd};
    fd = -1;
    return value;
  }

  void reset(int newFd = -1)
  {
    if (fd >= 0)
      ::close(fd);
    fd = newFd;
  }

private:
  int fd{-1};
};

struct PipeFds
{
  UniqueFd read{};
  UniqueFd write{};

  static bool create(PipeFds &pipeFds)
  {
    int fds[2]{-1, -1};
    if (::pipe(fds) != 0)
      return false;
    pipeFds.read.reset(fds[0]);
    pipeFds.write.reset(fds[1]);
    return true;
  }
};
