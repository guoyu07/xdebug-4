--TEST--
Test for segmentation fault with unusual variables (ZE1)
--SKIPIF--
<?php if (!extension_loaded("xdebug")) print "skip"; ?>
<?php if(version_compare(zend_version(), "2.0.0-dev", '>')) echo "skip Zend Engine 1 needed\n"; ?>
--INI--
xdebug.default_enable=1
xdebug.auto_trace=0
xdebug.collect_params=1
xdebug.collect_assignments=0
xdebug.profiler_enable=0
xdebug.show_local_vars=0
xdebug.dump_globals=0
--FILE--
<?php
	${1} = "foo";
	echo ${1} . "\n";

	${STDIN} = "foo";
	echo ${STDIN} . "\n";

	${array(1,2,3)} = "foo";
	echo ${array(1,2,3)} . "\n";

	${new stdclass} = "foo";
	echo ${new stdclass} . "\n";
?>
--EXPECTF--
foo
foo

Notice: Array to string conversion in %sbug00032-ze1.php on line 8

Call Stack:
    %f          %d   1. {main}() %sbug00032-ze1.php:0


Notice: Array to string conversion in %sbug00032-ze1.php on line 9

Call Stack:
    %f          %d   1. {main}() %sbug00032-ze1.php:0

foo

Notice: Object to string conversion in %sbug00032-ze1.php on line 11

Call Stack:
    %f          %d   1. {main}() %sbug00032-ze1.php:0


Notice: Object to string conversion in %sbug00032-ze1.php on line 12

Call Stack:
    %f          %d   1. {main}() %sbug00032-ze1.php:0

foo
