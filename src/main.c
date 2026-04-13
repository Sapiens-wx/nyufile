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
			case 'l':
				error|=args.opt!=-1;
				args.opt=opt;
				break;
			case 's':
				args.sha1=optarg;
				break;
			case 'r':
			case 'R':
				error|=args.opt!=-1;
				args.opt=opt;
				args.filename=optarg;
				break;
			default:
				error=1;
				break;
		}
	}
	error|=optind>=argc;
	error|=optind+1<argc; // cannot take any more arguments other than disk name
	error|=args.opt==-1;
	error|=args.opt=='R' && args.sha1==NULL;
	error|=args.filename!=NULL && args.filename[0]=='-'; // the argument cannot be a flag
	error|=args.sha1!=NULL && args.sha1[0]=='-'; // the argument cannot be a flag
	error|=args.opt=='i'&&args.sha1!=NULL; // -i flag cannot have -s flag
	error|=args.opt=='l'&&args.sha1!=NULL; // -l flag cannot have -s flag
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
				disk_recover_file_continuous(args.filename, args.sha1);
				break;
			case 'R':
				disk_recover_file(args.filename, args.sha1);
				break;
		}
		disk_destroy();
	}
	return 0;
}

