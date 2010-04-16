<?php
function error_handler($errno, $string, $file, $line, $context)
{
	$entry = Error_Class::newError();
}

class Error_Class
{
	public static function newError($errno = false)
	{
		return new Error_Entry(false, $errno);
	}

	public static function getBT()
	{
		$tmp = xdebug_get_function_stack();
		var_dump($tmp);
	}

}

class Error_Entry
{
	public function __construct($base, $errno)
	{
		Error_Class::getBT();
	}
}

set_error_handler('error_handler');

$tmp = explode('/', trim($_SERVER['FOO'], '/'));
echo "The End\n";
?>
