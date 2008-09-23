use strict;
use warnings;

# ------------------------------------------------------------
# Range. It is considered to include (end - start) items, from 'start'.

package Range;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->{_start} = shift;
    $s->{_end} = shift;
    return $s->_sanity_check ? $s : undef;
}

sub _sanity_check
{
    my $s = shift;
    return (defined $s->start) && (defined $s->end) && ($s->start <= $s->end);
}

sub start { return $_[0]->{_start}; }
sub end { return $_[0]->{_end}; }

sub equals
{
    my $s = shift;
    my $r = shift;
    return $s->start == $r->start && $s->end == $r->end;
}

sub intersect
{
    my $s = shift;
    my $r = shift;

    return undef unless $s->overlaps($r);

    my $start = $s->start > $r->start ? $s->start : $r->start;
    my $end = $s->end > $r->end ? $r->end : $s->end;
    return Range->new($start, $end);
}

sub add
{
    my $s = shift;
    my $val = shift;
    $s->{_start} += $val;
    $s->{_end} += $val;
    return 1;
}

sub subtract
{
    my $s = shift;
    my $val = shift;
    return $s->add(-$val);
}

sub size
{
    my $s = shift;
    return $s->end - $s->start;
}

sub contains
{
    my $s = shift;
    my $val = shift;
    return $s->start <= $val && $val < $s->end;
}

sub overlaps
{
    my $s = shift;
    my $r = shift;

    return $s->contains($r->start)
	|| $r->contains($s->start)
	|| ($r->start <= $s->start && $s->end < $r->end);
}

sub truncate_below
{
    my $s = shift;
    my $limit = shift;

    $s->{_start} = $limit if $s->start < $limit;
    $s->{_end} = $limit if $s->end < $limit;
    return 1;
}

1;
