#ifndef LIBGITREAD_H
#define LIBGITREAD_H

// defined in git's cache.h
#define UKNOWNTYPE -1 // aka: OBJ_BAD
#define COMMIT  1
#define TREE    2
#define BLOB    3
#define TAG     4

#define OFS_DELTA 6
#define REF_DELTA 7

#define DELTA_SIZE_MIN 4

#define IDX_VERSION_TWO_SIG 0xff744f63 // '\377tOc'

struct idx_v1_entry { // file format
    unsigned char offset[4];
    unsigned char sha1[20];
};

struct idx_entry { // nice format
    unsigned int offset;
    unsigned char sha1[40];
};

struct git_object {
    unsigned int type;
    unsigned int size;
    FILE *data;
};

int bin_tree_to_ascii_tree(struct git_object *g_obj);
char * sha1_to_hex(const unsigned char * sha1);
int pack_get_object(char * location,
                    unsigned int offset,
                    struct git_object * g_obj,
                    int full);
struct idx_entry * pack_idx_read(char * location, char * sha1);
int loose_get_object(char * location, struct git_object * g_obj, int full);



#endif