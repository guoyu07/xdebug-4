<?php
require 'dbgp/dbgpclient.php';
$data = <<<'NOWDOC'
<?php
class abc {
        private $arr;
        public function abc(){
                $this->arr = array(
                        0 => array("some", "values"),
                        1 => array("some", "more", "values")
                );
        }
}

class def extends abc {
        private $arr;
}

$o = new def;
echo "o: ";
var_dump($o);
NOWDOC;

$commands = array(
	'step_into',
	'breakpoint_set -t line -n 17',
	'run',
	'property_get -n o',
	'detach'
);

dbgpRun( $data, $commands );
?>
