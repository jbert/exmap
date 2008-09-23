use Elf;
use strict;
use warnings;

my $DEBUG_ON = 0;

sub debug
{
    print STDERR join(":", @_), "\n" if $DEBUG_ON;
}

# ------------------------------------------------------------
package Obj;

my $OBJ_LIFETIME_DEBUG = 0;

sub _init { return shift; }

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    print "C: $s\n" if $OBJ_LIFETIME_DEBUG;
    return $s->_init(@_);
}

sub DESTROY
{
    my $s = shift;
    print "D: $s\n" if $OBJ_LIFETIME_DEBUG;
}

    
# ------------------------------------------------------------
package PagePool;
use base qw/Obj/;

# TODO - we don't need to have a list of VMAs, do we? A simple count
# would suffice...
sub clear
{
    my $s = shift;
    %$s = ();
    #    print "C:\n";
}

sub register_page
{
    my $s = shift;
    my $pfn = shift;
    my $vma = shift;

    $s->{$pfn} ||= [];
    push @{$s->{$pfn}}, $vma;

    #    print "R: $page: "
    #	.scalar @{$s->{$page}}." ["
    #	.scalar (keys %$s)."]\n";
}

sub page_count
{
    my $s = shift;
    my $pfn = shift;

    unless (defined $pfn) {
	my @callinfo = caller;
	warn("Undefined pfn passed in from $callinfo[1] line $callinfo[2]");
	return undef;
    }
    
    my $vmas = $s->{$pfn};
    
    #    print "L: $page: ".join(':', @$vmas)." size ".scalar(@$vmas)."\n";
    
    unless ($vmas) {
	warn("Unregistered page $pfn");
	return undef;
    }
    return scalar @$vmas;
}

# ------------------------------------------------------------
package FilePool;
use base qw/Obj/;

sub clear
{
    my $s = shift;
    %$s = ();
}

sub name_to_file
{
    my $s = shift;
    my $fname = shift;
    return $s->{$fname};
}

sub get_or_make_file
{
    my $s = shift;
    my $fname = shift;

    my $file = $s->name_to_file($fname);
    return $file if $file;
    $s->{$fname} = File->new($fname);
    return $s->{$fname};
}

sub files
{
    my $s = shift;
    return values %$s;
}

# ------------------------------------------------------------
package Exmap;
use base qw/Obj/;

sub _init
{
    my $s = shift;
    $s->{_procs} = [];
    $s->{_files} = {};		# filename -> File
    $s->{_pid_to_proc} = {};
    $s->{_page_pool} = PagePool->new;
    $s->{_file_pool} = FilePool->new;
    return $s;
}

sub procs { return @{$_[0]->{_procs}}; }
sub page_pool { return $_[0]->{_page_pool}; }
sub file_pool { return $_[0]->{_file_pool}; }
sub files { return $_[0]->file_pool->files; }
sub elf_files { return grep { $_->is_elf } $_[0]->files; }

sub total_pages
{
    my $s = shift;
    return scalar values %{$s->{_addr_to_page}};
}

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

sub load
{
    my $s = shift;

    $s->_load_procs
	or return undef;

    $s->_calculate_file_mappings
	or return undef;

    return 1;
}

sub _load_procs
{
    my $s = shift;

    my $pp = $s->page_pool;
    $pp->clear;
    
    # Don't monitor ourselves, our VMAs etc will change too much as we run
    my @pids = grep { $_ != $$ } $s->_readable_pids();

    my @procs;
    foreach my $pid (@pids) {
	my $proc = Process->new($pid);
	unless ($proc->load($pp)) {
	    warn("Can't load info for pid $pid");
	    next;
	}
	push @procs, $proc;
    }
    
    $s->{_procs} = \@procs;
    $s->{_pid_to_proc} = { map { $_->pid => $_ } @procs };
    
    return scalar @procs;
}

sub _calculate_file_mappings
{
    my $s = shift;

    foreach my $proc ($s->procs) {
	$proc->_calc_file_maps($s->file_pool);
    }
    return scalar $s->files;
}


# ------------------------------------------------------------
package Process;
use base qw/Obj/;

use constant EXMAP_FILE => "/proc/exmap";

sub _init
{
    my $s = shift;
    $s->{_pid} = shift;
    $s->{_exe_name} = readlink "/proc/$s->{_pid}/exe";
    return $s;
}

sub load
{
    my $s = shift;
    my $page_pool = shift;
    $s->_load_vmas($page_pool) or return undef;
    $s->_load_page_info($page_pool) or return undef;
    return 1;
}

sub pid { return $_[0]->{_pid}; }
sub exe_name { return $_[0]->{_exe_name}; }
sub _vmas { return @{$_[0]->{_vmas}}; }
sub maps { return @{$_[0]->{_maps}}; }
sub files { return @{$_[0]->{_files}}; }

sub _find_vma_by_hex_addr
{
    my $s = shift;
    my $hex_addr = shift;

    foreach my $vma ($s->_vmas) {
	return $vma if $vma->info('hex_start') eq $hex_addr;
    }
    return undef;
}

sub add_file
{
    my $s = shift;
    my $file = shift;

    $s->{_files} ||= [];
    push @{$s->{_files}}, $file
	unless grep { $_ eq $file } @{$s->{_files}};
}

sub _has_file
{
    my $s = shift;
    my $file = shift;

    my @matches = grep { $_ eq $file } $s->files;
    warn("File ", $file->name, " present in process ", $s->pid,
	 " more than once")
	if (scalar @matches > 1);
    return scalar @matches == 1;
}

sub _restrict_maps_to_file
{
    my $s = shift;
    my $file = shift;
    my @maps = @_;

    unless ($file) {
	warn("No file to specified");
	return ();
    }
    unless ($s->_has_file($file)) {
	warn("PID ", $s->pid, " doesn't have file ", $file->name);
	return ();
    }

    my %count;
    my @file_maps = $file->maps;
    foreach my $map (@maps, @file_maps) {
	$count{$map}++;
    }
    # Only keep those in both arrays.
    @maps = grep { $count{$_} > 1 } @maps;

    return @maps;
}

sub _refine_maps_to_elf_range
{
    my $s = shift;
    my $elf_range = shift;
    my @maps = @_;

    return () unless $elf_range->size > 0;

    my @refinements;
    
    foreach my $map (@maps) {
	if ($map->elf_range
	    && $map->elf_range->overlaps($elf_range)) {
	    my $subrange = $elf_range->intersect($map->elf_range);
	    my $mem_range = $map->elf_to_mem_range($subrange);
	    push @refinements, { map => $map,
				 range => $mem_range };
	}
    }

    unless (@refinements) {
	my $warnstr = $s->pid . ": no map refinements for elf range "
	    . $elf_range->to_string. ": "
		. join(", ", map { $_->elf_range
				       ? $_->elf_range->to_string
				   : "undef" } @maps);
	warn($warnstr);
    }
	    

    if (@refinements > 1) {
	# Check there are no holes...
	my $addr;
	warn("first refinement doesn't reach end of map")
	    unless $refinements[0]->{range}->end
		== $refinements[0]->{map}->mem_range->end;
	warn("last refinement doesn't start at start of map")
	    unless $refinements[-1]->{range}->start
		== $refinements[-1]->{map}->mem_range->start;

	my @middle = @refinements;
	shift @middle;
	pop @middle;
	foreach my $r (@middle) {
	    warn("a middle refinement doesn't span the map range")
		unless $r->{range}->equals($r->{map}->mem_range);
	}
    }
    
    return @refinements;
}


sub _sum_map_info_over_file_and_elf_range
{
    my $s = shift;
    my $key = shift;
    my $file = shift;
    my $elf_range = shift;
    
    my $total = 0;

    my @maps = $s->maps;
    warn ("No maps in process", $s->pid) unless @maps;
    if ($file) {
	@maps = $s->_restrict_maps_to_file($file, @maps);
	warn ("No maps for file " . $file->name . " in process ", $s->pid)
	    unless @maps;
    }

    # A list of { map => $map, range => $mem_range }. Undef range implies
    # full map.
    my @refinements = map { { map => $_,
				  range => undef } } @maps;

    if ($elf_range) {
	return 0 if ($elf_range->size == 0);
	@refinements = $s->_refine_maps_to_elf_range($elf_range, @maps);
    }
    
    foreach my $refinement (@refinements) {
	my $val = $refinement->{map}->size_for_mem_range($key,
							 $refinement->{range});
	die("undefined val for $key in $refinement->{map}")
	    unless defined $val;
	$total += $val;
    }
    return $total;
}

# The '_size' functions take an optional 'file' parameter, which may
# also be followed by an 'elf_range' parameter.
sub vm_size
{
    my $s = shift;
    return $s->_sum_map_info_over_file_and_elf_range('vm_size', @_);
}

sub mapped_size
{
    my $s = shift;
    return $s->_sum_map_info_over_file_and_elf_range('mapped_size', @_);
}

sub effective_size
{
    my $s = shift;
    return $s->_sum_map_info_over_file_and_elf_range('effective_size', @_);
}


sub _load_vmas
{
    my $s = shift;
    my $page_pool = shift;
    
    my $mapfile = "/proc/".$s->pid."/maps";
    unless (open (M, "< $mapfile")) {
	warn("Can't open mapfile $mapfile: $!");
	return undef;
    }
    my @map_lines = <M>;
    close M;
    unless (@map_lines > 0) {
	# Don't warn. If we run as root we'll pick up a lot of these,
	# basically kernel threads without an mm_struct (empty
	# /proc/x/maps).
	#	warn("read zero maplines for ", $s->pid);
	return undef;
    }

    my @vmas;
    foreach my $line (@map_lines) {
	$line =~ s/\r?\n$//;
	my $vma = Vma->new($page_pool);
	unless ($vma->parse_line($line)) {
	    warn("Can't create VMA for line $line");
	    next;
	}
	push @vmas, $vma unless $vma->is_vdso;
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
	# <pfn> <mapcount>

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
	}
	else {
	    my ($pfn, $mapcount) = split(/\s+/, $line);
	    unless ($current_vma) {
		warn("Found page with no current vma");
		return undef;
	    }
	    $current_vma->add_page($pfn, $mapcount);
	}
    }

    return 1;
}


sub _calc_file_maps
{
    my $s = shift;
    my $filepool = shift;

    my @maps; # Accumulate all the per-proc maps in here

    # State variables for the loop. We walk addr up from the first vma->start
    # to the last vma end, consuming vmas and segments as we go. When we switch
    # to a new ELF file, we refresh segments.
    my $addr;
    my @segments;
    my @vmas = $s->_vmas;
    # Adding this to a seg mem address makes it a vma address
    my $seg_addr_offset;

    $addr = $vmas[0]->info('start');

    ::debug("PID " . $s->pid . " starting with " . scalar @vmas . " vmas");
    my $file;
    while (@vmas) {
	my $map;
	my $vma = $vmas[0];
	
	my $vma_start = $vma->info('start');
	$addr = $vma_start if $addr < $vma_start;

	my $vma_end = $vma->info('end');
	if ($addr >= $vma_end) {
	    warn(sprintf "%d: address 0x%08x at or after vma end 0x%08x",
		 $s->pid, $addr, $vma_end);
	    return undef;
	}

	my $vma_fname = $vma->info('file');
	
	# Did we finish the previous?
	if (@segments == 0) {
	    my $newfile = $filepool->get_or_make_file($vma_fname);
	    if ((!$file || $newfile != $file)
		&& $newfile->is_elf) {
		@segments = $newfile->elf->loadable_segments;
		$seg_addr_offset = $vma_start - $segments[0]->mem_range->start;
	    }
	    $file = $newfile;
	}

	::debug(sprintf "looping with addr 0x%08x nvmas %d nsegs %d file %s",
		$addr, scalar @vmas, scalar @segments, $vma_fname);

	# Bind file and proc
	$s->add_file($file);
	$file->add_proc($s);


	# Take out the non-ELF case
	if (@segments == 0) {
	    # Non-elf, so a Map with no elf range. It doesn't necessarily
	    # cover the whole VMA, since we could be an [anon] (or [heap])
	    # map whose first portion is the .bss of a preceding elf.
	    $map = Map->new($vma,
			    Range->new($addr,
				       $vma_end),
			    undef);
	    ::debug("added non-elf map", $map->mem_range->to_string,
		    " ", $map->_vma->info('file') );
	    $addr = $vma_end;
	    goto FOUND_MAP;
	}

	# OK, we are an ELF map (not necessarily an ELF File obj, because
	# we could be into an [anon] map covering .bss, but we'll still be in
	# a segment)
	my $seg = $segments[0];

	my $seg_mem_range = $seg->mem_range->add($seg_addr_offset);
	
	if ($addr < $seg_mem_range->start) {
	    # We've got a hole.
	    my $hole_end = $seg_mem_range->start > $vma_end
		? $vma_end : $seg_mem_range->start;

	    $map = Map->new($vma,
			    Range->new($addr, $hole_end),
			    undef);
	    ::debug("added hole map", $map->mem_range->to_string);
	    $addr = $hole_end;
	    goto FOUND_MAP;
	}
	
	if (!$seg_mem_range->contains($addr)) {
	    warn(sprintf "addr 0x%08x outside of seg_mem_range %s",
		 $addr, $seg_mem_range->to_string);
	    return undef;
	}

	# The map can only extend to the end of the segment or vma.
	# And maps are page-aligned.
	my $end_addr = $seg_mem_range->end;
	$end_addr = $vma_end if ($end_addr > $vma_end);

	my $map_mem_range = Range->new($addr, $end_addr);

	# And this range of the ELF file. (Possibly empty, we may be
	# into .bss)

	# Elf range starts same as the mem range
	# We shift back to seg vaddrs
	my $map_elf_range = $map_mem_range->subtract($seg_addr_offset);

	$map = Map->new($vma, $map_mem_range, $map_elf_range);
	::debug("adding elf map mem", $map->mem_range->to_string,
		" elf ", defined $map->elf_range ? $map->elf_range->to_string : "undef",
	        " ", $map->_vma->info('file') );

	$addr = $end_addr;

    FOUND_MAP:
	$file->add_map($map);
	push @maps, $map;
	# Consume any segments or vmas we have finished with.
	if ($addr >= $vmas[0]->info('end')) {
	    shift @vmas;
	    ::debug("consuming vma");
	}
	
	if (@segments
	    && $addr >= ($segments[0]->mem_range->end + $seg_addr_offset)) {
	    shift @segments;
	    ::debug("consuming segment");
	}
    }
    
    $s->{_maps} = \@maps;
    
    return 1;
}


# ------------------------------------------------------------
package File;
use base qw/Obj/;

sub _init
{
    my $s = shift;
    $s->{_name} = shift;
    $s->{_maps} = [];
    $s->{_procs} = [];
    if (-f $s->name) {
	$s->{_elf} = Elf->new($s->name,
			      1); # Suppress warning if not elf
    }
    return $s;
}

sub name { return $_[0]->{_name}; }
sub procs { return @{$_[0]->{_procs}}; }
sub elf { return $_[0]->{_elf}; }
sub is_elf { return $_[0]->elf; } # Mmmm. Sugary.
sub maps { return @{$_[0]->{_maps}}; }

sub _sum_map_info
{
    my $s = shift;
    my $subref = shift;

    my $total = 0;
    foreach my $map ($s->maps) {
	$total += $subref->($map);
    }
    return $total;
}

sub vm_size
{
    my $s = shift;
    my $subref = sub { my $map = shift;
		       return $map->vm_size_for_mem_range; };
    return $s->_sum_map_info($subref);
}

sub mapped_size
{
    my $s = shift;
    my $subref = sub { my $map = shift;
		       return $map->mapped_size_for_mem_range; };
    return $s->_sum_map_info($subref);
}

sub effective_size
{
    my $s = shift;
    my $subref = sub { my $map = shift;
		       return $map->effective_size_for_mem_range; };
    return $s->_sum_map_info($subref);
}

sub add_map
{
    my $s = shift;
    my $map = shift;
    
    push @{$s->{_maps}}, $map;
    return 1;
}

sub add_proc
{
    my $s = shift;
    my $proc = shift;

    my %existing = map { $_ => 1 } $s->procs;
    push @{$s->{_procs}}, $proc unless $existing{$proc};
    
    return 1;
}


# ------------------------------------------------------------
package Vma;
use base qw/Obj/;

use constant ANON_NAME => "[anon]";
use constant VDSO_NAME => "[vdso]";

sub _init
{
    my $s = shift;
    $s->{_page_pool} = shift;
    $s->{_pfns} = [];
    return $s;
}

sub parse_line
{
    my $s = shift;
    my $line = shift;
    
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

    $s->_calculate_info or return undef;

    return 1;
}

sub _calculate_info
{
    my $s = shift;

    my $info = $s->{_info};

    $info->{start} = hex $info->{'hex_start'};
    $info->{offset} = hex $info->{'hex_offset'};
    $info->{end} = hex($info->{'hex_end'});
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
    return $s->info('file') eq VDSO_NAME
	|| ($s->info('hex_start') eq "ffffe000"
	    && $s->info('hex_end') eq "fffff000");
}

sub add_page
{
    my $s = shift;
    my $pfn = shift;
    my $mapcount = shift;

    # The 'zero' pfn is an absent page
    $pfn = undef if hex($pfn) == 0;
    
    # Record all pages, in order
    push @{$s->{_pfns}}, $pfn;

    return 1 unless $pfn;

    # This page is mapped
    $s->{_info}->{mapped_size} += Elf::PAGE_SIZE;
    $s->{_page_pool}->register_page($pfn, $s);
    
    return 1;
}

sub is_file_backed
{
    my $s = shift;
    # Names like [anon], [heap], and [vdso] don't count as file backed
    return  !($s->info('file') =~ /^\[.*\]$/);
}

sub _addr_to_pgnum
{
    my $s = shift;
    my $addr = shift;

    my $pgnum = Elf::page_align_down($addr);
    $pgnum -= $s->info('start');
    if ($pgnum < 0) {
	warn("$addr is less than vma start " . $s->info('start'));
	return undef;
    }
    $pgnum /= Elf::PAGE_SIZE;
    if ($pgnum >= scalar @{$s->{_pfns}}) {
	warn("$addr is beyond vma end " . $s->info('end')
	     . " ($pgnum >= " . scalar @{$s->{_pfns}});
	return undef;
    }
    
    return $pgnum;
}

sub get_page_counts_for_range
{
    my $s = shift;
    my $range = shift;

    return () unless $range->size > 0;
    
    my $start_pgnum = $s->_addr_to_pgnum($range->start);
    return undef unless defined $start_pgnum;
    my $end_pgnum = $s->_addr_to_pgnum($range->end - 1);
    return undef unless defined $end_pgnum;

    if ($start_pgnum == $end_pgnum) {
	unless (exists $s->{_pfns}->[$start_pgnum]) {
	    warn("Can't find pfn for pgnum $start_pgnum");
	    return undef;
	}
	my $pfn = $s->{_pfns}->[$start_pgnum];
	return ({
		 count => $pfn ? $s->{_page_pool}->page_count($pfn) : 0,
		 fraction => $range->size / Elf::PAGE_SIZE,
		});
    }

    my @pgnums = ($start_pgnum..$end_pgnum);
    my @info;
    foreach my $pgnum (@pgnums) {
	my $pfn = $s->{_pfns}->[$pgnum];
	my $fraction = 1.0;

	# Handle fractional pages at either end
	if ($pgnum == $start_pgnum) {
	    $fraction = (Elf::PAGE_SIZE
			 - ($range->start
			    - Elf::page_align_down($range->start)))
		/ Elf::PAGE_SIZE;
	}
	if ($pgnum == $end_pgnum) {
	    $fraction = ($range->end - Elf::page_align_down($range->end - 1))
		/ Elf::PAGE_SIZE;
	}

	
	if ($pfn) {
	    my $count = $s->{_page_pool}->page_count($pfn);
	    die("PFN $pfn has a zero count") unless $count > 0;
	    push @info, { count => $count,
			  fraction => $fraction };
	}
	else {
	    push @info, { count => 0,
			  fraction => $fraction };
	}
    }

    

    return @info;
}

# ------------------------------------------------------------
package Map;
use base qw/Obj/;

sub _init
{
    my $s = shift;
    $s->{_vma} = shift;
    $s->{_mem_range} = shift;
    $s->{_elf_range} = shift;	# Optional - undef if not file backed

    if ($s->elf_range && $s->mem_range->size != $s->elf_range->size) {
	warn("Mem range != Elf mem range");
	return undef;
    }
    return $s;
}

sub mem_range { return $_[0]->{_mem_range}; }
sub elf_range { return $_[0]->{_elf_range}; }
sub _vma { return $_[0]->{_vma}; }

sub elf_to_mem_offset
{
    my $s = shift;
    return $s->mem_range->start - $s->elf_range->start;
}

sub elf_to_mem_range
{
    my $s = shift;
    my $elf_range = shift;

    unless ($s->elf_range->contains_range($elf_range)) {
	warn("Range " . $elf_range->to_string . " not contained within "
	     . $s->elf_range->to_string);
	return undef;
    }

    return $elf_range->add($s->elf_to_mem_offset);
}

sub effective_size_for_mem_range
{
    my $s = shift;
    my $mrange = shift || $s->mem_range;

    my $subrange = $s->mem_range->intersect($mrange);
    return 0 unless $subrange->size > 0;

    my @info = $s->_vma->get_page_counts_for_range($subrange);

    my $total = 0;
    foreach my $info (@info) {
	if ($info->{count} > 0) {
	    $total += (Elf::PAGE_SIZE / $info->{count}) * $info->{fraction};
	}
    }

    return $total;
}

sub vm_size_for_mem_range
{
    my $s = shift;
    my $mrange = shift || $s->mem_range;
    my $subrange = $s->mem_range->intersect($mrange);
    return $subrange->size;
}

sub mapped_size_for_mem_range
{
    my $s = shift;
    my $mrange = shift || $s->mem_range;

    my $subrange = $s->mem_range->intersect($mrange);
    return 0 unless $subrange->size > 0;

    my @info = $s->_vma->get_page_counts_for_range($subrange);

    my $total = 0;
    foreach my $info (@info) {
	if ($info->{count} > 0) {
	    $total += Elf::PAGE_SIZE * $info->{fraction};
	}
    }

    return $total;
}

# Polymorphism? Whats that?
sub size_for_mem_range
{
    my $s = shift;
    my $key = shift;
    my $range = shift;
    
    if ($key eq "vm_size") {
	return $s->vm_size_for_mem_range($range);
    }
    elsif ($key eq "effective_size") {
	return $s->effective_size_for_mem_range($range);
    }
    elsif ($key eq "mapped_size") {
	return $s->mapped_size_for_mem_range($range);
    }

    die("Invalid key");
}

sub to_string
{
    my $s = shift;
    return "MAP: MEM "
	. $s->mem_range->to_string
	    . " ELF "
		. ($s->elf_range ? $s->elf_range->to_string : "undef")
		    . " FILE " . $s->_vma->info('file');
    
}

1;
