# nyufile

## 项目介绍

`nyufile` 是用 C 编写的 FAT32 磁盘镜像分析与文件恢复工具。它直接读取镜像中的引导扇区、文件分配表（FAT）和根目录，支持：

- 显示文件系统的基本信息。
- 列出根目录中的文件和目录。
- 恢复连续分配的已删除文件，可通过 SHA-1 校验筛选候选文件。
- 根据 SHA-1 校验搜索并恢复非连续分配的已删除文件。

恢复操作会在镜像中恢复目录项并重建 FAT 链，文件仍保存在镜像内。

## 编译

需要 Linux/POSIX 环境、GCC、GNU Make 和 OpenSSL 开发库（头文件与 `libcrypto`）。Windows 用户可在 WSL 或 Linux 容器内编译。

在 Debian/Ubuntu 中可安装依赖：

```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev
```

从本仓库的 `labs` 目录开始编译：

```bash
cd nyufile
make
```

生成当前目录下的可执行文件 `nyufile`，中间目标文件位于 `build/`。makefile 使用 C99，并通过 `-lcrypto` 链接 OpenSSL。

清理编译产物：

```bash
make clean
```

## 使用

```text
./nyufile disk <options>
```

`disk` 是 FAT32 文件系统镜像路径。每次执行选择一种操作：

| 参数 | 用途 |
| --- | --- |
| `-i` | 显示 FAT 数量、每扇区字节数、每簇扇区数及保留扇区数。 |
| `-l` | 列出根目录条目及条目总数。 |
| `-r filename` | 恢复连续分配的已删除文件。 |
| `-r filename -s sha1` | 使用 SHA-1 校验后恢复连续分配的文件。 |
| `-R filename -s sha1` | 搜索可能非连续分配的文件，必须提供 SHA-1。 |

以目录内的 `fat32.disk` 为例：

```bash
./nyufile fat32.disk -i
./nyufile fat32.disk -l
```

恢复会直接修改镜像，先复制镜像，再对副本操作。下面的 `HELLO.TXT` 应替换为实际已删除文件的 FAT 短文件名：

```bash
cp fat32.disk recovery.disk
./nyufile recovery.disk -r HELLO.TXT
```

如需校验，将下面的 `SHA1` 替换为原文件内容的 40 位十六进制 SHA-1 值：

```bash
./nyufile recovery.disk -r HELLO.TXT -s SHA1
./nyufile recovery.disk -R HELLO.TXT -s SHA1
```

两条恢复命令是不同场景下的用法，按文件分配方式选择一条。若保留了原文件，可以事先使用 `sha1sum HELLO.TXT` 获取校验值。

成功时显示 `successfully recovered` 或 `successfully recovered with SHA-1`；找不到匹配文件时显示 `file not found`。未提供 SHA-1 且存在多个同名候选时，显示 `multiple candidates found`。

当前实现仅搜索根目录，使用 FAT 的 8.3 短文件名进行匹配，区分大小写；通常应使用大写名称，例如 `HELLO.TXT`。非连续恢复采用有限范围的穷举搜索：文件最多占用 5 个簇，起始簇需位于簇号 2–21 内，后续候选簇也从该范围内的空闲簇中选择。镜像必须可读写，即使执行 `-i` 或 `-l` 也会以读写方式打开。
