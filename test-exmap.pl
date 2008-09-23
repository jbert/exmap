#!/usr/bin/perl
use strict;
use warnings;
use Test::More tests => 29;

use_ok('Exmap');

my $SA_EXE ="./sharedarray";
my $SA_LIB ="libsharedarray.so"; # Not ./ since we want to pattern match
my $NUM_INSTANCES = 10;

# These need to match sharedarray.h (ok, could read them from there)
my $NUM_ARRAYS = 8;
my $ARRAY_SIZE = 409600;

ok(-x $SA_EXE, "can find $SA_EXE executable");
ok(-f $SA_LIB, "can find $SA_LIB library");

my @fhs = start_instances($SA_EXE, $NUM_INSTANCES);

# ------------------------------------------------------------
# Load the data
my $exmap = Exmap->new;
ok($exmap, "can create instance");
is($exmap->num_procs, 0, "zero processes at start");
ok($exmap->load_procs, "can load");
my $num_procs = $exmap->num_procs;
ok($num_procs > 0, "some processes after load");

my @procs = $exmap->procs;

@procs = grep { $_->exe_name =~ /$SA_EXE$/ } @procs;
is(scalar @procs, $NUM_INSTANCES, "can find all our procs");

my $proc = $procs[0];
#print "vm size is ", $proc->vm_size, "\n";
#print "array size is ", $NUM_ARRAYS * $ARRAY_SIZE, "\n";
ok($proc->vm_size > $NUM_ARRAYS * $ARRAY_SIZE, "image is big enough");
my $ps_size = get_pid_size_from_ps($proc->pid);
is($proc->vm_size, $ps_size, "exmap info matches ps output");

#ok($exmap->load_page_info, "can load page info");

my $mapped_size = $proc->mapped_size;
my $effective_size = $proc->effective_size;

#print "mapped size $mapped_size effective size $effective_size\n";
ok($mapped_size > 0, "nonzero mapped size");
ok($effective_size > 0, "nonzero effective size");
ok($effective_size < $mapped_size, "effective is smaller than mapped");


my @files = $exmap->files;
ok(@files > 0, "can find some mapped file");
my ($file) = grep { $_->name =~ /$SA_LIB$/ } @files;
ok($file, "can find our shared lib");

my @per_proc_file_vmas = $file->vmas_for_proc($proc);
ok(scalar @per_proc_file_vmas > 0, "can find some vmas for this process");
ok(scalar @per_proc_file_vmas <= 3, "but not too many");
my @all_vmas_for_file = $file->vmas;
is(scalar @all_vmas_for_file, $NUM_INSTANCES * scalar @per_proc_file_vmas,
   "have correct number of vmas for file overall");

my $all_procs_file_mapped_size = $file->mapped_size;
my $per_proc_file_mapped_size = $file->mapped_size($proc);
ok($all_procs_file_mapped_size > 0, "all procs file mapped size non-zero");
ok($per_proc_file_mapped_size > 0, "per proc file mapped size non-zero");
is($all_procs_file_mapped_size, $NUM_INSTANCES * $per_proc_file_mapped_size,
   "right multiplier for mapped size");

my $all_procs_file_vm_size = $file->vm_size;
my $per_proc_file_vm_size = $file->vm_size($proc);
ok($all_procs_file_vm_size > 0, "all procs file vm size non-zero");
ok($per_proc_file_vm_size > 0, "per proc file vm size non-zero");
is($all_procs_file_vm_size, $NUM_INSTANCES * $per_proc_file_vm_size,
   "right multiplier for vm size");

my $all_procs_file_effective_size = $file->effective_size;
my $per_proc_file_effective_size = $file->effective_size($proc);
ok($all_procs_file_effective_size > 0,
   "all procs file effective size non-zero");
ok($per_proc_file_effective_size > 0, "per proc file effective size non-zero");
is($all_procs_file_effective_size,
   $NUM_INSTANCES * $per_proc_file_effective_size,
   "right multiplier for effective size");

print "per_proc_file_vm_size: $per_proc_file_vm_size\n";
print "per_proc_file_mapped_size: $per_proc_file_mapped_size\n";
print "per_proc_file_effective_size: $per_proc_file_effective_size\n";

ok($per_proc_file_effective_size < $per_proc_file_mapped_size,
   "effective less than mapped");
ok($per_proc_file_mapped_size < $per_proc_file_vm_size,
   "mapped less than vm");

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
