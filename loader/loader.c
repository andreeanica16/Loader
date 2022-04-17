#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "exec_parser.h"

static so_exec_t *exec;
static int pageSize;
static struct sigaction old_action;
int fd_exec;

int my_ceil(double n)
{
	int int_n = (int)n;

	if (n == (double)int_n)
		return int_n;

	return int_n + 1;
}

/*
 * Initialize void *data from so_seg_t with a vector which associates
 * each page with 0 (if the page wasn't mapped until
 * now) or 1 (if the page is mapped in memory). The vector
 * initially contains 0 for each page(since no page is initially mapped).
 */
void initialize_segment_info_data(void)
{
	int i;
	int nr_pages;

	for (i = 0; i < exec->segments_no; i++) {
		/*
		 * Get the total number of pages the segment will have
		 */
		nr_pages = my_ceil((double)exec->segments[i].mem_size / pageSize);
		/*
		 * Initialize the vector with 0 for each page
		 */
		exec->segments[i].data = malloc(nr_pages * sizeof(int));
		if (exec->segments[i].data == NULL) {
			perror("malloc");
			return;
		}
		memset(exec->segments[i].data, 0, nr_pages * sizeof(int));
	}
}

/*
 * Find the segment that contains the address addr.
 * If no segment is found, return NULL.
 */
so_seg_t *get_corresponding_segment(char *addr)
{
	int i;
	int offset;

	for (i = 0; i < exec->segments_no; i++) {
		offset = addr - (char *)exec->segments[i].vaddr;

		if (offset >= 0 && offset < exec->segments[i].mem_size)
			return &exec->segments[i];
	}

	return NULL;
}

int get_page_number(so_seg_t *segm, char *addr)
{
	return (addr - (char *)segm->vaddr) / pageSize;
}

/*
 * Mark the page given by page number from segment segm
 * as mapped in memmory
 */
void mark_as_mapped(so_seg_t *segm, int page_number)
{
	int *mapped_pages = (int *)segm->data;

	mapped_pages[page_number] = 1;
}

/*
 * Check if the page given by page number from segment segm
 * is mapped in memmory
 */
int is_page_mapped(so_seg_t *segm, int page_number)
{
	int *mapped_pages = (int *)segm->data;

	return mapped_pages[page_number];
}

/*
 * Map the page given by page number from segment segm
 * in memmory
 */
void map_new_page(so_seg_t *segm, int page_number)
{
	char *page;
	ssize_t bytes_read;
	int bytes_to_read;
	int minimum_size;
	int rc;

	page = mmap(
		(void *)(segm->vaddr + page_number * pageSize),
		pageSize,
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS,
		0,
		0
	);

	if (page == MAP_FAILED) {
		perror("mmap");
		return;
	}

	memset(page, 0, pageSize);

	/*
	 * If we need to read data from the executable file.
	 */
	if (segm->file_size > pageSize * page_number) {
		/*
		 * Find out if we can read pageSize bytes from the executable
		 * file or less
		 */
		minimum_size = segm->file_size < pageSize * (page_number + 1) ?
			segm->file_size :
			pageSize * (page_number + 1);
		bytes_to_read = minimum_size - page_number * pageSize;

		/*
		 * Positin the file pointer on the position in the file from where
		 * we have to read data.
		 */
		rc = lseek(fd_exec, segm->offset + pageSize * page_number, SEEK_SET);
		if (rc == -1) {
			perror("lseek");
			return;
		}

		bytes_read = read(fd_exec, page, bytes_to_read);
		if (bytes_read < 0) {
			perror("read");
			return;
		}
	}

	/*
	 * Set the corresponding permisions for the page
	 */
	rc = mprotect(page, pageSize, segm->perm);
	if (rc == -1) {
		perror("mprotect");
		return;
	}

	mark_as_mapped(segm, page_number);
}

/*
 * Unmap all the mapped pages of a segment
 */
void unmap_segment(so_seg_t *segm)
{
	int nr_pages;
	int i, rc;

	nr_pages = my_ceil(segm->mem_size / pageSize);
	for (i = 0; i < nr_pages; i++) {
		if (is_page_mapped(segm, i)) {
			rc = munmap((void *)(segm->vaddr + i * pageSize), pageSize);
			if (rc == -1) {
				perror("munmap");
				return;
			}
		}
	}
}

static void segv_handler(int signum, siginfo_t *info, void *context)
{
	char *addr;
	int page_number;
	so_seg_t *segm;

	/*
	 * Check if the signal is SIGSEGV
	 */
	if (signum != SIGSEGV) {
		old_action.sa_sigaction(signum, info, context);
		return;
	}

	/*
	 * Obtain from siginfo_t the memory location
	 * which caused the page fault
	 */
	addr = (char *)info->si_addr;

	segm = get_corresponding_segment(addr);

	/*
	 * If it is not in a segment, it is an invalid
	 * memory access - the default page fault handler is running
	 */
	if (segm == NULL) {
		old_action.sa_sigaction(signum, info, context);
		return;
	}

	/*
	 * If the page fault is generated in an already mapped page, then an
	 * unauthorized memory access is attempted
	 */
	page_number = get_page_number(segm, addr);
	if (is_page_mapped(segm, page_number)) {
		old_action.sa_sigaction(signum, info, context);
		return;
	}

	/*
	 * If the page is in a segment, and it has not yet been mapped,
	 * then it is mapped to the corresponding address,
	 * with the permissions of that segment;
	 */
	map_new_page(segm, page_number);

}

int so_init_loader(void)
{
	/* TODO: initialize on-demand loader */
	struct sigaction action;
	int rc;

	memset(&action, 0, sizeof(action));
	pageSize = getpagesize();

	action.sa_sigaction = segv_handler;

	sigemptyset(&action.sa_mask);
	sigaddset(&action.sa_mask, SIGSEGV);

	action.sa_flags = SA_SIGINFO;

	rc = sigaction(SIGSEGV, &action, &old_action);
	if (rc == -1) {
		perror("invalid sigaction");
		return -1;
	}

	return -1;
}

int so_execute(char *path, char *argv[])
{
	int rc, i;

	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	fd_exec = open(path, O_RDONLY);
	if (fd_exec < 0) {
		perror("open");
		return -1;
	}

	initialize_segment_info_data();

	so_start_exec(exec, argv);

	rc = close(fd_exec);
	if (rc < 0) {
		perror("close");
		return -1;
	}

	/*
	 * Free memory
	 */
	for (i = 0; i < exec->segments_no; i++) {
		unmap_segment(&exec->segments[i]);
		free(exec->segments[i].data);
	}
	free(exec->segments);
	free(exec);

	return -1;
}
