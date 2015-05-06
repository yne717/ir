<?php

$src_file = "code";
$dest_file = $argv[1];

if (empty($dest_file)) {
  echo 'not filename...' . PHP_EOL;
  exit();
}

$file = file_get_contents($src_file, true);

$pattern = '/       .*?\n(.*?))/';
$replacement = '$1';
$file = preg_replace($pattern, $replacement, $file);

file_put_contents($dest_file, $file);
