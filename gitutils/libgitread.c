#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include <zlib.h>

// defined in git's cache.h
#define UKNOWNTYPE -1 // aka: OBJ_BAD
#define COMMIT  1
#define TREE    2
#define BLOB    3
#define TAG     4

#define OFS_DELTA 6
#define REF_DELTA 7


#define CHUNKSIZE (128*4)

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

// Taken directly from sha1_file.c from git v 1.5.5
//
// This uses a funky buffer/cache thing which can be removed,
// but why bother?
static char * sha1_to_hex(const unsigned char * sha1)
{
	static int bufno;
	static char hexbuffer[4][50];
	static const char hex[] = "0123456789abcdef";
	char *buffer = hexbuffer[3 & ++bufno], *buf = buffer;
	int i;

	for (i = 0; i < 20; i++) {
		unsigned int val = *sha1++;
		*buf++ = hex[val >> 4];
		*buf++ = hex[val & 0xf];
	}
	*buf = '\0';

	return buffer;
}

// Currently, deltafied data is NOT supported.
//
// There's a LOT of common code between this and loose_get_object; probably should
// do some refactoring to combine common parts later.
static int pack_get_object(char * location, unsigned int offset, struct git_object * g_obj, int full)
{
    FILE *pack_fp = NULL, *tmp = NULL;
    z_stream zst;
    unsigned char real_read_buffer[CHUNKSIZE], real_write_buffer[CHUNKSIZE];
    unsigned char *read_buffer = real_read_buffer, *write_buffer = real_write_buffer;

    int status;
    int amount_read = 0;
    unsigned int size = 0, type = 0, shift;
    unsigned char byte;
    
    // initialize
    g_obj->size = 0;
    g_obj->type = UKNOWNTYPE;
    g_obj->data = NULL;

    if(!(pack_fp = fopen(location, "r")))
        return -1;
    
    // move to the entry
    fseek(pack_fp, offset, SEEK_SET);
    
    // bit twiddle time :)
    // check the docs for details:
    //  http://www.kernel.org/pub/software/scm/git/docs/technical/pack-format.txt
    fread(&byte, 1, 1, pack_fp);
    type = (byte >> 4) & 7;             // bits 5 - 7
    size = byte & 0xf;                  // start with bits 0 - 4
    // get the rest of the size if needed
    shift = 4;
    while((byte & 128) != 0) {
        fread(&byte, 1, 1, pack_fp);
        size |= (byte & 0x7f) << shift; // need only bits 0 - 7
        shift += 7;
    }
    
    g_obj->size = size;
    g_obj->type = type;
    
    // they just need the type/size
    if(!full && (type == BLOB || type == OFS_DELTA || type == REF_DELTA) ) {
        fclose(pack_fp);
        return 0;
    }
    
    if(!(g_obj->data = tmpfile())) {
        // can't make a temporary file for the data
        fclose(pack_fp);
        return -1;
    }
    
    zst.zalloc = Z_NULL; // use defaults
    zst.zfree = Z_NULL; // ''
    zst.opaque = Z_NULL;
    zst.avail_in = 0;
    zst.next_in = Z_NULL;
    
    if((status = inflateInit(&zst)) != Z_OK) {
        fclose(pack_fp);
        return status;
    }
    
    // start reading in data and decompressing it
    do {
        zst.avail_in = fread(read_buffer, 1, CHUNKSIZE, pack_fp);
        if(ferror(pack_fp)) {
            inflateEnd(&zst);
            fclose(pack_fp);
            return -1;
        }
        if(zst.avail_in == 0) { // eof
            fclose(pack_fp);
            return -1;
        }
        zst.next_in = read_buffer;
        
        // fill up the write buffer
        do {
            zst.avail_out = CHUNKSIZE;
            zst.next_out = write_buffer;
            status = inflate(&zst, Z_NO_FLUSH);
            assert(status != Z_STREAM_ERROR);
            switch(status) {
                case Z_NEED_DICT:
                    status = Z_DATA_ERROR;
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&zst);
                    fclose(pack_fp);
                    return status;
            }
     
            amount_read = CHUNKSIZE - zst.avail_out;
            
            if(fwrite(write_buffer, 1, amount_read, g_obj->data) != amount_read || ferror(g_obj->data)) {
                fclose(pack_fp);
                inflateEnd(&zst);
                return -1;
            }
        } while(zst.avail_out == 0);
    } while (status != Z_STREAM_END);
    
    fclose(pack_fp);
    inflateEnd(&zst);
    return 0;
}

// Note: This function does accept shortened sha1s, just be aware that it returns the first
//       match. This could result in false positives with extremely shortened sha1s.
//
// Version 2 idx files are currently not supported.
static struct idx_entry * pack_idx_read(char * location, char * sha1)
{
    FILE *idx_fp = NULL;
    struct idx_v1_entry entry;
    struct idx_entry *nice_entry = NULL;
    unsigned char internal_buffer[4];
    unsigned char *buffer = internal_buffer;
    unsigned int total_entries = 0;
    char * idx_sha1;
    int i;
    
    if(!(idx_fp = fopen(location, "r")))
        return NULL; // not much in the way for a status message...

    if(!(fread(buffer, 1, 4, idx_fp) == 4)) {
        fclose(idx_fp);
        return NULL;
    }
    
    // check for version 2, fixing network byte order
    if((unsigned int) ( (((buffer[0] << 24) | buffer[1] << 16) | buffer[2] << 8) | buffer[3] ) == IDX_VERSION_TWO_SIG) {
        fclose(idx_fp);
        return NULL;
    }

    // get number of entries in the file and fix network byte orders
    fseek(idx_fp, 1020, SEEK_SET);
    fread(buffer, 1, 4, idx_fp);
    total_entries = (unsigned int) (((buffer[0] << 24) | buffer[1] << 16) | buffer[2] << 8) | buffer[3];
    
    // !!! there is no error checking for a corrupt idx file
    for(i = 0; i < total_entries; i++) {
        fread(&entry, sizeof(struct idx_v1_entry), 1, idx_fp);
        idx_sha1 = sha1_to_hex(entry.sha1);
        if(memcmp(idx_sha1, sha1, strlen(sha1) - 1) == 0) {
            // we have a match!
            if(!(nice_entry = (struct idx_entry *) malloc(sizeof(struct idx_entry)))) {
                fclose(idx_fp);
                return NULL;
            }
            nice_entry->offset = (unsigned int) (((entry.offset[0] << 24) | entry.offset[1] << 16) | entry.offset[2] << 8) | entry.offset[3];
            memcpy(nice_entry->sha1, idx_sha1, 41); // hex hash + \0
            fclose(idx_fp);
            
            return nice_entry;
        }
    }
    
    fclose(idx_fp);
    
    return NULL; // no match
}

// much of this function is from the zlib zpipe.c example
static int loose_get_object(char * location, struct git_object * g_obj, int full)
{
    FILE *loose_fp = NULL;
    FILE *tmp = NULL;
    unsigned char real_read_buffer[CHUNKSIZE];
    unsigned char real_write_buffer[CHUNKSIZE];
    unsigned char *read_buffer = real_read_buffer;
    unsigned char *write_buffer = real_write_buffer;
    unsigned char *c_ptr = NULL;

    int status;
    int amount_read = 0;

    int find_type = 1;
    int find_size = 1;

    z_stream zst;
    
    // initialize
    g_obj->size = 0;
    g_obj->type = UKNOWNTYPE;
    g_obj->data = NULL;

    zst.zalloc = Z_NULL; // use defaults
    zst.zfree = Z_NULL; // ''
    zst.opaque = Z_NULL;
    zst.avail_in = 0;
    zst.next_in = Z_NULL;

    if(!(loose_fp = fopen(location, "r")))
        return -1;
    
    if((status = inflateInit(&zst)) != Z_OK) {
        fclose(loose_fp);
        return status;
    }
    
    // start reading in data and decompressing it
    do {
        zst.avail_in = fread(read_buffer, 1, CHUNKSIZE, loose_fp);
        if(ferror(loose_fp)) {
            inflateEnd(&zst);
            fclose(loose_fp);
            return -1;
        }
        if(zst.avail_in == 0) { // eof
            fclose(loose_fp);
            return -1;
        }
        zst.next_in = read_buffer;
        
        // fill up the write buffer
        do {
            zst.avail_out = CHUNKSIZE;
            zst.next_out = write_buffer;
            status = inflate(&zst, Z_NO_FLUSH);
            assert(status != Z_STREAM_ERROR);
            switch(status) {
                case Z_NEED_DICT:
                    status = Z_DATA_ERROR;
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&zst);
                    fclose(loose_fp);
                    return status;
            }
     
            amount_read = CHUNKSIZE - zst.avail_out;
            if(amount_read >= 6 && find_type) {
                // we have enough data to find out the type
                if(memcmp("blob", write_buffer, 4) == 0) {
                    g_obj->type = BLOB;
                } else if(memcmp("commit", write_buffer, 6) == 0) {
                    g_obj->type = COMMIT;
                    full = 1;
                } else if(memcmp("tree", write_buffer, 4) == 0) {
                    g_obj->type = TREE;
                    full = 1;
                } else if(memcmp("tag", write_buffer, 3) == 0) {
                    g_obj->type = TAG;
                } else {
                    g_obj->type = UKNOWNTYPE;
                }
                find_type = 0;
                
                if(full) {
                    if(!(g_obj->data = tmpfile())) {
                        // can't make a temporary file for the data
                        fclose(loose_fp);
                        inflateEnd(&zst);
                        return -1;
                    }
                }
            }
            if(amount_read >= 6 && find_size) {
                // we might have enough to find the size...
                if(c_ptr = (unsigned char*) memchr(write_buffer, '\0', amount_read)) {
                    // now find the space that comes before the end of the size
                    c_ptr = (unsigned char*) memchr(write_buffer, ' ', amount_read);
                    g_obj->size = atoi((char *) c_ptr);
                    find_size = 0;
                }
            }
            
            // We ASSUME that on the first loop, we have AT LEAST figured out the type
            // and size of the object.
            //
            // If this doesn't work, a larger CHUNKSIZE should fix the problem.
            if(full) {
                if(fwrite(write_buffer, 1, amount_read, g_obj->data) != amount_read || ferror(g_obj->data)) {
                    fclose(loose_fp);
                    inflateEnd(&zst);
                    return -1;
                }
            } else {
                // we should be done; clean up and return
                fclose(loose_fp);
                inflateEnd(&zst);
                return 0;
            }
        } while(zst.avail_out == 0);
    } while (status != Z_STREAM_END);
    
    fclose(loose_fp);
    inflateEnd(&zst);
    return 0;
}

int main(void)
{
    /*
    struct idx_entry * entry = NULL;
    
    entry = pack_idx_read("/Users/kylejohnson/vector/cairo/.git/objects/pack/pack-4a32c03738a275d48dfb8927d44ccf3bbe1c1713.idx",
                          "1ab804891bb71fd742");
    if(entry != NULL) {
        printf("sha1: %s\n", entry->sha1);
        printf("offset: %i\n", entry->offset);
    }/**/
    
    ///*
    struct git_object g_obj;
    
    printf("exit: %i\n\n", pack_get_object("/Users/kylejohnson/vector/cairo/.git/objects/pack/pack-4a32c03738a275d48dfb8927d44ccf3bbe1c1713.pack", 12, &g_obj, 0) );
    switch(g_obj.type) {
        case COMMIT:
            printf("type is commit\n");
            break;
        case BLOB:
            printf("type is blob\n");
            break;
        case TREE:
            printf("type is tree\n");
            break;
        case TAG:
            printf("type is tag\n");
            break;
        default:
            printf("type is unknown: %i\n", g_obj.type);
    }
    printf("size is: %i\n", g_obj.size);
    if(g_obj.data != NULL)
        printf("data object exists!\n\n"); /**/
    
    return 0;
}