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
int fileLineCount(std::string filename);
pid_t pid_ctrlc;
std::string bash_history_path;

int main()
{
  int exit_status;
  signal(SIGINT, handler);
  // 不同步 iostream 和 cstdio 的 buffer
  std::ios::sync_with_stdio(false);
  std::string cmd;
  char buff[PATH_MAX];
  getcwd(buff, PATH_MAX);
  std::string cwd(buff);
  std::string temp = "/.bash_history";
  bash_history_path = cwd + temp;
  while (true)
  {
    pid_ctrlc = fork();
    if (pid_ctrlc == 0)
    {
      signal(SIGINT, handler);
      std::cout << "# ";
      if (!std::getline(std::cin, cmd)){
        std::cout << "\n";
        exit(0);
      }
      cmd = trim(cmd);
      std::vector<std::string> cmds = split(cmd, "|");
      int pipecmds = cmds.size();
      if (pipecmds != 0) {
      std::ofstream history_file(bash_history_path, std::ios::out | std::ios::app);
        if (history_file.is_open()) {
          if (pipecmds == 1) {
            std::vector<std::string> argv = split(cmds[0], " ");
            if (argv[0] != "exit" && argv[0][0] != '!')
              history_file << cmd << "\n";
          }
          else {
            history_file << cmd << "\n";
          }
        }
        else {
          std::cout << "history open error!\n";
          exit(255);
        }
        history_file.close();
      }
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
        while (wait(nullptr) > 0);
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
      if (exit_status == 0) {
        exit(0);
      }
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
    total_line = fileLineCount(bash_history_path);
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
    std::ifstream history_file(bash_history_path, std::ios::in);
    if (history_file.is_open()) {
      while (std::getline(history_file, read_line)) {
        current_line++;
        if (total_line - current_line < len) {
          out_line = "  " + std::to_string(current_line) + "  " + read_line;
          std::cout << out_line << "\n";
        }
      }
      exit(1);
    }
    else {
      std::cout << "history open error!\n";
      exit(255);
    }
    history_file.close();
    return 1;
  }
  if (argv[0] == "echo") {
    if (argc > 1) {
      if (argv[1] == "$SHELL") {
        char* path = getenv("SHELL");
        if (path == NULL)
          exit(255);
        else {
          std::cout << path << "\n";
          exit(1);
        }
      }
      if (argv[1] == "~root") {
        std::cout << "/root\n";
        exit(1);
      }
    }
  }
  if (argv[0][0] == '!') {
    if (argv[0] == "!!") {
      int total_line = fileLineCount(bash_history_path);
      int current_line = 0;
      std::string read_line;
      std::ifstream history_file(bash_history_path, std::ios::in);
      if (history_file.is_open()) {
        while (std::getline(history_file, read_line)) {
          current_line++;
          if (total_line == current_line) {
            pid_t pid = fork();
            if (pid == 0)
            {
              pid_t pid_outline = fork();
              if (pid_outline == 0) {
                std::cout << read_line << "\n";
              }
              while (wait(nullptr) > 0);
              if (pid_outline > 0) {
                std::vector<std::string> argv = split(read_line, " ");
                int argc = argv.size();
                if (builtinCommand(argc, argv) == 1)
                  exit(1);
                externalCommand(argc, argv);
                exit(255);
              }
            }
            while (wait(nullptr) > 0);
          }
        }
        exit(1);
      }
      else {
        std::cout << "history open error!\n";
        exit(255);
      }
      history_file.close();
      return 1;
    }
    else {
      int total_line = fileLineCount(bash_history_path);
      argv[0] = argv[0].substr(1);
      if (std::stoi(argv[0]) > total_line) {
        std::cout << "event not found!\n";
        exit(255);
      }
      int current_line = 0;
      std::string read_line;
      std::ifstream history_file(bash_history_path, std::ios::in);
      if (history_file.is_open()) {
        while (std::getline(history_file, read_line)) {
          current_line++;
          if (std::stoi(argv[0]) == current_line) {
            pid_t pid = fork();
            if (pid == 0)
            {
              pid_t pid_outline = fork();
              if (pid_outline == 0) {
                std::cout << read_line << "\n";
              }
              while (wait(nullptr) > 0);
              if (pid_outline > 0) {
                std::vector<std::string> argv = split(read_line, " ");
                int argc = argv.size();
                if (builtinCommand(argc, argv) == 1)
                  exit(1);
                externalCommand(argc, argv);
                exit(255);
              }
            }
            while (wait(nullptr) > 0);
          }
        }
        exit(1);
      }
      else {
        std::cout << "history open error!\n";
        exit(255);
      }
      history_file.close();
      return 1;
    }
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
      int fd = open(argv[orient + 1].c_str(), O_RDWR | O_CREAT, S_IRWXU);
      dup2(fd, STDOUT_FILENO);
      close(fd);
      argv.pop_back();
      argv.pop_back();
      argc = argc - 2;
    }
    else if (argv[orient] == "<")
    {
      int fd = open(argv[orient + 1].c_str(), O_RDWR, S_IRWXU);
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
      int fd = open(argv[orient + 1].c_str(), O_RDWR | O_APPEND | O_CREAT, S_IRWXU);
      dup2(fd, STDOUT_FILENO);
      close(fd);
      argv.pop_back();
      argv.pop_back();
      argc = argc - 2;
    }
  }
  if (argv[argc - 1] == "env" && argc >= 2) {
    std::string env_out = "";
    for (int i = argc - 2; i >= 0; i--) {
      if (argv[i].find('=') != std::string::npos) {
        if (i != 0)
          std::cout << argv[i] << "\n";
        if (i == 0) {
          std::cout << argv[i];
          fflush(stdout);
        }
        argv.erase(argv.begin() + i);
        argc--;
        if (i == 0) {
          std::cout << env_out << std::endl;
          char *arg_ptrs[argc + 1];
          for (auto i = 0; i < argc; i++)
            arg_ptrs[i] = &argv[i][0];
          arg_ptrs[argc] = nullptr;
          execvp(argv[0].c_str(), arg_ptrs);
          exit(255);
        }
      }
      else {
        std::cout << "No such command!\n";
        exit(255);
      }
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

int fileLineCount(std::string filename)
{
  int total_line = 0;
  std::string read_line;
    std::ifstream history_file(filename, std::ios::in);
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
    return total_line;
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