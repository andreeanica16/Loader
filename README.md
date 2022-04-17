
# Loader

## Introduction
Own implementation of an an executable / dynamic file loader in ELF format for Linux. The loader loads the executable file into memory page by page, using a demand paging mechanism - a page will only be loaded when it is needed. The loader only runs static executables - which are not linked to shared / dynamic libraries.

## Description

My implementation is based on a SIGSEGV signal processing routine, segv_handler. It looks for the address that produced SIGSEGV
in segments, finding the segment to which it belongs, with the help of the function
get_corresponding_segment. Then the function calculates which pages it belongs to
that address using the get_page_number function. Now all operations
which will be performed, will be at page level.
We need to check if the page has been mapped or not (to know if we are calling
default handler or map a new page in memory). To retain this
thing I used void * date from so_seg_t. This pointer will point
to a vector that will hold on to a position corresponding to a page
0 or 1, 0 if the page has not been mapped, 1 if the page has been mapped.

The initialization of this vector is done in the initialize_segment_info_data function,
before calling so_start_exec, because we know exactly how many pages each segment will take (using the mem_size field in so_seg_t). The vector will initially contains only 0 for each segment.

If the page has not yet been mapped, its mapping will be done accordingly
map_new_page. It is important to explain the verification
segm-> file_size> pageSize * page_number made before reading the data
from the file. If somehow the size of the segment in memory is larger than the size in the file, then we need to know if the page we want to
map contains information that is in the file or not. For this, if
segment size in file is smaller than current page location,
given the page number * pageSize, then it means that the current page does not
it contains absolutely no information in the file, so we won't have to read anything
of this, it is enough just to set and fill the newly mapped space with 0. 

After the program finishes running, we need to unmap the mapped pages,
using the unmap_segment function, which will only map previously mapped pages.

## How to compile the code?
In a terminal, run:
```
	make build
```





