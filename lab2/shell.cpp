#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <climits>
#include <unistd.h>
#include <sys/wait.h>

int execute(int argc, std::vector<std::string> argv);
std::vector<std::string> split(std::string s, const std::string &delimiter);

int main() {
  // 不同步 iostream 和 cstdio 的 buffer
  std::ios::sync_with_stdio(false);
  std::string cmd;
  while (true) {
    std::cout << "# ";
    std::getline(std::cin, cmd);
    std::vector<std::string> cmds = split(cmd, "|");
    int pipecmd = cmds.size();
    if (pipecmd == 0)
      continue;
    else if (pipecmd == 1) {
      std::vector<std::string> argv = split(cmds[0], " ");
      int argc = argv.size();
      if (execute(argc, argv) == 1)
        continue;
      pid_t pid = fork();
      char *arg_ptrs[argv.size() + 1];
      for (auto i = 0; i < argc; i++) 
        arg_ptrs[i] = &argv[i][0];
      arg_ptrs[argc] = nullptr;
      if (pid == 0) {
        execvp(argv[0].c_str(), arg_ptrs);
        exit(255);
      }
      int ret = wait(nullptr);
      if (ret < 0) {
        std::cout << "wait failed";
      }
    }
  }

}

int execute(int argc, std::vector<std::string> argv)
{
  if (argc == 0)
    return 1;

 if (argv[0] == "cd") {
    if (argc <= 1) {
      std::cout << "Insufficient arguments\n";
      return 1;
    }

    int ret = chdir(argv[1].c_str());
    if (ret < 0) {
      std::cout << "cd failed\n";
    }
    return 1;
  }
  if (argv[0] == "pwd") {
    std::string cwd;

    // 预先分配好空间
    cwd.resize(PATH_MAX);

    // std::string to char *: &s[0]（C++17 以上可以用 s.data()）
    // std::string 保证其内存是连续的
    const char *ret = getcwd(&cwd[0], PATH_MAX);
    if (ret == nullptr) {
      std::cout << "cwd failed\n";
    } else {
      std::cout << ret << "\n";
    }
    return 1;
  }

    // 设置环境变量
  if (argv[0] == "export") {
    for (auto i = ++argv.begin(); i != argv.end(); i++) {
      std::string key = *i;

      // std::string 默认为空
      std::string value;

      size_t pos;
      if ((pos = i->find('=')) != std::string::npos) {
        key = i->substr(0, pos);
        value = i->substr(pos + 1);
      }

      int ret = setenv(key.c_str(), value.c_str(), 1);
      if (ret < 0) {
        std::cout << "export failed\n";
      }
    }
    return 1;
  }

  if (argv[0] == "exit") {
    exit(0);
  }
  return 0;
}
std::vector<std::string> split(std::string s, const std::string &delimiter) {
  std::vector<std::string> res;
  size_t pos = 0;
  std::string token;
  while ((pos = s.find(delimiter)) != std::string::npos) {
    token = s.substr(0, pos);
    res.push_back(token);
    s = s.substr(pos + delimiter.length());
  }
  res.push_back(s);
  return res;
}


