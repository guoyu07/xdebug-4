<?php
$tf = xdebug_start_trace('/tmp/'. uniqid('xdt', TRUE));

$ar = array('a', 'bb', 'ccc');
$r = array_map('strlen', $ar);

echo gettype($r), "\n";

echo file_get_contents($tf);
unlink($tf);
?>
