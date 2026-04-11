#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include "disk_struct.h"

#define CLUSTER_START_IDX 2

char* disk;
size_t disk_size;

void disk_init(const char* diskname) {
    // open the disk file
    int fd = open(diskname, O_RDONLY);
    if (fd == -1) {
        perror("error opening the disk file");
    }

    // get file size
    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("fstat");
    }
    disk_size = st.st_size;

    // mmap
    disk = mmap(NULL, disk_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
    }

    close(fd);
}

void disk_destroy(){
    munmap(disk, disk_size);
}

// =====Helper Functions=====
BootEntry* get_boot(){
    return (BootEntry*)disk;
}
void* get_cluster(int cluster){
    cluster-=CLUSTER_START_IDX; // cluster starts from 2
    BootEntry* boot=get_boot();
    int boot_size=(int)(boot->BPB_RsvdSecCnt)*(int)boot->BPB_BytsPerSec;
    int total_fat_size=(int)(boot->BPB_FATSz32)*(int)boot->BPB_BytsPerSec*(int)boot->BPB_NumFATs;
    int cluster_size=((int)boot->BPB_BytsPerSec)*(int)boot->BPB_SecPerClus;
    return (void*)(disk+(boot_size+total_fat_size+cluster*cluster_size));
}
void* get_fat(int fat_idx){
    BootEntry* boot=get_boot();
    int boot_size=(int)(boot->BPB_RsvdSecCnt)*(int)boot->BPB_BytsPerSec;
    int fat_size=(int)(boot->BPB_FATSz32)*(int)boot->BPB_BytsPerSec;
    return (void*)(disk+(boot_size+fat_idx*fat_size));
}
// is this cluster eof
int fat_eof(int cluster){
    return cluster>=0x0ffffff8;
}
// returns the cluster number linked by the given cluster
int fat(int cluster){
    cluster-=CLUSTER_START_IDX; // cluster starts from 2
    int32_t* table=(void*)get_fat(0);
    return (int)table[cluster];
}
int dir_is_unallocated(DirEntry* dir_entry){
    return dir_entry->DIR_Name[0]==0 || dir_entry->DIR_Name[0]==0xe5;
}
int dir_is_lfn(DirEntry* dir_entry){
    return dir_entry->DIR_Attr==DIR_ATTR_LFN;
}
int get_cluster_lh(unsigned short lo, unsigned short hi){
    return (int)((((unsigned int)hi)<<(8*sizeof(unsigned short)))|(unsigned int)lo);
}
void print_dir_name(DirEntry* dir){
    const char* name=(char*)dir->DIR_Name;
    for(int i=0;i<8 && name[i]!=' ';++i)
        printf("%c", name[i]);
    if(name[9]!=' '){ // has suffix
        printf(".%.*s", 3, name+8);
    }
    if((((int)dir->DIR_Attr)&DIR_ATTR_DIR)==DIR_ATTR_DIR){ // a directory
        printf("/");
    }
    printf(" ");
}
void print_dir_info(DirEntry* dir){
    int cluster=get_cluster_lh(dir->DIR_FstClusLO, dir->DIR_FstClusHI);
    if((((int)dir->DIR_Attr)&DIR_ATTR_DIR)==DIR_ATTR_DIR){ // a directory
        printf("(starting cluster = %d)", cluster);
    } else{ // regular file
        unsigned int file_size=dir->DIR_FileSize;
        printf("(size = %u", file_size);
        if(file_size==0)
            printf(")");
        else
            printf(", starting cluster = %d)", cluster);
    }
}
void print_dir(DirEntry* dir){
    print_dir_name(dir);
    print_dir_info(dir);
    printf("\n");
}
int print_dir_in_cluster(DirEntry* cluster){
    BootEntry* boot=get_boot();
    const int cluster_size=(int)boot->BPB_BytsPerSec*(int)boot->BPB_SecPerClus;
    const int max_entries=cluster_size/sizeof(DirEntry);
    int entries=0;
    for(int i=0;i<max_entries;++i){
        DirEntry* cur=cluster+i;
        if(dir_is_unallocated(cur) || dir_is_lfn(cur)){
        } else{
            print_dir(cur);
            ++entries;
        }
    }
    return entries;
}
// ==========================

void disk_info(){
    BootEntry* boot=(BootEntry*)disk;

    printf("Number of FATs = %d\n", (int)boot->BPB_NumFATs);
    printf("Number of bytes per sector = %d\n", (int)boot->BPB_BytsPerSec);
    printf("Number of sectors per cluster = %d\n", (int)boot->BPB_SecPerClus);
    printf("Number of reserved sectors = %d\n", (int)boot->BPB_RsvdSecCnt);
}

void disk_list_root_dir(){
    BootEntry* boot=(BootEntry*)disk;
    const int root_cluster=boot->BPB_RootClus;
    int total_entries=0;
    for(int cur_cluster=root_cluster;!fat_eof(cur_cluster);cur_cluster=fat(cur_cluster)){
        DirEntry* dir_cluster=(DirEntry*)get_cluster(cur_cluster);
        total_entries+=print_dir_in_cluster(dir_cluster);
    }
    printf("Total number of entries = %d\n", total_entries);
}