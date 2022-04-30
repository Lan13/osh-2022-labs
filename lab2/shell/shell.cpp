#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <climits>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>

int builtinCommand(int argc, std::vector<std::string> argv);
int externalCommand(int argc, std::vector<std::string> argv);
std::string trim(std::string s);
std::vector<std::string> split(std::string s, const std::string &delimiter);
void handler(int sig);
pid_t pid_ctrlc;

int main()
{
  int exit_status;
  signal(SIGINT, handler);
  // 不同步 iostream 和 cstdio 的 buffer
  std::ios::sync_with_stdio(false);
  std::string cmd;
  while (true)
  {
    pid_ctrlc = fork();
    if (pid_ctrlc == 0)
    {
      signal(SIGINT, handler);
      std::cout << "# ";
      std::getline(std::cin, cmd);
      cmd = trim(cmd);
      std::vector<std::string> cmds = split(cmd, "|");
      int pipecmds = cmds.size();
      if (pipecmds == 0)
        continue;
      else if (pipecmds == 1)
      {
        std::vector<std::string> argv = split(cmds[0], " ");
        int argc = argv.size();
        if (builtinCommand(argc, argv) == 1)
          continue;
        pid_t pid = fork();
        if (pid == 0)
        {
          externalCommand(argc, argv);
          exit(255);
        }
        int ret = wait(nullptr);
        if (ret < 0)
        {
          std::cout << "wait failed";
          exit(255);
        }
      }
      else if (pipecmds == 2)
      {
        int pipefds[2];
        if (pipe(pipefds) < 0)
        {
          std::cout << "pipe error!\n";
          exit(255);
        }
        pid_t pid = fork();
        if (pid < 0)
        {
          std::cout << "fork error!\n";
          exit(255);
        }
        if (pid == 0)
        {
          close(pipefds[0]);
          dup2(pipefds[1], STDOUT_FILENO);
          close(pipefds[1]);
          std::vector<std::string> argv = split(cmds[0], " ");
          int argc = argv.size();
          externalCommand(argc, argv);
          exit(255);
        }
        pid = fork();
        if (pid == 0)
        {
          close(pipefds[1]);
          dup2(pipefds[0], STDIN_FILENO);
          close(pipefds[0]);
          std::vector<std::string> argv = split(cmds[1], " ");
          int argc = argv.size();
          externalCommand(argc, argv);
          exit(255);
        }
        close(pipefds[0]);
        close(pipefds[1]);
        while (wait(nullptr) > 0);
      }
      else
      {
        int last_read_fd;
        for (int i = 0; i <= pipecmds - 1; i++)
        {
          int pipefds[2];
          if (i != pipecmds - 1)
          {
            if (pipe(pipefds) < 0)
            {
              std::cout << "pipe error!\n";
              exit(255);
            }
          }
          pid_t pid = fork();
          if (pid == 0)
          {
            if (i != pipecmds - 1)
            {
              close(pipefds[0]);
              dup2(pipefds[1], STDOUT_FILENO);
              close(pipefds[1]);
            }
            if (i != 0)
            {
              dup2(last_read_fd, STDIN_FILENO);
              close(last_read_fd);
            }
            std::vector<std::string> argv = split(cmds[i], " ");
            int argc = argv.size();
            externalCommand(argc, argv);
            exit(255);
          }
          if (i != 0)
            close(last_read_fd);
          if (i != pipecmds - 1)
          {
            last_read_fd = pipefds[0];
            close(pipefds[1]);
          }
        }
        while (wait(nullptr) > 0);
      }
    }
    else {
      waitpid(pid_ctrlc, &exit_status, 0);
      if (exit_status == 0)
        exit(0);
    }
  }
  return 0;
}

int builtinCommand(int argc, std::vector<std::string> argv)
{
  if (argc == 0)
    return 1;
  if (argv[0] == "cd")
  {
    if (argc <= 1)
    {
      std::cout << "Insufficient arguments\n";
      exit(255);
    }
    int ret = chdir(argv[1].c_str());
    if (ret < 0)
    {
      std::cout << "cd failed\n";
      exit(255);
    }
    return 1;
  }
  // 设置环境变量
  if (argv[0] == "export")
  {
    for (auto i = ++argv.begin(); i != argv.end(); i++)
    {
      std::string key = *i;
      // std::string 默认为空
      std::string value;
      size_t pos;
      if ((pos = i->find('=')) != std::string::npos)
      {
        key = i->substr(0, pos);
        value = i->substr(pos + 1);
      }
      int ret = setenv(key.c_str(), value.c_str(), 1);
      if (ret < 0)
      {
        std::cout << "export failed\n";
        exit(255);
      }
    }
    return 1;
  }
  if (argv[0] == "exit") {
    exit(0);
  }
  if (argv[0] == "history") {
    int total_line = 0, current_line = 0;
    std::string read_line, out_line;
    std::ifstream history_file(".bash_history", std::ios::in);
    if (history_file.is_open()) {
      while (std::getline(history_file, read_line)) {
        total_line++;
      }
    }
    else {
      std::cout << "history open error!\n";
      exit(255);
    }
    history_file.close();
    int len = total_line;
    if (argc > 2) {
      std::cout << "history argv error: too many arguments!\n";
      exit(255);
    }
    else if (argc == 2) {
      len = std::stoi(argv[1]);
      if (len <= 0) {
        std::cout << "history argv error: cannot be a negative!\n";
        exit(255);
      }
    }
    std::ifstream history_file2(".bash_history", std::ios::in);
    if (history_file2.is_open()) {
      while (std::getline(history_file2, read_line)) {
        if (total_line - current_line <= len) {
          out_line = "  " + std::to_string(current_line) + "  " + read_line;
          std::cout << out_line << "\n";
        }
        current_line++;
      }
    }
    else {
      std::cout << "history open error!\n";
      exit(255);
    }
    history_file2.close();
  }
  return 0;
}

int externalCommand(int argc, std::vector<std::string> argv)
{
  int orient = -1;
  for (int i = 0; i < argc; i++)
  {
    if (argv[i] == ">" || argv[i] == ">>" || argv[i] == "<")
    {
      orient = i;
      break;
    }
  }
  if (orient >= 0)
  {
    if (argv[orient] == ">")
    {
      int fd = open(argv[orient + 1].c_str(), O_RDWR);
      if (fd == -1)
      {
        std::cout << "Error: No such file or directory";
        exit(255);
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
      argv.pop_back();
      argv.pop_back();
      argc = argc - 2;
    }
    else if (argv[orient] == "<")
    {
      int fd = open(argv[orient + 1].c_str(), O_RDWR);
      if (fd == -1)
      {
        std::cout << "Error: No such file or directory";
        exit(255);
      }
      dup2(fd, STDIN_FILENO);
      close(fd);
      argv.pop_back();
      argv.pop_back();
      argc = argc - 2;
    }
    else if (argv[orient] == ">>")
    {
      int fd = open(argv[orient + 1].c_str(), O_RDWR | O_APPEND);
      if (fd == -1)
      {
        std::cout << "Error: No such file or directory";
        exit(255);
      }
      dup2(fd, STDOUT_FILENO);
      close(fd);
      argv.pop_back();
      argv.pop_back();
      argc = argc - 2;
    }
  }
  char *arg_ptrs[argc + 1];
  for (auto i = 0; i < argc; i++)
    arg_ptrs[i] = &argv[i][0];
  arg_ptrs[argc] = nullptr;
  execvp(argv[0].c_str(), arg_ptrs);
  exit(255);
}

void handler(int sig)
{
  if (pid_ctrlc == 0) {
    std::cout << "\n";
    exit(1);
  }
}

std::vector<std::string> split(std::string s, const std::string &delimiter)
{
  std::vector<std::string> res;
  size_t pos = 0;
  std::string token;
  s = trim(s);
  while ((pos = s.find(delimiter)) != std::string::npos)
  {
    if (pos != 0)
    {
      token = trim(s.substr(0, pos));
      res.push_back(token);
    }
    s = s.substr(pos + delimiter.length());
  }
  res.push_back(s);
  return res;
}

std::string trim(std::string s)
{
  const char *t = " ";
  s = s.erase(0, s.find_first_not_of(t));
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}