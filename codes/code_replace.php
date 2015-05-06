<?php

$src_file = "code";
$dest_file = $argv[1];

if (empty($dest_file)) {
  echo 'not filename...' . PHP_EOL;
  exit();
}

$file = file_get_contents($src_file, true);

$pattern = '/^\s(.*?)\n(.*)/';
$replacement = '$2';
$file = preg_replace($pattern, $replacement, $file);

$pattern = '/0.*?:\s(.*)/';
$replacement = '$1';
$file = preg_replace($pattern, $replacement, $file);

$pattern = '/\s/';
$replacement = '';
$file = preg_replace($pattern, $replacement, $file);

$pattern = '/\n/';
$replacement = '';
$file = preg_replace($pattern, $replacement, $file);

$pattern = '/([0-9A-f]{2})/';
$replacement = '0x$1, ';
$file = preg_replace($pattern, $replacement, $file);

file_put_contents($dest_file, $file);
