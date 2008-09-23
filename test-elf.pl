#!/usr/bin/perl
use strict;
use warnings;
use Test::More tests => 99;
use_ok('Elf');

my %TESTDAT = (
	       `which ls` => {
		   type => 'EXEC',
		   exe => 1,
		   num_symbols => 0,
	       },
	       '/usr/lib/libz.so' => {
		   type => 'DYN',
		   exe => '',
		   num_symbols => 0,
	       },
	       );

my @BUILDFILES = ({
    source_name => 'hello-world.c',
    built_name => 'hello-world',
    build_cmd => 'gcc -o hello-world hello-world.c',
    testdat => {
	type => 'EXEC',
	exe => 1,
    },
    text => <<'END_HW',
    #include <stdio.h>
    int main(void)
    {
	return 0;
    }
END_HW
});
    

my $e = Elf->new;
ok(!defined($e), "must supply filename");
$e = Elf->new('/doesnotexist', 1); # Suppress warning to stderr
ok(!defined($e), "must supply file which exists");
my $not_elf = '/etc/motd';
ok(-f $not_elf, "non-elf file exists");
$e = Elf->new($not_elf, 1); # Suppress warning to stderr
ok(!defined($e), "must supply file which is ELF");

create_build_files();

foreach my $f (keys %TESTDAT) {
    my $td = $TESTDAT{$f};
    chomp $f;
    $e = Elf->new($f);
    ok($e, "can instantiate on $f");
    is($e->file, $f, "correct file name");
    is($e->e_type, $td->{type}, "has right file type");
    is($e->is_executable, $td->{exe}, "file $f is " .
       ($td->{exe} ? '' : 'not ')
       . "an exe");

    my @lines = `objdump -h $f | tail +6`;
    $td->{num_sections} = (scalar @lines) / 2;
    
    is($e->num_sections, $td->{num_sections}, "mainstream sections match objdump");

    foreach my $s ($e->mainstream_sections) {
	my $objdump_name = shift @lines;
	shift @lines; #Discard next
	$objdump_name =~ s/\r?\n$//;
	$objdump_name =~ s/^\s*\d+\s+//;
	$objdump_name =~ s/\s.*$//;
	is($s->name, $objdump_name, "section names match objdump $objdump_name");
    }

    my @symbols = $e->defined_symbols;
    is(scalar(@symbols), $td->{num_symbols}, "correct number of symbols");
}

remove_build_files();

exit 0;


sub create_build_files
{
    foreach my $bf (@BUILDFILES) {
	my $sname = $bf->{source_name};
	my $name = $bf->{built_name};
	unless(open(F, "> $sname")) {
	    warn("Can't open $sname");
	    next;
	}
	print F $bf->{text};
	close F;
	unless (system($bf->{build_cmd}) == 0) {
	    warn("Can't build $sname");
	    unlink $sname;
	    next;
	}
	unless (-f $name) {
	    warn("Build didn't create $name");
	    unlink $sname;
	    next;
	}
	$TESTDAT{$name} = $bf->{testdat};
	$TESTDAT{$name}->{num_symbols} = nm_count_symbols($name);
    }
    
}

sub remove_build_files
{
    foreach my $bf (@BUILDFILES) {
	unlink $bf->{source_name};
	unlink $bf->{built_name};
    }
}

sub nm_count_symbols
{
    my $exe = shift;
    my @lines = `nm $exe`;
    @lines = grep { !/ (w|U) / } @lines;
    return scalar(@lines);
}
