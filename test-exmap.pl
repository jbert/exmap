#!/usr/bin/perl
use strict;
use warnings;
use Test::More tests => 116;

use constant PAGE_SIZE => 4096;

use_ok('Exmap');

$ENV{LD_LIBRARY_PATH} .= ":";
my $SA_EXE = "./sharedarray";
my $SA_LIB = "libsharedarray.so"; # Not ./ since we want to pattern match
my $MI_EXE = "./mapit";
my $MI_DAT = "./mapit.dat";

my %SA_SYMBOLS = (
		  donttouch => {
				mapped_fraction => 0,
				private => 1,
			       },
		  readme => {
			     mapped_fraction => 1,
			     private => 0,
			    },
		  writeme => {
			      mapped_fraction => 1,
			      private => 1,
			     },
		  readhalf => {
			       mapped_fraction => 0.5,
			       private => 0,
			      },
		  writehalf => {
				mapped_fraction => 0.5,
				private => 1,
			       },
		 );

foreach my $sym (keys %SA_SYMBOLS) {
    $SA_SYMBOLS{"uninit_$sym"} = $SA_SYMBOLS{$sym};
}

my $NUM_INSTANCES = 10;

# These need to match sharedarray.[ch] (ok, could read them from there)
my $NUM_ARRAYS = 10;
my $ARRAY_SIZE = 100 * PAGE_SIZE;

ok(-x $SA_EXE, "can find $SA_EXE executable");
ok(-f $SA_LIB, "can find $SA_LIB library");

my @fhs = start_instances($SA_EXE, $NUM_INSTANCES);
push @fhs, start_instances($MI_EXE, $NUM_INSTANCES);

# ------------------------------------------------------------
# Load the data
my $exmap = Exmap->new;
ok($exmap, "can create instance");
is($exmap->num_procs, 0, "zero processes at start");
ok($exmap->load, "can load");
my $num_procs = $exmap->num_procs;
ok($num_procs > 0, "some processes after load");

my @procs = grep { $_->exe_name =~ /$SA_EXE$/ } $exmap->procs;
is(scalar @procs, $NUM_INSTANCES, "can find all our procs");

my $proc = $procs[0];
ok($proc->vm_size > $NUM_ARRAYS * $ARRAY_SIZE, "image is big enough");
my $ps_size = get_pid_size_from_ps($proc->pid);
# SKIP - hmm....since we added [vdso] in, they don't match
# what to do?
SKIP: {
    skip ("[vdso] change has broken ps compatibility - what to do?", 1);
    is($proc->vm_size, $ps_size, "exmap info matches ps output");
}

my $mapped_size = $proc->mapped_size;
my $effective_size = $proc->effective_size;

ok($mapped_size > 0, "nonzero mapped size");
ok($effective_size > 0, "nonzero effective size");
ok($effective_size < $mapped_size, "effective is smaller than mapped");

my @files = $exmap->files;
ok(@files > 0, "can find some mapped file");
@files = grep { $_->name =~ /$SA_LIB$/ } @files;
is(scalar @files, 1, "the file is only listed once");
my $file = shift @files;

ok($file->is_elf, "file recognised as an elf file");

# All our procs should be mapping our shared lib
foreach my $proc (@procs) {
    my $size_for_file_in_proc = $proc->vm_size($file);
    my $arrays_size = $NUM_ARRAYS * $ARRAY_SIZE;
    my $delta = abs($size_for_file_in_proc - $arrays_size) / $arrays_size;
    # Not exact, because there will be some function code etc.
    ok($delta < 0.01, "Shared lib has correct size in each proc");
}

my $text = $file->elf->section_by_name(".text");
ok($text, "can find text section");
ok($text->size > 0, "text section has nonzero size");
is($text->size, $procs[0]->mapped_size($file, $text->mem_range),
   "all of lib text is mapped");

my $bss = $file->elf->section_by_name(".bss");
ok($bss, "can find bss section");

my $data = $file->elf->section_by_name(".data");
ok($data, "can find data section");

is($data->size, $bss->size, "data and bss sizes the same");
my $delta = $procs[0]->mapped_size($file, $data->mem_range)
    - $procs[0]->mapped_size($file, $bss->mem_range);

ok($delta <= PAGE_SIZE, "data and bss mapped are within a page");


# Find how many arrays-worth will be mapped in .bss
my @bss_syms = grep { $_ =~ /uninit_/ } keys %SA_SYMBOLS;
my $mapped_number_of_arrays = 0;
foreach my $sym (@bss_syms) {
    $mapped_number_of_arrays += $SA_SYMBOLS{$sym}->{mapped_fraction};
}
my $arrays_size = $ARRAY_SIZE * $mapped_number_of_arrays;

foreach my $proc (@procs) {
    my $mapped_size_for_file_in_proc
	= $proc->mapped_size($file, $bss->mem_range);
    is($mapped_size_for_file_in_proc, $arrays_size,
       "correct mapped size in bss")
}


foreach my $sym_name (keys %SA_SYMBOLS) {
    my $sym = $file->elf->symbol_by_name($sym_name);
    ok($sym, "can find symbol $sym_name");
    is($sym->size, $ARRAY_SIZE, "symbol $sym_name has correct size");
    my $mapped_size = $procs[0]->mapped_size($file, $sym->range);
    is($mapped_size, $SA_SYMBOLS{$sym_name}->{mapped_fraction} * $ARRAY_SIZE,
       "symbol $sym_name has correct mapped size");

    # Uninitialised space which is only read appears to be shared.
    # This is cool from a memusage point of view, but it means that it is
    # shared among nearly all running procs to varying degrees. So we can't
    # really account for it.
    next if ($sym_name =~ /^uninit_read/);

    my $effective_size
	= $procs[0]->effective_size($file, $sym->range);
    my $expected_esize
	= $SA_SYMBOLS{$sym_name}->{mapped_fraction} * $ARRAY_SIZE;
    if (!$SA_SYMBOLS{$sym_name}->{private}) {
	# Shared...
	$expected_esize /= $NUM_INSTANCES;
    }

    # Floating pt calc, so avoid == test
    my $delta = abs($effective_size - $expected_esize);
    ok($delta < 0.001,  "$sym_name has correct esize");
}


# Test non-elf maps
@procs = grep { $_->exe_name =~ /$MI_EXE$/ } $exmap->procs;
is(scalar @procs, $NUM_INSTANCES, "can find all our mapit procs");

@files = $exmap->files;
ok(@files > 0, "can find some mapped files");
@files = grep { $_->name =~ /$MI_DAT$/ } @files;
is(scalar @files, 1, "$MI_DAT file is only listed once");
$file = shift @files;

ok(!$file->is_elf, "$MI_DAT isn't an elf file");
my $mi_data_size = -s $MI_DAT;
ok($mi_data_size > 0, "$MI_DAT isn't empty");
foreach my $proc (@procs) {
    is($proc->vm_size($file), $mi_data_size, "correct data file size");
    is($proc->mapped_size($file), $mi_data_size, "whole data file is mapped");
    is($proc->effective_size($file), $mi_data_size / $NUM_INSTANCES,
       "data file is shared between instances");
    
    
}
stop_instances(@fhs);

exit 0;

# ------------------------------------------------------------

sub stop_instances
{
    my @fhs = shift;
    foreach my $fh (@fhs) {
	print $fh "\n";
	close $fh or die("problem stopping instance : $!");
    }
    return 1;
}

sub start_instances
{
    my $exe = shift;
    my $num_instances = shift;

    my @fhs;

    while ($num_instances-- > 0) {
	my $fh;
	open ($fh, "|$exe")
	    or die("Can't start [$exe] : $!");
	push @fhs, $fh;
    }
    return @fhs;
}

sub get_pid_size_from_ps
{
    my $pid = shift;
    my @lines = `ps -e -o pid,vsz`;
    chomp @lines;
    @lines = grep { /\s*$pid\s/ } @lines;
    return undef unless scalar @lines == 1;
    my ($pspid, $size) = $lines[0] =~ /\s*(\d+)\s+(\d+)$/;
    return $size * 1024;
}
