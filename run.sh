#!/bin/bash
PHP_EXECUTABLE=php
EXT=$(find $PWD/modules -iname "*.so"| xargs basename)
$PHP_EXECUTABLE -d extension_dir=`pwd`/modules/ -d zend_extension=`pwd`/modules/${EXT%} $* -d xdebug.profiler_enable=1 -d xdebug.profiler_cputime=1 -d xdebug.profiler_output_dir=`pwd` $*
