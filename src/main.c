#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include "disk.h"

extern char *optarg;
extern int optind;

typedef struct Args{
	const char* sha1;
	const char* filename;
	const char* diskname;
	int opt;
}Args;

static void print_help_msg(){
	printf("Usage: ./nyufile disk <options>\n  -i                     Print the file system information.\n  -l                     List the root directory.\n  -r filename [-s sha1]  Recover a contiguous file.\n  -R filename -s sha1    Recover a possibly non-contiguous file.\n");
}

int main(int argc, char* argv[]){
	Args args={NULL, NULL, NULL, -1};
	int error=0;
	int opt;
	while(-1!=(opt=getopt(argc, argv, "ils:r:R:"))){
		switch(opt){
			case 'i':
				args.opt='i';
				break;
			case 'l':
				args.opt='l';
				break;
			case 's':
				args.sha1=optarg;
				break;
			case 'r':
				args.opt='r';
				args.filename=optarg;
				break;
			case 'R':
				args.opt='R';
				args.filename=optarg;
				break;
			default:
				error=1;
				break;
		}
	}
	if(optind>=argc)
		error=1;
	if(error){
		print_help_msg();
	} else{
		args.diskname=argv[optind];
		disk_init(args.diskname);
		switch(args.opt){
			case 'i':
				disk_info();
				break;
			case 'l':
				disk_list_root_dir();
				break;
			case 'r':
				disk_recover_file(args.filename);
				break;
		}
		disk_destroy();
	}
	return 0;
}

