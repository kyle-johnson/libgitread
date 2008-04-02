from distutils.core import setup, Extension

setup(version = '0.1', description = 'Wrapper for libgitread; a tiny C library for reading git objects.',
    ext_modules = [Extension('gitutil', sources = ['pygitutil.c', 'libgitread.c', 'filecache.c'], libraries = ['z'])]
)