# basic git object handling

import os
import tempfile
import struct
import zlib
import time

# object types
COMMIT = 1
TREE = 2
BLOB = 3
TAG = 4

# storage type
LOOSE = 1
PACKED = 2

# Convert a binary sha1 to a hex string.
# If the sha1 is not binary, just return it.
def sha1_to_hex(sha1):
    try:
        shaTuple = struct.unpack('!10H', sha1)
        return ''.join( [hex(x)[2:] for x in shaTuple] )
    except:
        return sha1

# Read a pack-*.idx file and return its data.
# Or, specify sha1 and it will return 0 if not found or: offset, full sha1.
#
# Format doc:
#  http://www.kernel.org/pub/software/scm/git/docs/technical/pack-format.txt
#
# !!! I haven't seen version 2 files so haven't implemented them
#
# !!! this function DOES handle short sha1's.
def pack_idx_read(location, sha1=None):
    f = open(location)
    
    #test for version two
    if struct.unpack("!L", f.read(4))[0] == 4285812579:
        raise Exception, "Version 2 pack index files are not yet supported"
    
    f.seek(1020)
    count = struct.unpack("!L", f.read(4))[0]

    if sha1:
        for i in range(count):
            offset = f.read(4)
            tempSha1 = sha1_to_hex(f.read(20))
            # did we find it?
            if tempSha1[:len(sha1)] == sha1:
                f.close()
                return struct.unpack("!L", offset)[0], tempSha1
        # found nothing
        f.close()
        return 0, None
    else:
        entries = []
        for i in range(count):
            entries.append( (struct.unpack("!L", f.read(4))[0], sha1_to_hex(f.read(20))) )
        f.close()
        return entries

# By default just return a type and size.
# If full is True then returns the type, size, and file pointer.
#
# Format doc:
#  http://www.kernel.org/pub/software/scm/git/docs/technical/pack-format.txt
#
# !!! delta code is NOT complete
def pack_get_object(location, offset, full=False):
    f = open(location)
    f.seek(offset)
    
    byte = struct.unpack("!B", f.read(1))[0]
    kind = (byte >> 4) & 7 # bits 5 - 7
    size = byte & 0xf # start with bits 0 - 4
    # get the rest of the size if needed
    shift = 4
    while byte & 128 != 0:
        byte = struct.unpack("!B", f.read(1))[0]
        size = size | ((byte & 0x7f) << shift) # need only bits 0 - 7
        shift = shift + 7
    
    if not full and kind == BLOB:
        # easy enough :)
        f.close()
        return kind, size, None
    else:
        # great, now to deal with the lovely reality of deltafied data and more
        
        # delta example: http://github.com/schacon/git-ruby/tree/master/lib/git-ruby/raw/internal/pack.rb
        # check for/handle delta
        if kind > 4:
            deltaSha1 = sha1_to_hex(f.read(20)) # base object SHA1
            # then delta data, deflated
            f.close()
            raise Exception, "Delta data is not yet implemented"
        else:
            # non-delta
            dc = zlib.decompressobj()
            tmpF = tempfile.TemporaryFile()
            while tmpF.tell() < size:
                tmpF.write(dc.decompress(dc.unconsumed_tail + f.read(64)))
            
            tmpF.seek(0)
            f.close()
            del dc
        
        return kind, size, tmpF

# Given a valid location, returns: kind, size, and possibly a file pointer.
# If full is True then returns the type, size, and file pointer.
#
# File pointers are only returned automatically IF the object is NOT a blob.
def loose_get_object(location, full=False):
    f = open(location)
    
    # uncompress just enough to get the type and size
    dc = zlib.decompressobj()
    buff = []
    i = 0
    buff.append(dc.decompress(f.read(80)))
    while buff[i].find('\x00') == -1:
        i = i + 1
        buff.append(dc.decompress(dc.unconsumed_tail + f.read(30)))

    uc = ''.join(buff)
    typeStr = uc[:uc.index(' ')] # type comes before first space
    size = int(uc[len(typeStr)+1:uc.index('\x00')]) # size comes before \0
    
    if typeStr == 'blob':
        kind = BLOB
    elif typeStr == 'tree':
        kind = TREE
    elif typeStr == 'commit':
        kind = COMMIT
    elif typeStr == 'tag':
        king = TAG
    else:
        raise Exception, "found an unknown object type: %s" % kind
    
    if not full and kind == BLOB:
        # easy
        f.close()
        del dc
        return kind, size, None
    else:
        # need to continue on with the decompression
        tmpF = tempfile.TemporaryFile()
        tmpF.write(uc[uc.index('\x00')+1:]) # we already have a little bit done
        while tmpF.tell() < size:
            tmpF.write(dc.decompress(dc.unconsumed_tail + f.read(64)))
        
        tmpF.seek(0)
        f.close()
        del dc
        return kind, size, tmpF

class GitObject(object):
    location = None # LOOSE or PACKED
    kind = None # aka: type
    size = None # in bytes
    
    raw = None # non-blob objects get data returned immediatly; this is a tempfile
    
    data = None # only for blobs
    
    entries = None # only for trees
    
    message = None # only for commits
    parent = None
    tree = None
    committer = None
    author = None
    commitTime = None
    
    def __init__(self, sha1, gitDir=None, lazy=True):
        t1 = time.time()
        if not gitDir:
            raise Exception, "you must supply the base project directory"
            return
        
        if gitDir.split('/')[-1:] == '.git':
            self.dir = gitDir
        else:
            self.dir = os.path.join(gitDir, '.git')
        if not os.path.exists(self.dir):
            raise Exception, "%s is not the base project directory" % gitDir
            return
        
        self.sha1 = sha1 #sha1_to_hex(sha1)
        
        # now try and find out where the object is; loose first, then pack
        foundLoose = False
        raw = None

        looseBasePath = os.path.join(self.dir, 'objects', self.sha1[:2])
        if os.path.exists(looseBasePath):
            if len(self.sha1) == 40:
                loosePath = os.path.join(looseBasePath, self.sha1[2:])
                if os.path.exists(loosePath):
                    foundLoose = True
            else:
                for looseFile in os.listdir(looseBasePath):
                    if looseFile[:len(self.sha1)-2] == self.sha1[2:]:
                        # found it
                        self.sha1 = self.sha1[:2] + looseFile
                        loosePath = os.path.join(looseBasePath, looseFile)
                        foundLoose = True
                        break

        if foundLoose:
            # good, this is an easy one :)
            self.location = LOOSE
            self.path = loosePath
            self.kind, self.size, raw = loose_get_object(self.path)
        else:
            # the object is in a pack file
            packDir = os.path.join(self.dir, 'objects/pack')
            idxFiles = [packDir + '/' + x for x in os.listdir(packDir) if x[-3:] == 'idx']
            for idx in idxFiles:
                offset, fullSha1 = pack_idx_read(idx, sha1=self.sha1)
                if offset != 0:
                    self.location = PACKED
                    self.path = idx[:-3] + 'pack'
                    self.offset = offset
                    self.sha1 = fullSha1
                    self.kind, self.size, raw = pack_get_object(self.path, self.offset)
                    break
        
        if raw:
            self.raw = raw
            if self.kind == TREE:
                self.loadTree()
            elif self.kind == COMMIT:
                self.loadCommit()
            elif self.kind == TAG:
                self.loadTag()
            else:
                raise Exception, "raw data was given when none was required"
            
            self.raw.close()
            self.raw = None
            

        if not self.location:
            raise Exception, "object %s does not exist" % self.sha1
            return
        
        t2 = time.time()
        print "Total __init__ time: %f" % (t2-t1)
    
    def loadTree(self):
        closeRaw = False # if __init__ called, then it will close self.raw
        # quick error check
        if not self.location:
            raise Exception, "This is an involid object"
            return
        
        # begin loading
        if not self.entries and self.kind == TREE:
            self.entries = []
            
            #if not self.raw:
            #    closeRaw = True
            #    if self.location == LOOSE:
            #        _, _, self.raw = loose_get_object(self.path, full=True)
            #    else: # PACKED
            #        _, _, self.raw = pack_get_object(self.path, self.offset, full=True)
            
            while self.raw.tell() < self.size:
                fileName = []
                c = self.raw.read(1)
                fileName.append(c)
                while c != '\x00':
                    c = self.raw.read(1)
                    fileName.append(c)
                    
                # remove the \0 from the list
                del fileName[len(fileName)-1]
                self.entries.append( (''.join(fileName[fileName.index(' ')+1:]), sha1_to_hex(self.raw.read(20))) )
    
    def loadCommit(self):
        closeRaw = False # if __init__ called, then it will close self.raw
        # quick error check
        if not self.location:
            raise Exception, "This is an involid object"
            return
        
        # begin loading
        if not self.message and self.kind == COMMIT:
            self.raw.seek(5) # "tree "
            self.tree = self.raw.read(40)
            self.raw.seek(self.raw.tell() + 8) # "\nparent "
            self.parent = self.raw.read(40)
            
            self.raw.seek(self.raw.tell() + 8) # "\nauthor "
            line = self.raw.readline()
            self.author = line[:line.index('>')+1] # include the email address
            
            self.raw.seek(self.raw.tell() + 10) # "comitter "
            line = self.raw.readline()
            self.comitter = line[:line.index('>')+1]
            self.commitTime = line[line.index('>')+2:len(line)-1]
            
            self.raw.seek(self.raw.tell() + 1) # "\n"
            self.message = self.raw.read() # get the rest
        
    def loadTag(self):
        pass