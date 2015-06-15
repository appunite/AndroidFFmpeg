#!/bin/bash -e
svn checkout http://libyuv.googlecode.com/svn/trunk/ libyuv
(cd freetype2 && ./autogen.sh)
(cd fribidi && autoreconf -ivf)
(cd libass && autoreconf -ivf)
(cd vo-aacenc && autoreconf -ivf)
(cd vo-amrwbenc && autoreconf -ivf)
