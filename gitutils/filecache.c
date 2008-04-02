#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "filecache.h"

struct file_cache_node {
    FILE *file;
    char *location;
    unsigned int reference_count;
    struct file_cache_node *prev;
    struct file_cache_node *next;
};

static struct file_cache_node *file_list = NULL;

/////////////////////////////////////////////////////////////////////
// This is just a simple linked list implementation. Go read your  //
// favorite data structures reference if this makes no sense.      //
/////////////////////////////////////////////////////////////////////

void util_close_file_cached(FILE *file)
{
    struct file_cache_node *cur_node;
    int first = 1;
    
    if(file_list != NULL) {
        cur_node = file_list;
        
        do {
            if(!first)
                cur_node = cur_node->next;
            else
                first = 0;
            
            if(file == cur_node->file) {
                cur_node->reference_count--;
                if(cur_node->reference_count == 0) {
                    // time to close the file and remove the node
                    //printf("=== file %s closed\n", cur_node->location);
                    fclose(cur_node->file);
                    free(cur_node->location);
                    if(cur_node != file_list)
                        cur_node->prev->next = cur_node->next;
                    if(cur_node->next != NULL)
                        cur_node->next->prev = cur_node->prev;
                    if(cur_node == file_list)
                        file_list = NULL;
                    free(cur_node);
                }
                return;
            }
        } while(cur_node->next != NULL);
    }
}

FILE *util_open_file_cached(char *location)
{
    struct file_cache_node *cur_node;
    int first = 1;
    
    if(file_list == NULL) {
        if(!(file_list = (struct file_cache_node *) malloc(sizeof(struct file_cache_node))))
            return NULL;
        file_list->prev = NULL;
        file_list->next = NULL;
        if(!(file_list->file = fopen(location, "rb"))) {
            free(file_list);
            return NULL;
        }
        if(!(file_list->location = (char *) malloc(strlen(location) + 1))) {
            fclose(file_list->file);
            free(file_list);
            return NULL;
        }
        // copy filename with \0 at the end
        memcpy(file_list->location, location, strlen(location) + 1);
        file_list->reference_count = 1;
        return file_list->file;
    } else {
        // first find if the file is already open
        cur_node = file_list;
        do {
            if(!first)
                cur_node = cur_node->next;
            else
                first = 0;

            if(strlen(location) == strlen(cur_node->location)) {
                if(memcmp(location, cur_node->location, strlen(location)) == 0) {
                    // found one!
                    cur_node->reference_count++;
                    fseek(cur_node->file, 0, SEEK_SET);
                    return cur_node->file;
                }
            }
        } while(cur_node->next != NULL);
        
        // need to add a new node and open the file
        if(!(cur_node->next = malloc(sizeof(struct file_cache_node))))
            return NULL;
        
        cur_node->next->prev = cur_node;
        cur_node = cur_node->next;
        cur_node->next = NULL;
        if(!(cur_node->file = fopen(location, "rb"))) {
            cur_node->prev->next = NULL;
            free(cur_node);
            return NULL;
        }
        if(!(cur_node->location = (char *) malloc(strlen(location)+1))) {
            fclose(cur_node->file);
            cur_node->prev->next = NULL;
            free(cur_node);
            return NULL;
        }
        memcpy(cur_node->location, location, strlen(location) + 1);
        cur_node->reference_count = 1;
        return cur_node->file;
    }
}