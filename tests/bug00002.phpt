--TEST--
Test for traces to file
--SKIPIF--
<?php if (!extension_loaded("xdebug")) print "skip"; ?>
--INI--
xdebug.enable=1
xdebug.auto_trace=0
xdebug.collect_params=1
xdebug.collect_return=0
xdebug.collect_assignments=0
xdebug.auto_profile=0
xdebug.profiler_enable=0
xdebug.show_mem_delta=0
xdebug.trace_format=0
--FILE--
<?php
	require 'bug00002.inc';

	$action = 'do_stuff';
	$tf = xdebug_start_trace('/tmp/bug00002.trace');
	$action();
	xdebug_stop_trace();
	readfile($tf);
	unlink($tf);
?>
--EXPECTF--

TRACE START [%d-%d-%d %d:%d:%d]
    %f%w%d     -> do_stuff() /%s/bug00002.php:6
    %f%w%d     -> xdebug_stop_trace() /%s/bug00002.php:7
    %f%w%d
TRACE END   [%d-%d-%d %d:%d:%d]
