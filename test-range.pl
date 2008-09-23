#!/usr/bin/perl
use strict;
use warnings;
use Test::More tests => 58;

use_ok qw(Range);

my $r = Range->new(0, -1);
ok(!$r, "Can't have reversed range");
$r = Range->new(0, 0);
ok($r, "Can have empty range 1");

$r = Range->new(3, 3);
ok($r, "Can have empty range 2");
is($r->start, 3, "check start");
is($r->end, 3, "check end");
is($r->size, 0, "check zero length size");
ok(!$r->contains(3), "check doesn't contain start pt (since zero length)");
ok(!$r->contains(0), "check doesn't contain below");
ok(!$r->contains(4), "check doesn't contain above");
ok(!$r->overlaps($r), "zero-length range doesn't self-overlap");

$r = Range->new(2, 6);
is($r->start, 2, "check start");
is($r->end, 6, "check end");
is($r->size, 4, "check length size");
ok($r->contains(2), "check contains start pt");
ok(!$r->contains(6), "check doesn't contain end");
ok($r->contains(4), "check contains within");

ok($r->overlaps($r), "check self-overlaps");
ok($r->overlaps(Range->new(2, 2)), "overlap1a");
ok(!$r->overlaps(Range->new(0, 0)), "overlap1");
ok(!$r->overlaps(Range->new(6, 6)), "overlap2");
ok(!$r->overlaps(Range->new(7, 8)), "overlap3");
ok(!$r->overlaps(Range->new(6, 8)), "overlap4");

ok(!$r->overlaps(Range->new(1, 2)), "overlap5");
ok($r->overlaps(Range->new(1, 3)), "overlap6");
ok($r->overlaps(Range->new(2, 3)), "overlap7");
ok($r->overlaps(Range->new(3, 4)), "overlap8");
ok($r->overlaps(Range->new(3, 6)), "overlap9");
ok($r->overlaps(Range->new(3, 7)), "overlap10");

ok($r->overlaps(Range->new(0, 10)), "overlap11");

ok($r->equals(Range->new(2, 6)), "range equality1");
ok(!$r->equals(Range->new(1, 6)), "range equality2");
ok(!$r->equals(Range->new(2, 7)), "range equality3");

ok(!defined $r->intersect(Range->new(0, 1)), "intersect1");
ok(!defined $r->intersect(Range->new(0, 2)), "intersect2");
ok($r->intersect(Range->new(2, 2))->equals(Range->new(2, 2)), "intersect3");
ok(!defined $r->intersect(Range->new(6, 6)), "intersect4");
ok(!defined $r->intersect(Range->new(6, 7)), "intersect5");
ok(!defined $r->intersect(Range->new(6, 8)), "intersect6");

ok($r->intersect(Range->new(1, 3))->equals(Range->new(2, 3)), "intersect7");
ok($r->intersect(Range->new(1, 9))->equals(Range->new(2, 6)), "intersect7");
ok($r->intersect(Range->new(2, 9))->equals(Range->new(2, 6)), "intersect7");
ok($r->intersect(Range->new(3, 9))->equals(Range->new(3, 6)), "intersect7");
ok($r->intersect(Range->new(3, 4))->equals(Range->new(3, 4)), "intersect7");
ok($r->intersect(Range->new(3, 6))->equals(Range->new(3, 6)), "intersect7");

ok(Range->new(0, 10)->intersect(Range->new(3, 6))->equals(Range->new(3, 6)),
   "intersect7");

ok(Range->new(3, 6)->intersect(Range->new(0, 10))->equals(Range->new(3, 6)),
   "intersect7");

$r->add(3);
is($r->start, 5, "add to range moves start");
is($r->end, 9, "add to range moves end");
is($r->size, 4, "add to range leaves size alone");
$r->subtract(2);
is($r->start, 3, "subtract range moves start");
is($r->end, 7, "subtract range moves end");
is($r->size, 4, "subtract range leaves size alone");


$r->truncate_below(2);
ok($r->equals(Range->new(3, 7)), "truncate_below below the range");
$r->truncate_below(3);
ok($r->equals(Range->new(3, 7)), "truncate_below at the start");
$r->truncate_below(4);
ok($r->equals(Range->new(4, 7)), "truncate_below in the middle");
$r->truncate_below(8);
ok($r->equals(Range->new(8, 8)), "truncate_below above the end");
$r = Range->new(3, 7);
$r->truncate_below(7);
ok($r->equals(Range->new(7, 7)), "truncate_below at the end");
