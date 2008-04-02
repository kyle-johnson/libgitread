#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>

#include "libgitread.h"
#include "filecache.h"

#define CHUNKSIZE (1024*4)

// Since some scripting languages have issues with binary sha1 digests
// and strings with null charactres, this function is provided to turn
// a git_objec.data file for a tree into a pure-ascii representation.
// One tree entry per line:
//      mode name sha1
int bin_tree_to_ascii_tree(struct git_object *g_obj)
{
    unsigned char *source_internal_buffer, *src_buff,
                  *end, *pos;
    char *sha1;
    
    if(!(source_internal_buffer = (unsigned char *) malloc(g_obj->size)))
        return -1;
    
    fseek(g_obj->data, 0, SEEK_SET);
    fread(source_internal_buffer, 1, g_obj->size, g_obj->data);
    src_buff = source_internal_buffer;
    fclose(g_obj->data);
    
    if(!(g_obj->data = tmpfile())) {
        free(source_internal_buffer);
        return -2;
    }
    
    end = (unsigned char *) src_buff + g_obj->size;
    
    while(src_buff < end) {
        pos = memchr(src_buff, '\0', 1024); // get the mode and filename
        fwrite(src_buff, 1, pos - src_buff, g_obj->data);
        fwrite(" ", 1, 1, g_obj->data);
        src_buff = (unsigned char *) pos + 1;
        
        sha1 = sha1_to_hex(src_buff);
        fwrite(sha1, 1, 40, g_obj->data);
        fwrite("\n", 1, 1, g_obj->data);
        src_buff += 20;
    }
    fseek(g_obj->data, 0, SEEK_SET);
    free(source_internal_buffer);
    
    return 0;
}

// Taken directly from sha1_file.c from git v 1.5.5
//
// This uses a funky buffer/cache thing which could be removed,
// but why bother?
char * sha1_to_hex(const unsigned char * sha1)
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

// Taken from patch-delta.c from git v 1.5.5 with some small changes.
void *patch_delta(const void *src_buf, unsigned long src_size,
		  const void *delta_buf, unsigned long delta_size,
		  unsigned long dst_size)
{
	const unsigned char *data, *top;
	unsigned char *dst_buf, *out, cmd;
	unsigned long size;

	if (delta_size < DELTA_SIZE_MIN)
		return NULL;

	data = delta_buf;
	top = (const unsigned char *) delta_buf + delta_size;

    size = dst_size;

	dst_buf = malloc(size + 1);
	dst_buf[size] = 0; // what is with this??

	out = dst_buf;
	while (data < top) {
		cmd = *data++;
		if (cmd & 0x80) {
			unsigned long cp_off = 0, cp_size = 0;
			if (cmd & 0x01) cp_off = *data++;
			if (cmd & 0x02) cp_off |= (*data++ << 8);
			if (cmd & 0x04) cp_off |= (*data++ << 16);
			if (cmd & 0x08) cp_off |= (*data++ << 24);
			if (cmd & 0x10) cp_size = *data++;
			if (cmd & 0x20) cp_size |= (*data++ << 8);
			if (cmd & 0x40) cp_size |= (*data++ << 16);
			if (cp_size == 0) cp_size = 0x10000;
			if (cp_off + cp_size < cp_size ||
			    cp_off + cp_size > src_size ||
			    cp_size > size)
				break;
			memcpy(out, (char *) src_buf + cp_off, cp_size);
			out += cp_size;
			size -= cp_size;
		} else if (cmd) {
			if (cmd > size)
				break;
			memcpy(out, data, cmd);
			out += cmd;
			data += cmd;
			size -= cmd;
		} else {
			// cmd == 0 is reserved for future encoding
			// extensions. In the mean time we must fail when
			// encountering them (might be data corruption).
            free(dst_buf);
            return NULL;
		}
	}

	// sanity check
	if (data != top || size != 0) {
		free(dst_buf);
		return NULL;
	}

	//*dst_size = out - dst_buf;
	return dst_buf;
}


// Taken from delta.h  from git v 1.5.5 with some small changes to work
// with file pointers instead of data buffers.
//
// This must be called twice on the delta pack entry: first to get the
// expected source size, and again to get the target size.
static inline unsigned int get_delta_hdr_size(FILE *pack_fp)
{
	unsigned char cmd;
	unsigned int size = 0;
	int i = 0;
	do {
        fread(&cmd, 1, 1, pack_fp);
		size |= (cmd & ~0x80) << i;
		i += 7;
    } while (cmd & 0x80); // && data < top);
    
	return size;
}


// Some notes on deltafied data:
//  - Base objects will _always_ be located in the same pack file.
//  - Base objects may be delta objects as well (up to 50 by git's defaults); yes, this
//    can led to many recursive function calls to finally get a "complete" base object.
//  - The base object's type is the result ("un-deltafied") object's type.
//  - REF_DELTA consists of a 20 byte binary SHA1 which is the address of the base object,
//    followed by the delta data.
//  - OFS_DELTA is newer. It consists of a sequence of bytes (similar to those at the
//    start of a pack entry) which provides the offset _before_ the current pack object
//    at which the base object can be found. The delta data follows.
//  - The first entry in the delta data is the length of the base object; this length is
//    encoded similarly to the pack entry's length.
//  - The second entry in the delta data is the length of the result object; encoding is
//    identical to the previous entry.
//  - The rest of the delta data is made up of one or more delta hunks: data and
//    instructions for how to create the result object.
//
//  More info: http://git.rsbx.net/Documents/Git_Data_Formats.txt
//
// ! There's a LOT of common code between this and loose_get_object; probably should
// do some refactoring to combine common parts later.
int pack_get_object(char * location, unsigned int offset, struct git_object * g_obj, int full)
{
    FILE *pack_fp = NULL, *delta_fp = NULL;
    z_stream zst;
    struct git_object base_object; // used for deltas
    unsigned char real_read_buffer[CHUNKSIZE], real_write_buffer[CHUNKSIZE]; // global to prevent stack overflow?
    unsigned char *read_buffer = real_read_buffer, *write_buffer = real_write_buffer;

    int status;
    int amount_read = 0;
    unsigned int size = 0, type = 0, shift;
    unsigned int base_size = 0, result_size = 0; // used for deltas
    unsigned char byte;

    int i;
    int file_position = 0;
    
    // initialize
    g_obj->size = 0;
    g_obj->type = UKNOWNTYPE;
    g_obj->data = NULL;

    if(!(pack_fp = util_open_file_cached(location))) { //fopen(location, "r"))) {
        printf("!!!! failed to open pack file\n");
        return -1;
    }
    
    // move to the entry
    fseek(pack_fp, offset, SEEK_SET);
    
    // bit twiddle time :)
    // check the docs for details:
    //  http://www.kernel.org/pub/software/scm/git/docs/technical/pack-format.txt
    fread(&byte, 1, 1, pack_fp);
    type = (byte >> 4) & 7;
    size = byte & 0xf;
    shift = 4;
    while(byte & 128) {
        fread(&byte, 1, 1, pack_fp);
        size += (byte & 0x7f) << shift;
        shift += 7;
    }
    
    g_obj->size = size;
    g_obj->type = type;
    
    // they just need the type/size
    if(!full && type == BLOB) {
        util_close_file_cached(pack_fp);
        return 0;
    }
    
    if(!(g_obj->data = tmpfile())) {
        util_close_file_cached(pack_fp);
        printf("!!!! failed to create tmpfile\n");
        return -1;
    }
    
    if(type == REF_DELTA) {
        // printf("==== REF_DELTA\n\n");
        struct idx_entry *base_idx;
        
        char * sha1;
        char * idx_location;
        
        // get the sha1        
        fread(read_buffer, 1, 20, pack_fp);
        sha1 = sha1_to_hex(read_buffer);
        
        // find the offset from the index file
        if(!(idx_location = (char *) malloc(sizeof(char)*strlen(location)))) {
            util_close_file_cached(pack_fp);
            return -1;
        }
        memcpy(idx_location, location, strlen(location) - 4);  // remove "pack" extension
        memcpy((char *) (&idx_location + strlen(location) - 4), "idx", 4); // add "idx" plus \0
        base_idx = pack_idx_read(idx_location, sha1);
        free(idx_location);
        if(base_idx == NULL) {
            util_close_file_cached(pack_fp);
            return -1;
        }
        
        // get the base object
        if(pack_get_object(location, base_idx->offset, &base_object, 1) != 0) {
            free(base_idx);
            util_close_file_cached(pack_fp);
            return -1;
        }
    } else if(type == OFS_DELTA) {
        //printf("==== OFS_DELTA\n\n");
        
        unsigned int pack_offset;
        fread(&byte, 1, 1, pack_fp);
        pack_offset = byte & 0x7f;
        while(byte & 0x80) {
            fread(&byte, 1, 1, pack_fp);
            pack_offset += 1;
            pack_offset = (pack_offset << 7) + (byte & 0x7f);
        }
        pack_offset = offset - pack_offset;

        // save file location
        file_position = ftell(pack_fp);

        // get the base object
        if(pack_get_object(location, pack_offset, &base_object, 1) != 0) {
            printf("!!!! failed to get the base object for a OFS_DELTA\n");
            util_close_file_cached(pack_fp);
            return -1;
        }
        // reset file position
        fseek(pack_fp, file_position, SEEK_SET);
    }    
    
    zst.zalloc = Z_NULL; // use defaults
    zst.zfree = Z_NULL; // ''
    zst.opaque = Z_NULL;
    zst.avail_in = 0;
    zst.next_in = Z_NULL;
    
    if((status = inflateInit(&zst)) != Z_OK) {
        util_close_file_cached(pack_fp);
        return status;
    }
    
    // start reading in data and decompressing it
    do {
        zst.avail_in = fread(read_buffer, 1, CHUNKSIZE, pack_fp);
        if(ferror(pack_fp)) {
            inflateEnd(&zst);
            util_close_file_cached(pack_fp);
            return -1;
        }
        if(zst.avail_in == 0) { // eof
            util_close_file_cached(pack_fp);
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
                    util_close_file_cached(pack_fp);
                    printf("=== zlib says: %i\n", status);
                    return status;
            }
     
            amount_read = CHUNKSIZE - zst.avail_out;
            
            if(fwrite(write_buffer, 1, amount_read, g_obj->data) != amount_read || ferror(g_obj->data)) {
                util_close_file_cached(pack_fp);
                inflateEnd(&zst);
                return -1;
            }
        } while(zst.avail_out == 0);
    } while (status != Z_STREAM_END);
    inflateEnd(&zst);
    util_close_file_cached(pack_fp);

    if(type == REF_DELTA || type == OFS_DELTA) {
        fseek(g_obj->data, 0, SEEK_SET);
        
        // have to get these two sizes before decompression
        base_size = get_delta_hdr_size(g_obj->data);
        result_size = get_delta_hdr_size(g_obj->data);
        
        // needed 'since those two hdr_sizes are included in the delta size
        size -= ftell(g_obj->data);
        
        g_obj->type = base_object.type;
        g_obj->size = result_size;

        if(!(delta_fp = tmpfile())) {
            fclose(g_obj->data);
            fclose(base_object.data);
            return -1;
        }
                
        // at this point, we have the two pieces of data to make the result:
        // 1) g_obj->data is the delta data
        // 2) base_object.data is the base data
        //
        // size = delta size
        // base_size = base size
        // result_size = result size
        {
            unsigned char *delta_buffer, *base_buffer, *result_buffer;
            delta_buffer = (unsigned char *) malloc(size);
            base_buffer = (unsigned char *) malloc(base_size);
            
            // fseek(g_obj->data, 0, SEEK_SET);
            fseek(base_object.data, 0, SEEK_SET);
            
            fread(delta_buffer, 1, size, g_obj->data);
            fread(base_buffer, 1, base_size, base_object.data);

            fclose(g_obj->data);
            fclose(base_object.data);
            
            result_buffer = (unsigned char *) patch_delta(base_buffer, base_size, delta_buffer, size, result_size);
            free(delta_buffer);
            free(base_buffer);
            if(result_buffer == NULL) {
                return -1;
            }
            
            // copy result buffer to a file
            if(!(g_obj->data = tmpfile())) {
                free(result_buffer);
                return -1;
            }
            fwrite(result_buffer, 1, result_size, g_obj->data);
            free(result_buffer);
        }
    }

    fseek(g_obj->data, 0, SEEK_SET);
    return 0;
}

// Note: This function does accept shortened sha1s, just be aware that it returns the first
//       match. This could result in false positives with extremely shortened sha1s.
//
// Version 2 idx files are currently not supported.
struct idx_entry * pack_idx_read(char * location, char * sha1)
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
int loose_get_object(char * location, struct git_object * g_obj, int full)
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
                if(ftell(g_obj->data) == 0) {
                    c_ptr = (unsigned char*) memchr(write_buffer, '\0', amount_read) + 1;
                    fwrite(c_ptr, 1, amount_read - (c_ptr - write_buffer), g_obj->data);
                } else {
                    fwrite(write_buffer, 1, amount_read, g_obj->data);
                }
                
                if(ferror(g_obj->data)) {
                //if(fwrite(write_buffer, 1, amount_read, g_obj->data) != amount_read || ferror(g_obj->data)) {
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
    inflateEnd(&zst);
    fclose(loose_fp);
    
    fseek(g_obj->data, 0, SEEK_SET);
    return 0;
}

/*static int main(int argc, char *argv[])
{
    
    //struct idx_entry * entry = NULL;
    
    //entry = pack_idx_read("/Users/kylejohnson/vector/cairo/.git/objects/pack/pack-4a32c03738a275d48dfb8927d44ccf3bbe1c1713.idx",
    //                      "1ab804891bb71fd742");
    //if(entry != NULL) {
    //    printf("sha1: %s\n", entry->sha1);
    //    printf("offset: %i\n", entry->offset);
    //}
    
    
    struct git_object g_obj;
    
    printf("exit: %i\n\n", pack_get_object("/Users/kylejohnson/vector/cairo/.git/objects/pack/pack-4a32c03738a275d48dfb8927d44ccf3bbe1c1713.pack", (argc > 1)? atoi(argv[1]): 1207716, &g_obj, (argc > 2)? 1:0) );
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
        printf("data object exists!\n\n");

    return 0;
}*/