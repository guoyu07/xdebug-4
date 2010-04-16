<?php
	$tf = xdebug_start_trace('/tmp/'. uniqid('xdt', TRUE));

	function foo($a)
	{
		return $a;
	}

	$array = array("te\"st's" => 42);
	foo($array);

	echo file_get_contents($tf);
	unlink($tf);
?>
