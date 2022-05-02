#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <climits>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/user.h>

std::string trim(std::string s);
std::vector<std::string> split(std::string s, const std::string &delimiter);

int main() {
    std::ios::sync_with_stdio(false);
    std::string cmd;
    while (true) {
        std::cout << "# ";
        std::getline(std::cin, cmd);
        cmd = trim(cmd);
        std::vector<std::string> cmds = split(cmd, "|");
        std::vector<std::string> argv = split(cmd, " ");
        int argc = argv.size();
        if (argv[0] != "strace") {
            std::cout << "please enter strace command!\n";
            continue;
        }
        pid_t pid = fork();
        if (pid < 0) {
            std::cout << "fork error!\n";
            exit(255);
        }
        else if (pid == 0) {
            ptrace(PTRACE_TRACEME, 0, 0, 0);
            char *arg_ptrs[argc + 1];
            for (auto i = 0; i < argc; i++)
                arg_ptrs[i] = &argv[i][0];
            arg_ptrs[argc] = nullptr;
            execvp(argv[1].c_str(), arg_ptrs);
            exit(1);
        }
        else {
            waitpid(pid, 0, 0);
            ptrace(PTRACE_SYSCALL, pid, 0, 0);
            waitpid(pid, 0, 0);
            struct user_regs_struct regs;
            ptrace(PTRACE_GETREGS, pid, 0, &regs);
            long syscall = regs.orig_rax;
            fprintf(stderr, "%ld(%ld, %ld, %ld, %ld, %ld, %ld)\n", syscall, 
            (long)regs.rdi, (long)regs.rsi, (long)regs.rdx, (long)regs.r10,
            (long)regs.r8, (long)regs.r9);
            ptrace(PTRACE_SYSCALL, pid, 0, 0);
            waitpid(pid, 0, 0);
            ptrace(PTRACE_GETREGS, pid, 0, &regs);
//            fprintf(stderr, " = %lid\n", (long)regs.rax);
            break;
        }
    }
    return 0;
}

std::vector<std::string> split(std::string s, const std::string &delimiter)
{
    std::vector<std::string> res;
    size_t pos = 0;
    std::string token;
    s = trim(s);
    while ((pos = s.find(delimiter)) != std::string::npos) {
        if (pos != 0) {
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