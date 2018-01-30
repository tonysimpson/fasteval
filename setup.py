from __future__ import division, absolute_import, print_function

import os
import sys
from setuptools import setup
from setuptools.extension import Extension

# Building without nanoconfig
cpy_extension = Extension('fasteval', sources=['fasteval.c'], 
        extra_compile_args=["-O3"])


setup(
    name='fasteval',
    ext_modules=[cpy_extension],
    author='Tony Simpson',
    author_email='agjasimpson@gmail.com',
    license='MIT',
)
