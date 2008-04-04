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

struct sha1 {
    unsigned char sha1[20];
    unsigned short length;
};

struct idx {
    char *location;
    unsigned char *data;
    int version;
    size_t size;
    uint32_t entries;
};

struct idx_entry {
    uint32_t offset;
    unsigned char sha1[20];
};

struct git_object {
    unsigned int type;
    unsigned int size;
    FILE *data;
    unsigned char* mem_data;
};

int get_sha1_hex(const char *hex, unsigned char *sha1);
inline int str_sha1_to_sha1_obj(const char *str_sha1, struct sha1 *obj_sha1);
char * sha1_to_hex(const unsigned char * sha1);
void *patch_delta(const void *src_buf, unsigned long src_size,
		  const void *delta_buf, unsigned long delta_size,
          unsigned long dst_size);
int pack_get_object(char * location,
                    unsigned int offset,
                    struct git_object * g_obj,
                    int full);
void unload_idx(struct idx *idx);
struct idx * load_idx(char *location);
struct idx_entry * pack_idx_read(const struct idx *index, const struct sha1 *hash);
int loose_get_object(char * location, struct git_object * g_obj, int full);



#endif