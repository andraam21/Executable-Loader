/*
 * Loader Implementation
 *
 * 2022, Operating Systems
 */

#include "loader.h"
#include "exec_parser.h"
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

#define PAGESIZE 4096

static so_exec_t *exec;
static struct sigaction act;
char *executable;
int **flags;

// Verify the edges of a segment
int verifyedges(int pgnb, int nb, int sgmnb){

	if(pgnb == nb && sgmnb % PAGESIZE != 0){
		return 1;
	}
	return 0;
}


static void segv_handler(int signum, siginfo_t *info, void *context)
{
	// Where did the segfault appeared
	uintptr_t val = (uintptr_t)(void *)info->si_addr;
	
	if(signum != SIGSEGV){
		act.sa_sigaction(signum, info, context);
		return;
	}	

	for(int i = 0; i < exec->segments_no; i++){
		
		// Getting the data for every segment
		so_seg_t* segment = &exec->segments[i];

		int pagenumber = val - segment->vaddr;
		pagenumber >>=12;

		int numofmem = segment->mem_size;
		numofmem >>= 12;
		if(segment->mem_size % PAGESIZE != 0){
			numofmem++;
		}

		int numoffil = segment->file_size;
		numoffil >>= 12;
		if(segment->file_size % PAGESIZE != 0){
			numoffil++;
		}
		
		if(val >= segment->vaddr){
			if(val <= segment->vaddr + segment->mem_size){

				// Test if the segment has been mapped
				if(flags[i][pagenumber] == 0){
					flags[i][pagenumber] = 1;

					int allocate_mem = PAGESIZE;					
					int changemem;
					changemem = verifyedges(pagenumber, numofmem - 1, segment->mem_size);
					if(changemem == 1){
						allocate_mem = segment->mem_size % PAGESIZE;
					}

					// Map the segment
					void* start = (void*)segment->vaddr;
					void* wheretoplace = start + pagenumber * PAGESIZE;
					char* map = mmap(wheretoplace, allocate_mem, PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);					
					if(map == MAP_FAILED){
						return;
					}				
				
					int allocate_cpy = PAGESIZE;
					int changecpy;
					changecpy = verifyedges(pagenumber, numoffil - 1, segment->file_size);
					if(changecpy == 1){
						allocate_cpy = segment->file_size % PAGESIZE;
					}
					else if(pagenumber >= numoffil) {
						allocate_cpy = 0;
					}

					// Copy the files in the mapped area
					void* whattocopy = executable + segment->offset + pagenumber * PAGESIZE;
					memcpy(map, whattocopy, allocate_cpy);

					// Set the permissions
					int perm = mprotect(map, allocate_mem, segment->perm);
					if(perm == -1){
						return;
					}
					return;

				} else {
					act.sa_sigaction(signum, info, context);
					return;
				}

			}
		}
		
	}
	act.sa_sigaction(signum, info, context);
	return;
}

int so_init_loader(void)
{
	int rc;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = segv_handler;
	sa.sa_flags = SA_SIGINFO;
	rc = sigaction(SIGSEGV, &sa, NULL);
	if (rc < 0) {
		perror("sigaction");
		return -1;
	}
	return 0;

}

int so_execute(char *path, char *argv[])
{
	exec = so_parse_exec(path);
	if (!exec){
		return -1;
	}

	// Save the page mapping state
	flags = calloc(exec->segments_no, sizeof(int*));
	for(int i = 0; i < exec->segments_no; i++){
		so_seg_t *aux = &exec->segments[i];
		flags[i] = calloc(aux->mem_size / PAGESIZE, sizeof(int));
	}

	// Open the file in order to start the mapping
	FILE *file = fopen(path, "rw");	
	if(file == NULL){
		return -1;
	}

	// Get the size of the file
	fseek(file, 0L, SEEK_END);
	long sizeoffile = ftell(file);
	fseek(file, 0L, SEEK_SET);

	// Start mapping in the executable
	executable = mmap(0, sizeoffile, PROT_READ, MAP_PRIVATE, 3, 0);
	if(executable == MAP_FAILED){
		return -1;
	}

	so_start_exec(exec, argv);

	fclose(file);

	return 0;
}

