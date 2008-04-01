# basic git object handling

import os
import time

import gitutil

# object types
COMMIT = 1
TREE = 2
BLOB = 3
TAG = 4

# storage type
LOOSE = 1
PACKED = 2

# This class uses GitObject in a variety of methods
# similar to those provided by git on the command line.
#
# Note that these print nothing out, but mostly return
# objects of various sorts which are easily printed
# via a main() function if desired.
#
# Current emphasis is on read-only operations.
class Git(object):
    repo = None # path to .git dir
    head = None # name (ie. master)
    headSha1 = None
    
    def __init__(self, repo=None):
        # make sure we have the repo dir right
        if not repo:
            if os.getcwd().split('/')[-1:] == '.git':
                self.repo = os.getcwd()
            elif os.path.exists(os.path.join(os.getcwd(), '.git')):
                self.repo = os.path.join(os.getcwd(), '.git')
            else:
                raise Exception, "Could not location .git directory"
        else:
            if repo.split('/')[-1:] == '.git':
                self.repo = repo
            elif os.path.exists(os.path.join(repo, '.git')):
                self.repo = os.path.join(repo, '.git')
            else:
                raise Exception, "Could not location .git directory"
        
        # find current head
        f = open(os.path.join(self.repo, 'HEAD'))
        f.seek(5) # "ref: "
        headPath = os.path.join(self.repo, f.read()[:-1]) # ignore trailing \n
        f.close()
        
        f = open(headPath)
        self.head = headPath.split('/').pop()
        self.headSha1 = f.read()[:-1] # ignore trailing \n
        f.close()
        self.headObj = GitObject(self.headSha1, self.repo)
    
    # git branch
    #
    # Returns: list of (branch name, current)
    def list_branches(self):
        branches = []
        for branch in os.listdir(os.path.join(self.repo, "refs/heads")):
            if branch is self.head:
                branches.append( (branch, True) )
            else:
                branches.append( (branch, False) )
        return branches

    # git rev-list --maxcount=1 <commit> <path>
    #
    # If commit is left as None, then the current head is used.
    #
    # Returns: commit where <path>'s contents last changed.
    def rev_list(self, path, commit=None):
        # prep path if needed
        if self.repo[:-4] == path[:len(self.repo)-4]:
            path = 1
        initialTree = GitObject(self.headObj.tree, self.repo)
        for filename, sha1 in initialTree.entries:
            pass

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
    
    def __init__(self, sha1, gitDir, lazy=True):        
        self.dir = gitDir
        self.sha1 = sha1
        
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
            self.kind, self.size, raw = gitutil.loose_get_object(self.path)
        else:
            # the object is in a pack file
            packDir = os.path.join(self.dir, 'objects/pack')
            idxFiles = [packDir + '/' + x for x in os.listdir(packDir) if x[-3:] == 'idx']
            for idx in idxFiles:
                offset, fullSha1 = gitutil.pack_idx_read(idx, self.sha1)
                if offset != 0:
                    self.location = PACKED
                    self.path = idx[:-3] + 'pack'
                    self.offset = offset
                    self.sha1 = fullSha1
                    self.kind, self.size, raw = gitutil.pack_get_object(self.path, self.offset)
                    break
        
        if raw:
            self.raw = raw
            if self.kind == TREE:
                self.tree = self.raw
                #self.loadTree()
            elif self.kind == COMMIT:
                self.loadCommit()
            elif self.kind == TAG:
                self.loadTag()
            elif self.kind == BLOB:
                # might as well load into ram.... delete GitObjects you don't use, k?
                self.data = self.raw.read()
            
            if self.kind in (COMMIT, TAG, BLOB): # tree types don't return file objects!
                self.raw.close()
            self.raw = None
            

        if not self.location:
            raise Exception, "object %s does not exist" % self.sha1
            return
    
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
            
            #self.raw.seek(self.raw.tell() + 8) # "\nauthor "
            #line = self.raw.readline()
            #self.author = line[:line.index('>')+1] # include the email address
            
            #self.raw.seek(self.raw.tell() + 10) # "comitter "
            #line = self.raw.readline()
            #self.comitter = line[:line.index('>')+1]
            #self.commitTime = line[line.index('>')+2:len(line)-1]
            
            #self.raw.seek(self.raw.tell() + 1) # "\n"
            self.message = self.raw.read() # get the rest
        
    def loadTag(self):
        pass