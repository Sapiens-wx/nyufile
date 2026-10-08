# nyufile

## Overview

`nyufile` is a FAT32 disk image analysis and file recovery tool written in C. It directly reads the boot sector, file allocation tables (FATs), and root directory of an image. It supports:

- Displaying basic file system information.
- Listing files and directories in the root directory.
- Recovering deleted files stored in contiguous clusters, with optional SHA-1 verification to select a candidate.
- Searching for and recovering deleted files stored in non-contiguous clusters using SHA-1 verification.

Recovery restores the directory entry and rebuilds the FAT chain inside the image. The recovered file remains in the image.

## Building

Requires a Linux/POSIX environment, GCC, GNU Make, and the OpenSSL development libraries (headers and `libcrypto`). Windows users can build in WSL or a Linux container.

On Debian/Ubuntu, install the dependencies with:

```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev
```

Starting from the parent `labs` directory:

```bash
cd nyufile
make
```

This creates the `nyufile` executable in the current directory and intermediate object files in `build/`. The makefile uses C99 and links OpenSSL with `-lcrypto`.

To remove build artifacts:

```bash
make clean
```

## Usage

```text
./nyufile disk <options>
```

`disk` is the path to a FAT32 file system image. Select one operation per invocation:

| Option | Description |
| --- | --- |
| `-i` | Display the number of FATs, bytes per sector, sectors per cluster, and reserved sectors. |
| `-l` | List root directory entries and the total entry count. |
| `-r filename` | Recover a deleted file stored in contiguous clusters. |
| `-r filename -s sha1` | Recover a contiguous file after verifying its SHA-1 hash. |
| `-R filename -s sha1` | Search for a possibly non-contiguous file. SHA-1 is required. |

For example, inspect the `fat32.disk` image in this directory:

```bash
./nyufile fat32.disk -i
./nyufile fat32.disk -l
```

Recovery modifies the image directly. Copy the image first and work on the copy. Replace `HELLO.TXT` below with the FAT short filename of the deleted file:

```bash
cp fat32.disk recovery.disk
./nyufile recovery.disk -r HELLO.TXT
```

For verification, replace `SHA1` below with the 40-digit hexadecimal SHA-1 hash of the original file contents:

```bash
./nyufile recovery.disk -r HELLO.TXT -s SHA1
./nyufile recovery.disk -R HELLO.TXT -s SHA1
```

These are alternative recovery commands; choose one based on the file's allocation pattern. If you have a copy of the original file, obtain its hash beforehand with `sha1sum HELLO.TXT`.

Successful recovery prints `successfully recovered` or `successfully recovered with SHA-1`. If no matching file is found, the program prints `file not found`. If multiple candidates exist and no SHA-1 hash is supplied, it prints `multiple candidates found`.

The current implementation searches only the root directory and matches FAT 8.3 short filenames case-sensitively. Uppercase names, such as `HELLO.TXT`, should normally be used. Non-contiguous recovery uses a bounded exhaustive search: the file must occupy at most 5 clusters, its starting cluster must be in the range 2–21, and subsequent candidate clusters are selected from free clusters in that same range. The image must be readable and writable, even for `-i` and `-l`, because it is opened in read/write mode.
