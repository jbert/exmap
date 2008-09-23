#!/usr/bin/perl -w
use Exmap;
use Gtk2;
use Gtk2::SimpleList;

main(@ARGV);
exit 0;

sub main
{
    my $exmap = Exmap->new;

    $exmap->load_procs;
    $exmap->load_page_info;

    Gtk2->init;
    my $mw = Gtk2::Window->new("toplevel");

    # Why is this necessary?
    $mw->set_default_size(800,600);
    $mw->signal_connect(destroy => sub { Gtk2->main_quit; });

    my $proclist = ProcList->new($exmap->procs);
    my $filelist = FileList->new($exmap->files);
    my $elflist = ElfList->new($exmap->elf_files);

    my $tabwin = Gtk2::Notebook->new;
    $tabwin->append_page($proclist->window, "Processes");
    $tabwin->append_page($filelist->window, "Files");
    $tabwin->append_page($elflist->window, "Elf Files");

    my $bottombar = Gtk2::HBox->new;

    $bottombar->add(Gtk2::Label->new("Number of Procs: "
				     . scalar($exmap->procs)
				     . " Number of Files: "
				     . scalar($exmap->files)));
    
    my $quit_button = Gtk2::Button->new("Quit");
    $quit_button->signal_connect(clicked => sub { Gtk2::main_quit; });
    $bottombar->pack_end($quit_button, 0, 0, 0);
    
    my $vbox = Gtk2::VBox->new;
    $vbox->add($tabwin);
    $vbox->pack_end($bottombar, 0, 0, 0);
    
    $mw->add($vbox);
    $mw->show_all;

    Gtk2->main;
}

# ------------------------------------------------------------
package View;

sub window
{
    my $s = shift;
    my $win = shift;
    if ($win) {
	$s->{_window} = $win;
    }
    return $s->{_window};
}

# ------------------------------------------------------------
package ListView;
use base qw/View/;

sub _init_windows
{
    my $s = shift;
    
    my $listwin = Gtk2::SimpleList->new($s->_columns);
    $s->list_window($listwin);
    $s->_make_all_sortable;

    my $scr_list = Gtk2::ScrolledWindow->new;
    $scr_list->set_policy('automatic', 'automatic');
    $scr_list->add($listwin);

    $s->window($scr_list);
    return 1;
}

sub _columns { die "_columns called in listview" };

sub list_window
{
    my $s = shift;
    my $win = shift;
    if ($win) {
	$s->{_list_window} = $win;
    }
    return $s->{_list_window};
}

sub _make_all_sortable
{
    my $s = shift; # not used

    my $win = $s->list_window;
    my @cols = $win->get_columns;
    my $colid = 0;
    foreach my $col (@cols) {
	$s->list_window->get_column($colid)->set_sort_column_id($colid);
	++$colid;
    }
    return;
}


# ------------------------------------------------------------
package ProcList;
use base qw/ListView/;

use constant BYTES_PER_KBYTE => 1024;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->{_procs} = \@_;
    $s->_init_windows;
    $s->update_rows;
    return $s;
}

sub _columns
{
    return (PID => 'int',
	    Exe => 'text',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	    );
}

sub update_rows
{
    my $s = shift;
    my $lw = $s->list_window;

    @{$lw->{data}} = map {
	[
	 $_->pid,
	 $_->exe_name,
	 $_->vm_size / BYTES_PER_KBYTE,
	 $_->mapped_size / BYTES_PER_KBYTE,
	 $_->effective_size  / BYTES_PER_KBYTE,
	 ]
     } @{$s->{_procs}};
    return 1;
}

# ------------------------------------------------------------
package FileList;
use base qw/ListView/;
use constant BYTES_PER_KBYTE => 1024;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->{_files} = \@_;
    $s->_init_windows;
    $s->update_rows;
    return $s;
}

sub _columns
{
    return ('File Name' => 'text',
	    'Num Procs' => 'int',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	    );
}

sub update_rows
{
    my $s = shift;
    my $lw = $s->list_window;

    @{$lw->{data}} = map {
	[
	 $_->name,
	 scalar($_->procs),
	 $_->vm_size / BYTES_PER_KBYTE,
	 $_->mapped_size / BYTES_PER_KBYTE,
	 $_->effective_size  / BYTES_PER_KBYTE,
	 ]
     } @{$s->{_files}};
    return 1;
}

# ------------------------------------------------------------
package ElfList;
use base qw/ListView/;
use constant BYTES_PER_KBYTE => 1024;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->{_elf_files} = \@_;
    $s->_init_windows;
    $s->update_rows;
    return $s;
}

sub _columns
{
    return ('Elf File Name' => 'text',
	    'Num Procs' => 'int',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	    );
}

sub update_rows
{
    my $s = shift;
    my $lw = $s->list_window;

    @{$lw->{data}} = map {
	[
	 $_->name,
	 scalar($_->procs),
	 $_->vm_size / BYTES_PER_KBYTE,
	 $_->mapped_size / BYTES_PER_KBYTE,
	 $_->effective_size  / BYTES_PER_KBYTE,
	 ]
     } @{$s->{_elf_files}};
    return 1;
}
