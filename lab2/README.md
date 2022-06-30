# osh-lab2 实验报告

姓名 蓝俊玮 学号 PB20111689

本次实验我只选做了 shell 部分的选做题：

- 支持 History 历史命令持久化，新打开的 shell 能够使用之前保存的记录。将 .bash_history 文件存储在 shell 目录下，在程序中先获取当前的 cwd 然后加上 ".bash_history" 就可以得到 .bash_history 的文件路径，并且能够在接下来的操作中继续使用，避免了使用 cd 指令之后导致无法获取 .bash_history 的文件路径。

- 处理 Ctrl-D (EOF) 按键。直接对输入进行判断：`if (!std::getline(std::cin, cmd))` ，如果判断为真，说明遇到了 Ctrl-D，就可以退出 shell 程序。

- 支持 `echo ~root` 功能。输出 "/root" 。

- 支持 `echo $SHELL` 功能。$SHELL 在 bash 中会被替换，然后通过 echo 指令打印出来，使用 `getenv("SHELL")` 就可以获取 $SHELL 的值。

- 支持 `A=1 B=2 C=3 env` 功能。根据多次尝试，这条指令功能应该是将前面的变量和 env 的变量一起输出，多个参数从右边开始输出，例如本例就会输出：

  > C=3
  >
  > B=2
  >
  > A=1
  >
  > env...输出

