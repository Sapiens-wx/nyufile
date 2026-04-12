#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <openssl/sha.h>
#include "disk_struct.h"

#define CLUSTER_START_IDX 2

char* disk;
size_t disk_size;

void disk_init(const char* diskname) {
    // open the disk file
    int fd = open(diskname, O_RDWR);
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
    disk = mmap(NULL, disk_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (disk == MAP_FAILED) {
        perror("mmap");
    }

    close(fd);
}

void disk_destroy(){
    //msync(disk, disk_size, MS_SYNC);
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
// gets the [fat_idx]th fat (address of the table)
void* get_fat(int fat_idx){
    BootEntry* boot=get_boot();
    int boot_size=(int)(boot->BPB_RsvdSecCnt)*(int)boot->BPB_BytsPerSec;
    int fat_size=(int)(boot->BPB_FATSz32)*(int)boot->BPB_BytsPerSec;
    return (void*)(disk+(boot_size+fat_idx*fat_size));
}
// is this cluster eof
int fat_eof(int cluster){
    return cluster>=FAT_EOF;
}
// returns the cluster number linked by the given cluster
int fat(int cluster){
    int32_t* table=(void*)get_fat(0);
    return (int)table[cluster];
}
// links a cluster with another cluster in the fat
void set_fat(int cluster, int value){
    int num_fat=get_boot()->BPB_NumFATs;
    for(int i=0;i<num_fat;++i){
        int32_t* table=get_fat(i);
        table[cluster]=value;
    }
}
void disk_print_fat(int start, int len){
    BootEntry* boot=get_boot();
    int fat_size=(int)boot->BPB_FATSz32*(int)boot->BPB_BytsPerSec;
    int num_clusters=fat_size/4;
    printf("num clusters: %d\n", num_clusters);
    for(int i=start;i<start+len && i<num_clusters;++i){
        printf("%3d |%4d\n", i, fat(i));
    }
}
int dir_is_unallocated(DirEntry* dir_entry){
    return dir_entry->DIR_Name[0]==0 || dir_entry->DIR_Name[0]==0xe5;
}
int dir_is_lfn(DirEntry* dir_entry){
    return dir_entry->DIR_Attr==DIR_ATTR_LFN;
}
int get_cluster_lh(DirEntry* dir){
    unsigned short lo=dir->DIR_FstClusLO, hi=dir->DIR_FstClusHI;
    return (int)((((unsigned int)hi)<<(8*sizeof(unsigned short)))|(unsigned int)lo);
}
void print_dir_name(DirEntry* dir){
    const char* name=(char*)dir->DIR_Name;
    for(int i=0;i<8 && name[i]!=' ';++i)
        printf("%c", name[i]);
    if(name[8]!=' '){
        printf(".");
        for(int i=8;i<11 && name[i]!=' ';++i)
            printf("%c", name[i]);
    }
    if((((int)dir->DIR_Attr)&DIR_ATTR_DIR)==DIR_ATTR_DIR){ // a directory
        printf("/");
    }
    printf(" ");
}
void print_dir_info(DirEntry* dir){
    int cluster=get_cluster_lh(dir);
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
// -----recover files-----

int is_deleted_dir_match_filename(DirEntry* dir, const char* filename){
    const char* dir_name=(const char*)&(dir->DIR_Name[1]);
    char actual_filename[11];
    const char* filename_it=filename;
    for(int i=0;i<11;){
        if(*filename_it=='.'){
            while(i<8)
                actual_filename[i++]=' ';
            ++filename_it;
        } else{
            actual_filename[i++]=*filename_it;
            if(*filename_it)
                ++filename_it;
        }
    }
    return memcmp(dir_name, actual_filename+1, 10)==0;
}

// finds all deleted files that match the given file name
void find_deleted_dir_internal(DirEntry* cluster, const char* filename, DirEntry** out_deleted_dirs, int* out_deleted_dirs_len, int out_deleted_dirs_cap){
    BootEntry* boot=get_boot();
    const int cluster_size=(int)boot->BPB_BytsPerSec*(int)boot->BPB_SecPerClus;
    const int max_entries=cluster_size/sizeof(DirEntry);
    for(int i=0;i<max_entries;++i){
        DirEntry* cur=cluster+i;
        if(!dir_is_lfn(cur) && dir_is_unallocated(cur)){
            if(is_deleted_dir_match_filename(cur, filename)){
                if(*out_deleted_dirs_len>=out_deleted_dirs_cap)
                    perror("find_deleted_dir: out_deleted_dirs out of space");
                out_deleted_dirs[(*out_deleted_dirs_len)++]=cur;
            }
        }
    }
}
// finds all matched deleted files in the root directory
void find_deleted_dir(const char* filename, DirEntry** out_deleted_dirs, int* out_deleted_dirs_len, int out_deleted_dirs_cap){
    BootEntry* boot=(BootEntry*)disk;
    const int root_cluster=boot->BPB_RootClus;
    // traverse through the root dir (may have multiple clusters)
    for(int cur_cluster=root_cluster;!fat_eof(cur_cluster);cur_cluster=fat(cur_cluster)){
        DirEntry* dir_cluster=(DirEntry*)get_cluster(cur_cluster);
        // find all deleted files that matches the filename
        find_deleted_dir_internal(dir_cluster, filename, out_deleted_dirs, out_deleted_dirs_len, out_deleted_dirs_cap);
    }
}

void recover_dir_continuous(DirEntry* dir, const char* filename){
    // recover file name
    dir->DIR_Name[0]=filename[0];
    // recover clusters (link them together)
    BootEntry* boot=get_boot();
    int cluster_size=(int)boot->BPB_BytsPerSec*(int)boot->BPB_SecPerClus;
    int num_clusters=((int)dir->DIR_FileSize+cluster_size-1)/cluster_size;
    if(num_clusters>0){
        int cur_cluster=get_cluster_lh(dir);
        for(int i=1;i<num_clusters;++i){
            int next_cluster=cur_cluster+1;
            set_fat(cur_cluster, next_cluster);
            cur_cluster=next_cluster;
        }
        set_fat(cur_cluster, FAT_EOF);
    }
}
// -----sha1-----
void print_hex(const unsigned char* bin, int len){
    for(int i=0;i<len;++i){
        printf("%02x", bin[i]);
    }
}
// if returns NULL, then the dir is empty
// assumes that the clusters are contiguous
void* read_dir_continuous(DirEntry* dir){
    BootEntry* boot=get_boot();
    long long file_size=dir->DIR_FileSize;
    int cluster_size=(int)boot->BPB_BytsPerSec*(int)boot->BPB_SecPerClus;
    int num_clusters=((int)file_size+cluster_size-1)/cluster_size;
    if(num_clusters==0)
        return NULL;
    char* d=(char*)malloc((size_t)file_size);
    int cur_cluster=get_cluster_lh(dir);
    for(int i=0;i<num_clusters;++i){
        memcpy(d+i*cluster_size, get_cluster(cur_cluster), file_size>cluster_size?(size_t)cluster_size:(size_t)file_size);
        cur_cluster++;
        file_size-=cluster_size;
    }
    return d;
}
void sha1_dir_continuous(DirEntry* dir, unsigned char* sha1){
    unsigned char* d=(unsigned char*)read_dir_continuous(dir);
    SHA1(d, dir->DIR_FileSize, sha1);
    free(d);
}
int hex_char_to_val(char c) {
    if ('0' <= c && c <= '9') return c - '0';
    if ('a' <= c && c <= 'f') return c - 'a' + 10;
    if ('A' <= c && c <= 'F') return c - 'A' + 10;
    return -1;
}

int sha1_hex_to_bin(const char *hex, unsigned char *out) {
    if (strlen(hex) != 40) return -1;

    for (int i = 0; i < 20; i++) {
        int hi = hex_char_to_val(hex[2 * i]);
        int lo = hex_char_to_val(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (hi << 4) | lo;
    }
    return 0;
}
// returns true if equal
int sha1_cmp_continuous(const unsigned char* sha1, DirEntry* dir){
    unsigned char dir_sha1[SHA_DIGEST_LENGTH];
    unsigned char target_sha1[SHA_DIGEST_LENGTH];
    sha1_dir_continuous(dir, dir_sha1);
    sha1_hex_to_bin((const char*)sha1, target_sha1);
    return memcmp(dir_sha1, target_sha1, SHA_DIGEST_LENGTH)==0;
}

// recovers a contiguously allocated DirEntry with sha1 value
DirEntry* recover_dir_sha1_continuous(const char* filename, DirEntry** dirs, int dirs_len, const char* sha1){
    DirEntry* target_dir=NULL;
    for(int i=0;i<dirs_len;++i){
        if(sha1_cmp_continuous((unsigned char*)sha1, dirs[i])){
            target_dir=dirs[i];
            break;
        }
    }
    if(target_dir==NULL){
        return NULL;
    }
    recover_dir_continuous(target_dir, filename);
    return target_dir;
}

int factorial(int n){
    if(n<=1) return 1;
    return n*factorial(n-1);
}
#define SEARCH_CLUSTER_END_INDEX 22
#define PERMUTE_MAX_COUNT 5
typedef struct Permute_t{
    char sha1_bin[SHA_DIGEST_LENGTH];
    int hash[SEARCH_CLUSTER_END_INDEX]; // if hash[cluster]==0, then this cluster can be chosen
    char* data; // reads data to this continuous memory
    int permutation[5];
    long long file_size;
    int cluster_size;
    int num_clusters; // how many clusters in total we have to find (max PERMUTE_MAX_COUNT)
} Permute_t;

int permute_dir_internal(Permute_t* info, int permute_idx){
    if(permute_idx==info->num_clusters){
        // done permuting. do the thing
        unsigned char dir_sha1[SHA_DIGEST_LENGTH];
        SHA1((const unsigned char*)info->data, info->file_size, dir_sha1);
        if(memcmp(dir_sha1, info->sha1_bin, SHA_DIGEST_LENGTH)==0){
            return 1;
        }
        return 0;
    }
    for(int i=CLUSTER_START_IDX;i<SEARCH_CLUSTER_END_INDEX;++i){
        info->hash[i]=fat(i);
        if(info->hash[i]==0){
            info->hash[i]=1;
            info->permutation[permute_idx]=i;
            long long rest_file_size=info->file_size-permute_idx*info->cluster_size;
            memcpy(info->data+permute_idx*info->cluster_size, get_cluster(i), rest_file_size>info->cluster_size?info->cluster_size:rest_file_size);
            if(permute_dir_internal(info, permute_idx+1))
                return 1;
        }
    }
    return 0;
}
// permute all possible cluster combinations for [dir] and returns the correct permutation
// returns 0 if fails. Otherwise, succeeds.
int permute_dir(DirEntry* dir, const char* sha1, Permute_t* out_permute_info){
    BootEntry* boot=get_boot();
    long long file_size=dir->DIR_FileSize;
    int cluster_size=(int)boot->BPB_BytsPerSec*(int)boot->BPB_SecPerClus;
    int num_clusters=((int)file_size+cluster_size-1)/cluster_size;

    // init permute info
    sha1_hex_to_bin(sha1, (unsigned char*)out_permute_info->sha1_bin);
    out_permute_info->data=(char*)malloc((size_t)file_size);
    out_permute_info->file_size=file_size;
    out_permute_info->cluster_size=cluster_size;
    out_permute_info->num_clusters=num_clusters;

    out_permute_info->permutation[0]=get_cluster_lh(dir);
    out_permute_info->hash[out_permute_info->permutation[0]]=1;
    return permute_dir_internal(out_permute_info, 1);
}
DirEntry* recover_dir_sha1(const char* filename, DirEntry** dirs, int dirs_len, const char* sha1){
    Permute_t permute_info;
    DirEntry* deleted_ent=NULL;
    for(int i=0;i<dirs_len;++i){
        DirEntry* dir=dirs[i];
        if(permute_dir(dir, sha1, &permute_info)){
            deleted_ent=dir;
            break;
        }
    }
    if(deleted_ent==NULL)
        return NULL;
    // recover file name
    deleted_ent->DIR_Name[0]=filename[0];
    // recover clusters (link them together)
    if(permute_info.num_clusters>0){
        int cur_cluster=permute_info.permutation[0];
        for(int i=1;i<permute_info.num_clusters;++i){
            int next_cluster=permute_info.permutation[i];
            set_fat(cur_cluster, next_cluster);
            cur_cluster=next_cluster;
        }
        set_fat(cur_cluster, FAT_EOF);
    }
    return deleted_ent;
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
    // traverse through the root dir (may have multiple clusters)
    for(int cur_cluster=root_cluster;!fat_eof(cur_cluster);cur_cluster=fat(cur_cluster)){
        DirEntry* dir_cluster=(DirEntry*)get_cluster(cur_cluster);
        total_entries+=print_dir_in_cluster(dir_cluster);
    }
    printf("Total number of entries = %d\n", total_entries);
    disk_print_fat(2, 10);
}

void disk_recover_file_continuous(const char* filename, const char* sha1){
    #define DELETED_DIRS_LEN 128
    DirEntry* deleted_dirs[DELETED_DIRS_LEN];
    int deleted_dirs_len=0;
    find_deleted_dir(filename, deleted_dirs, &deleted_dirs_len, DELETED_DIRS_LEN);
    if(deleted_dirs_len==0){
        printf("%s: file not found\n", filename);
    } else if(deleted_dirs_len>1){
        if(sha1==NULL)
            printf("%s: multiple candidates found\n", filename);
        else{ // recover the file based on sha1 value
            DirEntry* recovered_dir = recover_dir_sha1_continuous(filename, deleted_dirs, deleted_dirs_len, sha1);
            if(recovered_dir==NULL)
                printf("%s: file not found\n", filename);
            else
                printf("%s: successfully recovered with SHA-1\n", filename);
        }
    } else{
        // TODO: if sha1 is provided, compare the sha1 value.
        DirEntry* deleted_ent=deleted_dirs[0];
        if(sha1!=NULL && !sha1_cmp_continuous((const unsigned char*)sha1, deleted_ent))
            printf("%s: file not found\n", filename);
        else{
            recover_dir_continuous(deleted_ent, filename);
            if(sha1!=NULL)
                printf("%s: successfully recovered with SHA-1\n", filename);
            else
                printf("%s: successfully recovered\n", filename);
        }
    }
    #undef DELETED_DIRS_LEN
}

void disk_recover_file(const char* filename, const char* sha1){
    #define DELETED_DIRS_LEN 128
    DirEntry* deleted_dirs[DELETED_DIRS_LEN];
    int deleted_dirs_len=0;
    find_deleted_dir(filename, deleted_dirs, &deleted_dirs_len, DELETED_DIRS_LEN);
    if(deleted_dirs_len==0){
        printf("%s: file not found\n", filename);
    } else{
        DirEntry* recovered_dir = recover_dir_sha1(filename, deleted_dirs, deleted_dirs_len, sha1);
        if(recovered_dir==NULL)
            printf("%s: file not found\n", filename);
        else
            printf("%s: successfully recovered with SHA-1\n", filename);
    }
    #undef DELETED_DIRS_LEN
}