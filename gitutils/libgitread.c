#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <netinet/in.h> // for ntohl(), etc
#include <string.h>
#include <zlib.h>

#include "libgitread.h"
#include "filecache.h"

#define CHUNKSIZE (1024*4)

const signed char hexval_table[256] = {
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 00-07 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 08-0f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 10-17 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 18-1f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 20-27 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 28-2f */
	  0,  1,  2,  3,  4,  5,  6,  7,		/* 30-37 */
	  8,  9, -1, -1, -1, -1, -1, -1,		/* 38-3f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 40-47 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 48-4f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 50-57 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 58-5f */
	 -1, 10, 11, 12, 13, 14, 15, -1,		/* 60-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 68-67 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 70-77 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 78-7f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 80-87 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 88-8f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 90-97 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* 98-9f */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a0-a7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* a8-af */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b0-b7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* b8-bf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c0-c7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* c8-cf */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d0-d7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* d8-df */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e0-e7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* e8-ef */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f0-f7 */
	 -1, -1, -1, -1, -1, -1, -1, -1,		/* f8-ff */
};

static inline unsigned int hexval(unsigned char c)
{
	return hexval_table[c];
}

// Works with short hex strings too!
// (Just note that in the case of an odd number of hex characters,
// the last character will be discarded.)
int get_sha1_hex(const char *hex, unsigned char *sha1)
{
	int i;
    unsigned int val;
    int len = strlen(hex) / 2;
    //int odd_or_even = len % 2;
    
    for(i = 0; i < len; i++) {
        val = (hexval(hex[0]) << 4) | hexval(hex[1]);
		if (val & ~0xff)
			return -1;
		*sha1++ = val;
		hex += 2;
    }
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
            //printf("++++ cmd == 0 in delta_patch\n");
            return NULL;
		}
	}

	// sanity check
	if (data != top || size != 0) {
		free(dst_buf);
        //printf("++++ sanity check failed in delta_patch\n");
        //printf("     data = %i\n     top = %i\n     size = %i\n", (int) data, (int) top, (int)size);
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
static inline unsigned int get_delta_hdr_size(unsigned char **datap)//FILE *pack_fp)
{
    unsigned char *data = *datap;
	unsigned char cmd;
	unsigned int size = 0;
	int i = 0;
	do {
        //fread(&cmd, 1, 1, pack_fp);
        cmd = *data++;
		size |= (cmd & ~0x80) << i;
		i += 7;
    } while (cmd & 0x80); // && data < top);
    *datap = data;
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
    unsigned char *obj_data = NULL;
    
    int status;
    int amount_read = 0;
    unsigned int size = 0, type = 0, shift;
    unsigned int base_size = 0, result_size = 0; // used for deltas
    unsigned char byte;
    
    int i;
    int file_position = 0;
    
    //printf("%i\n", (int) offset);
    
    // initialize
    g_obj->size = 0;
    g_obj->type = UKNOWNTYPE;
    g_obj->data = NULL;
    g_obj->mem_data = NULL;
    
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
    
    if(!(g_obj->mem_data = (unsigned char *) malloc(g_obj->size))) {//g_obj->data = tmpfile())) {
        util_close_file_cached(pack_fp);
        printf("!!!! failed to create tmpfile\n");
        return -1;
    }
    obj_data = g_obj->mem_data;
    
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
        //printf("#### getting a delta\n");
        if(pack_get_object(location, pack_offset, &base_object, 1) != 0) {
            printf("!!!! failed to get the base object for a OFS_DELTA\n");
            free(g_obj->mem_data);
            g_obj->mem_data = NULL;
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
        free(g_obj->mem_data);
        g_obj->mem_data = NULL;
        return status;
    }
    
    // start reading in data and decompressing it
    do {
        zst.avail_in = fread(read_buffer, 1, CHUNKSIZE, pack_fp);
        if(ferror(pack_fp)) {
            inflateEnd(&zst);
            util_close_file_cached(pack_fp);
            free(g_obj->mem_data);
            g_obj->mem_data = NULL;
            printf("==== ferror(pack_fp)\n");
            return -1;
        }
        if(zst.avail_in == 0) { // eof ?
            util_close_file_cached(pack_fp);
            free(g_obj->mem_data);
            g_obj->mem_data = NULL;
            printf("==== zst.avail_in == 0\n");
            return -1;
        }
        zst.next_in = read_buffer;
        
        // fill up the write buffer
        do {
            zst.avail_out = g_obj->size - (obj_data - g_obj->mem_data);//CHUNKSIZE;
            zst.next_out = obj_data;//write_buffer;
            status = inflate(&zst, Z_NO_FLUSH);

            switch(status) {
                case Z_STREAM_ERROR:
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&zst);
                    util_close_file_cached(pack_fp);
                    free(g_obj->mem_data);
                    g_obj->mem_data = NULL;
                    printf("=== zlib says: %i\n", status);
                    return status;
            }
     
            amount_read = g_obj->size - ((obj_data - g_obj->mem_data) + zst.avail_out);//CHUNKSIZE - zst.avail_out;
            
            obj_data += amount_read;
            /*if(fwrite(write_buffer, 1, amount_read, g_obj->data) != amount_read || ferror(g_obj->data)) {
                util_close_file_cached(pack_fp);
                inflateEnd(&zst);
                return -1;
            }*/
        } while(zst.avail_out == 0 && status != Z_STREAM_END);
    } while (status != Z_STREAM_END);
    inflateEnd(&zst);
    util_close_file_cached(pack_fp);

    obj_data = g_obj->mem_data;

    if(type == REF_DELTA || type == OFS_DELTA) {
        //fseek(g_obj->data, 0, SEEK_SET);
        
        // have to get these two sizes before decompression
        base_size = get_delta_hdr_size(&obj_data);
        result_size = get_delta_hdr_size(&obj_data);
        
        // needed 'since those two hdr_sizes are included in the delta size
        size -= obj_data - g_obj->mem_data;//ftell(g_obj->data);
        //printf("-+-+ offset: %i\n     obj_data: %i\n     mem_data: %i\n", (int) (obj_data - g_obj->mem_data), (int) obj_data, (int) g_obj->mem_data);
        
        g_obj->type = base_object.type;
        g_obj->size = result_size;

        /*if(!(delta_fp = tmpfile())) {
            free(g_obj->mem_data);//fclose(g_obj->data);
            free(base_object.mem_data);//fclose(base_object.data);
            return -1;
        }*/

        // at this point, we have the two pieces of data to make the result:
        // 1) g_obj->data is the delta data
        // 2) base_object.data is the base data
        //
        // size = delta size
        // base_size = base size
        // result_size = result size
        {
            unsigned char *delta_buffer, *base_buffer, *result_buffer;
            //delta_buffer = (unsigned char *) malloc(size);
            //base_buffer = (unsigned char *) malloc(base_size);
            
            //fseek(base_object.data, 0, SEEK_SET);
            
            delta_buffer = obj_data;//fread(delta_buffer, 1, size, g_obj->data);
            base_buffer = base_object.mem_data;//fread(base_buffer, 1, base_size, base_object.data);

            //fclose(g_obj->data);
            //fclose(base_object.data);
            
            result_buffer = (unsigned char *) patch_delta(base_buffer, base_size, delta_buffer, size, result_size);
            free(g_obj->mem_data);//free(delta_buffer);
            free(base_object.mem_data);//free(base_buffer);
            if(result_buffer == NULL) {
                printf("==== result_buffer == NULL\n");
                return -1;
            }
            
            // copy result buffer to a file
            //if(!(g_obj->data = tmpfile())) {
            //    free(result_buffer);
            //    return -1;
            //}
            //fwrite(result_buffer, 1, result_size, g_obj->data);
            //free(result_buffer);
            g_obj->mem_data = result_buffer;
        }
    }

    //fseek(g_obj->data, 0, SEEK_SET);
    return 0;
}

void unload_idx(struct idx *idx)
{
    if(!idx)
        return;
    
    if(idx->data)
        munmap(idx->data, idx->size);
    
    free(idx->location);
    free(idx);
    idx = NULL;
}

struct idx * load_idx(char *location)
{
    int idx_fd;
    int idx_size;
    struct stat idx_st;
    struct idx *idx;
    void *raw_idx_data;
    uint32_t *hdr, entries;

    if((idx_fd = open(location, O_RDONLY)) < 0)
        return NULL;
    
    // do some quick integrety checking
    if(fstat(idx_fd, &idx_st)) {
        close(idx_fd);
        return NULL;
    }
    if((int) idx_st.st_size < 4*256+24+20+20) // header + one entry + packfile checksum + idxfile checksum
    {
        printf("Bad idx file: size is too small.\n");
        close(idx_fd);
        return NULL;
    }
    
    // mmap the idx file
    raw_idx_data = (unsigned char *) mmap(NULL, idx_st.st_size, PROT_READ, MAP_PRIVATE, idx_fd, 0);
    close(idx_fd);
    if(!raw_idx_data)
        return NULL;
    
    // check for version 1 or 2
    hdr = raw_idx_data;
    if(*hdr == htonl(IDX_VERSION_TWO_SIG)) {
        printf("Version 2 idx files not supported.\n");
        munmap(raw_idx_data, idx_st.st_size);
        return NULL;
    }
    
    // Additional version integrety checks go here if desired.
    // 1) For version 1 idxs, read all 255 chunks in the header and ensure that the preceeding
    //    chunk is less than the following chunk. (This is unimplemented for small speed
    //    reasons.)
    // 2) Read the total object count and compare it to the actual size. (This is implemented.)
    entries = ntohl(hdr[255]);
    if((uint32_t) idx_st.st_size != (4*256 + entries * 24 + 20 + 20)) {
        printf("Bad idx file: file length does not match the number of entries.\n");
        munmap(raw_idx_data, idx_st.st_size);
        return NULL;
    }
    
    // Enough checking. Time to store all this to an idx struct and return it.
    if(!(idx = malloc(sizeof(struct idx)))) {
        printf("Failed to allocate an idx struct.\n");
        munmap(raw_idx_data, idx_st.st_size);
        return NULL;
    }
    if(!(idx->location = malloc(strlen(location)+1))) {
        printf("Failed to allocate idx.location.\n");
        free(idx);
        munmap(raw_idx_data, idx_st.st_size);
        return NULL;
    }
    memcpy(idx->location, location, strlen(location)+1);
    idx->data = raw_idx_data;
    idx->version = 1;
    idx->size = idx_st.st_size;
    idx->entries = entries;
    
    return idx;
}

// Compare two hashes. The name is a bit of a misnomer; "pf" means "partial hash"
// and "full hash", though all this really means is the first argument needs to
// be a struct sha1 and the second a 20-byte, binary sha1.
// Return is identical to that of memcmp.
static inline int hashcmp_pf(const struct sha1 *hash, const unsigned char *full_hash)
{
    //unsigned char *partial_hash;
    
    //partial_hash = &hash->sha1;
    return memcmp((unsigned char *) &hash->sha1/*partial_hash*/, full_hash, hash->length);
}

// Note: This function does accept shortened sha1s, just be aware that it returns the first
//       match. This could result in false positives with extremely shortened sha1s.
//
// Version 2 idx files are currently not supported.
struct idx_entry * pack_idx_read(const struct idx *idx_index, const struct sha1 *hash)
{
    struct idx_entry *nice_entry = NULL;
    const uint32_t *level1_ofs = idx_index->data;
    const unsigned char *index = idx_index->data;
    unsigned hi, lo;
    
    if(!index || !hash)
        return NULL;

    // Basic idea: the idx's sha1 entries are sorted smallest to largest, so we don't
    // need to search everything.
    index += 4 * 256; // bypass the header
	hi = ntohl(level1_ofs[ *hash->sha1 ]);
	lo = ((hash->sha1[0] == 0) ? 0 : ntohl(level1_ofs[ *hash->sha1 - 1 ]));

	do {
		unsigned mi = (lo + hi) / 2;
		unsigned x = mi * 24 + 4;
		int cmp = hashcmp_pf(hash, index + x);
		if (!cmp) {
		    // we have a match!
		    if(!(nice_entry = malloc(sizeof(struct idx_entry))))
                return NULL;
            nice_entry->offset = ntohl(*((uint32_t *) (index + 24 * mi)));
            memcpy(nice_entry->sha1, index + 24 * mi + 4, 20);
            return nice_entry;
		}
		if (cmp < 0)
			hi = mi;
		else
			lo = mi+1;
	} while (lo < hi);
    
    return NULL; // no match
}

// much of this function is from the zlib zpipe.c example
int loose_get_object(char * location, struct git_object * g_obj, int full)
{
    FILE *loose_fp = NULL;
    FILE *tmp = NULL;
    unsigned char real_read_buffer[CHUNKSIZE];
    unsigned char real_small_write_buffer[128]; //CHUNKSIZE];
    unsigned char *read_buffer = real_read_buffer;
    unsigned char *small_write_buffer = real_small_write_buffer;
    unsigned char *c_ptr = NULL, *obj_data = NULL;

    int status;
    int amount_read = 0;

    int find_type = 1;
    int find_size = 1;

    z_stream zst;
    
    // initialize
    g_obj->size = 0;
    g_obj->type = UKNOWNTYPE;
    g_obj->data = NULL;
    g_obj->mem_data = NULL;

    zst.zalloc = Z_NULL; // use defaults
    zst.zfree = Z_NULL;  // ''
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
        if(zst.avail_in == 0) { // eof ?
            fclose(loose_fp);
            return -1;
        }
        zst.next_in = read_buffer;
        
        // fill up the write buffer
        do {
            if(g_obj->mem_data == NULL) {
                // we are just trying to get the type and size right now, so we use a small buffer
                zst.avail_out = 128;
                zst.next_out = small_write_buffer;
            } else {
                // okay, now we are really goin' to town...
                zst.avail_out = g_obj->size - (obj_data - g_obj->mem_data); //amount_read; // already read some of the data...
                zst.next_out = obj_data;
            }
            
            // error check
            status = inflate(&zst, Z_NO_FLUSH);
            switch(status) {
                case Z_STREAM_ERROR:
                case Z_NEED_DICT:
                case Z_DATA_ERROR:
                case Z_MEM_ERROR:
                    inflateEnd(&zst);
                    fclose(loose_fp);
                    return status;
            }
     
            if(g_obj->mem_data == NULL)
                amount_read = 128 - zst.avail_out;
            else
                amount_read = g_obj->size - ((obj_data - g_obj->mem_data) + zst.avail_out);
            
            // Yes, we actually check for an amount read even though we do little
            // other error checking and it SHOULD be correct. :)
            if(amount_read >= 6 && (find_type || find_size)) {
                if(find_type) {
                    // we have enough data to find out the type
                    if(memcmp("blob", small_write_buffer, 4) == 0) {
                        g_obj->type = BLOB;
                    } else if(memcmp("commit", small_write_buffer, 6) == 0) {
                        g_obj->type = COMMIT;
                        full = 1;
                    } else if(memcmp("tree", small_write_buffer, 4) == 0) {
                        g_obj->type = TREE;
                        full = 1;
                    } else if(memcmp("tag", small_write_buffer, 3) == 0) {
                        g_obj->type = TAG;
                    } else {
                        g_obj->type = UKNOWNTYPE;
                    }
                    find_type = 0;
                }
                if(find_size) {
                    // \0 comes after the size
                    //c_ptr = (unsigned char*) memchr(small_write_buffer, '\0', amount_read);
                    // find the space that comes before the size
                    c_ptr = (unsigned char*) memchr(small_write_buffer, ' ', amount_read);
                    g_obj->size = atoi((char *) c_ptr);
                    find_size = 0;
                    
                    if(full) {
                        if(!(g_obj->mem_data = (unsigned char *) malloc(g_obj->size))) {
                            // can't allocate space for the data
                            fclose(loose_fp);
                            inflateEnd(&zst);
                            return -1;
                        }
                        obj_data = g_obj->mem_data; // setup the utility pointer
                    }
                }
            }
            
            // Write out lingering data from the first time through reading.
            // After this write, zlib will write directly to obj_data itself.
            if(full && g_obj->mem_data - obj_data == 0) {
                // we are at the start, so at this point:
                // 1) we have data in a buffer
                // 2) some of that data we need to discard (up to and including the \0)
                c_ptr = (unsigned char*) memchr(small_write_buffer, '\0', amount_read) + 1;
                memcpy(obj_data, c_ptr, amount_read - (c_ptr - small_write_buffer));
                obj_data += (amount_read - (c_ptr - small_write_buffer));
                
                // fwrite(c_ptr, 1, amount_read - (c_ptr - write_buffer), g_obj->data);
            } else if(full) {
                // update the pointer so we don't overwrite anything
                obj_data += amount_read;
            } else if(!full) {
                // all they wanted was the type and size; clean up and return
                fclose(loose_fp);
                inflateEnd(&zst);
                return 0;
            }
        } while(zst.avail_out == 0 && status != Z_STREAM_END);
    } while (status != Z_STREAM_END);
    inflateEnd(&zst);
    fclose(loose_fp);
    
    return 0;
}

inline int str_sha1_to_sha1_obj(const char *str_sha1, struct sha1 *obj_sha1)
{
    obj_sha1->length = strlen(str_sha1) / 2; // will drop any odd amount that get_sha1_hex drops
    return get_sha1_hex(str_sha1, obj_sha1->sha1);
}

/*
int main(int argc, char *argv[])
{
    struct idx_entry * entry = NULL;
    struct idx *idx = NULL;
    struct sha1 hash;

                          // 7e83d6e6fb8c4b11d5d95acac4a35df8969e0944
    if(str_sha1_to_sha1_obj("7e83d6e6fb8", &hash) != 0) {
        printf("Failed to convert the sha1 to bin.\n");
        return 0;
    }
    
    if(!(idx = load_idx("/Users/kylejohnson/vector/cairo/.git/objects/pack/pack-4a32c03738a275d48dfb8927d44ccf3bbe1c1713.idx"))) {
        printf("Failed to load the idx file.\n");
        return 0;
    }
    
    entry = pack_idx_read(idx, &hash);
    
    if(entry != NULL) {
        printf("sha1: %s\n", sha1_to_hex(entry->sha1));
        printf("offset: %i\n", entry->offset);
    } else {
        printf("entry was null :(\n");
    }
    
    unload_idx(idx);


    /*struct git_object g_obj;

    printf("exit: %i\n\n", pack_get_object("/Users/kylejohnson/vector/cairo/.git/objects/pack/pack-4a32c03738a275d48dfb8927d44ccf3bbe1c1713.pack", (argc > 2)? atoi(argv[2]) : 1207716, &g_obj, (argc > 1)? 1:0));
    //loose_get_object("/Users/kylejohnson/pygitlib/.git/objects/2f/838d9ee515c7004d4c4bc1a35b5aba18668f45", &g_obj, (argc > 1)? 1:0) );
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
    if(g_obj.mem_data != NULL) {
        printf("data object exists!\n\n");
        printf("%s\n-----------\n", g_obj.mem_data);
    }*/


    //return 0;
//}/**/