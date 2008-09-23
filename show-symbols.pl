#!/usr/bin/perl
use warnings;
use strict;
use Elf;

my $file = shift;
die("No file specified") unless $file;
my $e = Elf->new($file);
foreach my $s ($e->defined_symbols) {
    print join(" ", sprintf("0x%08x", $s->value), $s->size, $s->name), "\n";
}
    
