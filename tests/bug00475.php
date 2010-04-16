<?php
require 'dbgp/dbgpclient.php';
$data = <<<'NOWDOC'
<?php
$a = array( "example\0key" => "Value", "example" => "value\0key" );
var_dump( $a );
NOWDOC;

$commands = array(
	'step_into',
	'breakpoint_set -t line -n 3',
	'run',
	'property_get -n a',
	'detach'
);

dbgpRun( $data, $commands );
?>
