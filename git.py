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
    headObj = None # GitObject for the head commit
    
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

    # git rev-list [--maxcount=x] <commit> [<path>]
    #
    # If commit is left as None, then the current head is used.
    # If path is provided, maxcount will be considered as 1.
    #
    # Returns: list of commit objects
    def rev_list(self, commit=None, maxcount=None, path=None):
        commits = []
        i = 1
        
        if maxcount == 0:
            raise Exception, "maxcount must be greater than zero"
            return None
        
        if commit is None:
            workingCommit = self.headObj # no reason to query it again
        else:
            workingCommit = GitObject(commit, self.repo)
        
        if path is None:
            commits.append(workingCommit)
            
            while workingCommit.parent != None and i != maxcount:
                i = i + 1
                workingCommit = GitObject(workingCommit.parent, self.repo)
                commits.append(workingCommit)
            
            return commits
        else:
            # The idea here is find the last time the path was changed.
            #
            # 1) Start by finding its (and any parent trees it's in) sha1 for the
            #    given commit.
            # 2) Compare those sha1's--starting with the top-most level--with the parent commit.
            # 3) Update upper-level sha1's as needed until the bottom-most sha1 (the actual blob)
            #    changes.
            # 4) Go forward one commit and return it.
            
            def build_tree(path, tree):
                pathSha1s = []
                for i in range(len(path)):
                    for mode, sha1, name in tree:
                        if i != len(path) - 1:
                            # then we are looking for directories
                            if mode == 40000 and name == path[i]:
                                pathSha1s.append(sha1)
                                break
                        else:
                            # then we are looking for a file
                            if mode != 40000 and name == path[i]:
                                pathSha1s.append(sha1)
                                break
                    if i + 1 == len(pathSha1s) and i + 1 < len(path):
                        # we need another tree to dig down
                        tree = GitObject(sha1, self.repo).entries
                    else:
                        break
                return pathSha1s
            
            # prep path if needed (we just want the path within the rep, not a full path)
            if self.repo[:-4] == path[:len(self.repo)-4]: # compare without "/.git"
                path = path[len(self.repo[:-4]):]
            path = path.split('/')
                        
            initialSha1Tree = build_tree(path, GitObject(workingCommit.tree, self.repo).entries)
            if len(path) != len(initialSha1Tree):
                raise Exception, "That path does not exist within the provided commit"
                return None
            if workingCommit.parent == None:
                # initial commit
                return workingCommit
            
            previousCommit = workingCommit
            workingCommit = GitObject(workingCommit.parent, self.repo)
            sha1Tree = build_tree(path, GitObject(workingCommit.tree, self.repo).entries)
            while len(sha1Tree) == len(initialSha1Tree) and sha1Tree[-1:] == initialSha1Tree[-1:] and workingCommit.parent != None:
                previousCommit = workingCommit
                workingCommit = GitObject(workingCommit.parent, self.repo)
                if workingCommit.tree == None:
                    raise Exception, "there is a problem in the function!!!"
                    return None
                sha1Tree = build_tree(path, GitObject(workingCommit.tree, self.repo).entries)
            
            return previousCommit
    
    # git ls-tree <tree|commit>
    # Returns: a list of tree entries where each entry is a tuple: (mode, filename, sha1)
    #          or None on error.
    def ls_tree(self, tree):
        tree = GitObject(tree, self.repo)
        
        if tree.kind is TREE:
            entries = tree.entries
        elif tree.kind is COMMIT:
            realtree = GitObject(tree.tree, self.repo)
            entries = realtree.entries
            #del realtree # doesn't this happen automatically? :P
        else:
            entries = None
            
        #del tree # doesn't this also happen automatically? :P
        return entries

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
        #print "######## " + sha1
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
            if self.kind is TREE:
                self.entries = self.raw
                #self.loadTree()
            elif self.kind is COMMIT:
                self.loadCommit()
            elif self.kind is TAG:
                self.loadTag()
            elif self.kind is BLOB:
                # might as well load into ram.... delete GitObjects you don't use, k?
                self.data = self.raw
            
            if self.kind in (COMMIT, TAG, BLOB): # tree types don't return file objects!
                del self.raw
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
        if not self.message and self.kind is COMMIT:
            self.tree = self.raw[5:45]
            if self.raw[46:53] == "parent ":
                self.parent = self.raw[53:93]
            else:
                # initial commit; there is no parent
                self.parent = None
            
            #self.raw.seek(self.raw.tell() + 8) # "\nauthor "
            #line = self.raw.readline()
            #self.author = line[:line.index('>')+1] # include the email address
            
            #self.raw.seek(self.raw.tell() + 10) # "comitter "
            #line = self.raw.readline()
            #self.comitter = line[:line.index('>')+1]
            #self.commitTime = line[line.index('>')+2:len(line)-1]
            
            #self.raw.seek(self.raw.tell() + 1) # "\n"
            self.message = self.raw # get the rest
        
    def loadTag(self):
        pass