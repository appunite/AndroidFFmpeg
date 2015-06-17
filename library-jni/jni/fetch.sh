#!/bin/bash -e
(cd freetype2 && ./autogen.sh)
(cd fribidi && autoreconf -ivf)
(cd libass && autoreconf -ivf)
(cd vo-aacenc && autoreconf -ivf)
(cd vo-amrwbenc && autoreconf -ivf)
