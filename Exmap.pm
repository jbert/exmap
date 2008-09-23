use Elf;
use strict;
use warnings;

package PagePool;

# All pages are registered here, to detect sharing.
my %PAGE_POOL;

sub clear_page_pool
{
    %PAGE_POOL = ();
#    print "C:\n";
}

sub register_page_in_pool
{
    my $page = shift;
    my $vma = shift;

    $PAGE_POOL{$page} ||= [];
    push @{$PAGE_POOL{$page}}, $vma;

#    print "R: $page: "
#	.scalar @{$PAGE_POOL{$page}}." ["
#	.scalar (keys %PAGE_POOL)."]\n";
}

sub page_pool_count
{
    my $page = shift;
    my $vmas = $PAGE_POOL{$page};
    
#    print "L: $page: ".join(':', @$vmas)." size ".scalar(@$vmas)."\n";
    
    warn("Unregistered page $page") unless $vmas;
    return scalar @$vmas;
}

# ------------------------------------------------------------
package Obj;

my $LIFETIME_DEBUG = 0;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    print "C: $s\n" if $LIFETIME_DEBUG;
    return $s->_init(@_);
}

sub DESTROY
{
    my $s = shift;
    print "D: $s\n" if $LIFETIME_DEBUG;
}

# ------------------------------------------------------------
package Exmap;
use base qw/Obj/;

sub _init
{
    my $s = shift;
    $s->{_procs} = [];
    $s->{_pid_to_proc} = {};
    return $s;
}

sub load_procs
{
    my $s = shift;

    PagePool::clear_page_pool();
    
    # Don't monitor ourselves, our VMAs etc will change too much as we run
    my @pids = grep { $_ != $$ } $s->_readable_pids();
    my @procs = grep { defined $_ } map { Process->new($_); } @pids;
    
    $s->{_procs} = \@procs;
    $s->{_pid_to_proc} = { map { $_->pid => $_ } @procs };

    $s->_find_files;
    $s->_calculate_effective;
    
    return scalar @procs;
}

sub _find_files
{
    my $s = shift;

    my %unique_files;
    my $find_vma_files_subref = sub {
	my $exmap = shift;
	my $proc = shift;
	my $vma = shift;

	my $name = $vma->info('file');
	warn ("VMA $vma without file") unless $name;
	if (!exists $unique_files{$name}) {
	    $unique_files{$name} = File->new($name);
	}
	$unique_files{$name}->add_vma($vma);
	$unique_files{$name}->add_proc($proc);
    };

    unless ($s->_walk_proc_vmas($find_vma_files_subref)) {
	warn("Failed to walk vmas to find files");
	return undef;
    }

    $s->{_files} = \%unique_files;
    return scalar keys %unique_files;
}

sub _calculate_effective
{
    my $s = shift;

    my $update_effective_subref = sub {
	my $exmap = shift;
	my $proc = shift;
	my $vma = shift;
	return 1 if $vma->is_vdso;

	$vma->_update_effective_count;
    };
    # And then work out the sharing
    unless ($s->_walk_proc_vmas($update_effective_subref)) {
	warn("Failed to walk vmas to update effective counts");
	return undef;
    }

    return 1;
}

# Loop through all procs, and the vmas of each proc, calling the subref
# with each vma (and proc and exmap)
sub _walk_proc_vmas
{
    my $s = shift;
    my $subref = shift;
    
    my @pids = @_;
    @pids = $s->pids unless @pids;

    foreach my $pid (@pids) {
	my $proc = $s->{_pid_to_proc}->{$pid};
	unless ($proc) {
	    warn("Can't find proc for pid $pid");
	    next;
	}
	foreach my $vma ($proc->vmas) {
	    $subref->($s, $proc, $vma)
		or return undef;
	}
    }

    return 1;
}

sub total_pages
{
    my $s = shift;
    return scalar values %{$s->{_addr_to_page}};
}

sub procs { return @{$_[0]->{_procs}}; }
sub files { return values %{$_[0]->{_files}}; }
sub elf_files { return grep { $_->is_elf } $_[0]->files; }

sub pids
{
    my $s = shift;
    return keys %{$s->{_pid_to_proc}};
}

sub pid_to_proc
{
    my $s = shift;
    my $pid = shift;
    return $s->{_pid_to_proc}->{$pid};
}

sub name_to_file
{
    my $s = shift;
    my $fname = shift;
    return $s->{_files}->{$fname};
}

sub num_procs
{
    my $s = shift;
    return scalar($s->procs);
}

sub _all_pids
{
    my $s = shift;
    my @pids = map { s!^/proc/!!; $_; } glob "/proc/[0-9]*";
    return @pids;
}

sub _readable_pids
{
    my $s = shift;
    return grep { -r "/proc/$_/mem" } $s->_all_pids();
}


# ------------------------------------------------------------
package Process;
use base qw/Obj/;

use constant EXMAP_FILE => "/proc/exmap";

sub _init
{
    my $s = shift;
    $s->{_pid} = shift;
    $s->_load_maps or return undef;
    $s->_load_page_info or return undef;
    $s->_calculate or return undef;
    return $s;
}

sub pid { return $_[0]->{_pid}; }
sub exe_name { return $_[0]->{_exe_name}; }
sub vmas { return @{$_[0]->{_vmas}}; }
sub filenames { return @{$_[0]->{_fnames}}; }

sub owns_vma
{
    my $s = shift;
    my $supplied_vma = shift;
    foreach my $vma ($s->vmas) {
	# Objects stringify to a value containing their address and class,
	# so string comparison is what we want here
	return 1 if $vma eq $supplied_vma;
    }
    return 0;
}

sub _load_maps
{
    my $s = shift;

    my $mapfile = "/proc/".$s->pid."/maps";
    unless (open (M, "< $mapfile")) {
	warn("Can't open mapfile $mapfile: $!");
	return undef;
    }
    my @map_lines = <M>;
    close M;
    return undef unless @map_lines > 0;

    my @vmas;
    foreach my $line (@map_lines) {
	$line =~ s/\r?\n$//;
	my $vma = Vma->new($line);
	push @vmas, $vma;
    }
    $s->{_vmas} = \@vmas;
    
    return $s;
}

sub _load_page_info
{
    my $s = shift;

    # Ask exmap about our pid
    unless (open(E, "> ".EXMAP_FILE)) {
	warn("can't open ".EXMAP_FILE." for writing : $!");
	return undef;
    }
    print E $s->pid, "\n";
    close E;

    # And read back what exmap has to say
    unless (open (E, "< ".EXMAP_FILE)) {
	warn("can't open ".EXMAP_FILE." for reading : $!");
	return undef;
    }
    my @exmap_lines = <E>;
    close E;

    my $current_vma;
    foreach my $line (@exmap_lines) {
	chomp $line;
	# Lines are either:
	# Start a new VMA:
	# VMA 0xdeadbeef <npages>
	# or
	# Page info
	# <pfn> <pidmapped> <mapcount>
	if ($line =~ /^VMA/) {
	    # New VMA
	    my ($vma_hex_start, $npages) = $line =~ /^VMA\s+0x(\S+)\s+(\d+)$/;
	    my $vma = $s->_find_vma_by_hex_addr($vma_hex_start);
	    unless ($vma) {
		# TODO - try reload completely here?
		warn("PID ", $s->pid, " can't find VMA $vma_hex_start");
		return undef;
	    }
	    $current_vma = $vma;
	    $current_vma->clear_pages;
	}
	else {
	    my ($pfn, $mapped, $mapcount) = split(/\s+/, $line);
	    # Ignore the zero pfn
	    next if hex ($pfn) == 0;
	    unless ($current_vma) {
		warn("Found page with no current vma");
		return undef;
	    }
	    $current_vma->register_page($pfn, $mapped, $mapcount);
	}
    }

    return 1;
}

sub _find_vma_by_hex_addr
{
    my $s = shift;
    my $hex_addr = shift;

    foreach my $vma ($s->vmas) {
	return $vma if $vma->info('hex_start') eq $hex_addr;
    }
    return undef;
}

sub _calculate
{
    my $s = shift;

    my @vmas = $s->vmas;
    # First VMA maps the exe
    $s->{_exe_name} = $vmas[0]->info('file');

    my %unique_fnames;
    foreach my $vma (@vmas) {
	$unique_fnames{$vma->info('file')}++;
    }

    $s->{_fnames} = [ keys %unique_fnames ];
    return 1;
}

sub _sum_vma_info_over_file_or_all
{
    my $s = shift;
    my $key = shift;
    my $file = shift;
    
    my $total = 0;
    foreach my $vma ($s->vmas) {
	# Ignore the pseudo-page if present
	next if $vma->is_vdso;
	# Ignore the vma if we are narrowing to a specific file
	next if ($file && $vma->info('file') ne $file->name);
	my $val = $vma->info($key);
	die("undefined val for $key in $vma") unless defined $val;
	$total += $val;
    }
    return $total;
}

sub vm_size
{
    my $s = shift;
    my $file = shift;
    return $s->_sum_vma_info_over_file_or_all('vm_size', $file);
}

sub mapped_size
{
    my $s = shift;
    my $file = shift;
    return $s->_sum_vma_info_over_file_or_all('mapped_size', $file);
}

sub effective_size
{
    my $s = shift;
    my $file = shift;
    return $s->_sum_vma_info_over_file_or_all('effective_size', $file);
}

# ------------------------------------------------------------
package Vma;
use base qw/Obj/;

use constant ANON_NAME => "[anon]";
use constant PAGE_SIZE => 4096;

sub _init
{
    my $s = shift;
    $s->{_line} = shift;
    $s->_parse_line or return undef;
    $s->_calculate_info or return undef;
    return $s;
}

sub _parse_line
{
    my $s = shift;
    
    my $line = $s->{_line};

    my %info;

    @info{qw(hex_start hex_end perms hex_offset)}
    = ($line =~ /^([0-9a-f]+)-([0-9a-f]+)\s+(\S+)\s+([0-9a-f]+)/);

    if (not defined $info{hex_start}) {
	warn("Can't parse line [$line]");
	return undef;
    }
    $info{file} = substr($line, 49) if length $line >= 49;
    
    $info{file} ||= ANON_NAME;

    $s->{_info} = \%info;

    return 1;
}

sub _calculate_info
{
    my $s = shift;

    my $info = $s->{_info};

    foreach my $key (qw/start end offset/) {
	$info->{$key} = hex $info->{"hex_$key"};
    }
    $info->{vm_size} = $info->{end} - $info->{start};

    return 1
}

sub info
{
    my $s = shift;
    my $key = shift;
    return $s->{_info}->{$key};
}

# Newer kernels have an unreadable page at 0xfffe000. We want to exclude
# this from most calculations.
sub is_vdso
{
    my $s = shift;
    return $s->info('hex_start') eq "ffffe000"
	&& $s->info('hex_end') eq "fffff000";
}

sub clear_pages
{
    my $s = shift;
    $s->{_pages} = undef;
    $s->{_info}->{mapped_size} = 0;
    $s->{_info}->{effective_size} = 0;
}

sub register_page
{
    my $s = shift;
    my $page = shift;
    my $mapped = shift;
    my $mapcount = shift;

    $s->{_info}->{mapped_size} += PAGE_SIZE;
    PagePool::register_page_in_pool($page, $s);
    $s->{_pages}->{$page}++;
    
    return 1;
}

sub _update_effective_count
{
    my $s = shift;

    my $effective_pages = 0;
    foreach my $page (keys %{$s->{_pages}}) {
	my $count = PagePool::page_pool_count($page);
	die("Page $page has a zero count") unless $count > 0;
	$effective_pages += 1 / $count;
    }
    $s->{_info}->{effective_size} = $effective_pages * PAGE_SIZE;
    return 1;
}

# ------------------------------------------------------------
package File;
use base qw/Obj/;

sub _init
{
    my $s = shift;
    $s->{_name} = shift;
    $s->{_vmas} = [];
    $s->{_procs} = [];
    if (-f $s->name) {
	$s->{_elf} = Elf->new($s->name,
			      1); # Suppress warning if not elf
    }
    return $s;
}

sub name { return $_[0]->{_name}; }
sub vmas { return @{$_[0]->{_vmas}}; }
sub procs { return @{$_[0]->{_procs}}; }
sub elf { return $_[0]->{_elf}; }
sub is_elf { return $_[0]->elf; } # Mmmm. Sugary.

sub vmas_for_proc
{
    my $s = shift;
    my $proc = shift;

    return grep { $proc->owns_vma($_) } $s->vmas;
}

sub add_vma
{
    my $s = shift;
    my $vma = shift;
    
    my %unique = map { $_ => 1 } $s->vmas;
    push @{$s->{_vmas}}, $vma unless $unique{$vma};
    return 1;
}

sub add_proc
{
    my $s = shift;
    my $proc = shift;

    my %unique = map { $_ => 1 } $s->procs;
    push @{$s->{_procs}}, $proc unless $unique{$proc};
    
    return 1;
}

sub _sum_vma_info_over_proc_or_all
{
    my $s = shift;
    my $key = shift;
    my $proc = shift;

    my $total = 0;
    my @vmas = $proc ? $s->vmas_for_proc($proc) : $s->vmas;
    foreach my $vma (@vmas) {
	next if $vma->is_vdso;
	$total += $vma->info($key);
    }
    return $total;
}

sub vm_size
{
    my $s = shift;
    my $proc = shift;
    return $s->_sum_vma_info_over_proc_or_all('vm_size', $proc);
}

sub mapped_size
{
    my $s = shift;
    my $proc = shift;
    return $s->_sum_vma_info_over_proc_or_all('mapped_size', $proc);
}

sub effective_size
{
    my $s = shift;
    my $proc = shift;
    return $s->_sum_vma_info_over_proc_or_all('effective_size', $proc);
}


1;
