#ifndef FILECACHE_H
#define FILECACHE_H

void util_close_file_cached(FILE *file);
FILE *util_open_file_cached(char *location);


#endif