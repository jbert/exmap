#!/usr/bin/perl -w
use strict;
use Exmap;
use Gtk2;
use Gtk2::SimpleList;

main(@ARGV);
exit 0;

sub main
{
    my $exmap = Exmap->new;

    $exmap->load_procs;

    Gtk2->init;
    my $mw = Gtk2::Window->new("toplevel");

    # Why is this necessary?
    $mw->set_default_size(800,600);
    $mw->signal_connect(destroy => sub { Gtk2->main_quit; });

    my $elflist = ElfList->new($exmap->elf_files);

    my $tabwin = Gtk2::Notebook->new;
    $tabwin->append_page(make_proctab($exmap), "Processes");
    $tabwin->append_page(make_filetab($exmap), "Files");

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

sub make_proctab
{
    my $exmap = shift;

    my $vbox = Gtk2::VBox->new;
    my $proclist = ProcList->new($exmap->procs);
    my $proclistwin = $proclist->list_window;
    my $filelist_for_current_proc = PerProcFileList->new;
    
    $vbox->add($proclist->window);
    $vbox->add($filelist_for_current_proc->window);

    # Update the filelist when the proc selection changes
    $proclistwin->get_selection->signal_connect(changed => sub {
	my $selection = shift;
	my $model = $proclistwin->get_model;
	my $iter = $selection->get_selected;
	return undef unless $iter;
	my $pid = $model->get_value($iter, 0);
	my $proc = $exmap->pid_to_proc($pid);
	$filelist_for_current_proc->set_proc($proc, $exmap);
    });
    
    
    return $vbox;
}

sub make_filetab
{
    my $exmap = shift;

    my $vbox = Gtk2::VBox->new;
    my $filelist = FileList->new($exmap->files);
    my $filelistwin = $filelist->list_window;
    my $proclist_for_current_file = PerFileProcList->new;
    
    $vbox->add($filelist->window);
    $vbox->add($proclist_for_current_file->window);

    # Update the proclist when the file selection changes
    $filelistwin->get_selection->signal_connect(changed => sub {
	my $selection = shift;
	my $model = $filelistwin->get_model;
	my $iter = $selection->get_selected;
	return undef unless $iter;
	my $fname = $model->get_value($iter, 0);
	my $file = $exmap->name_to_file($fname);
	$proclist_for_current_file->set_file($file);
    });
    
    
    return $vbox;
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
    $s->_init_windows;
    $s->set_files(@_);
    return $s;
}

sub set_files
{
    my $s = shift;
    $s->{_files} = \@_;
    $s->update_rows;
    return 1;
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
package PerProcFileList;
use base qw/ListView/;
use constant BYTES_PER_KBYTE => 1024;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->_init_windows;
    return $s;
}

sub set_proc
{
    my $s = shift;
    $s->{_proc} = shift;
    my $exmap = shift;
    $s->update_rows($exmap);
    return 1;
}

sub _columns
{
    return ('File Name' => 'text',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	    );
}

sub update_rows
{
    my $s = shift;
    my $exmap = shift;
    
    my $lw = $s->list_window;

    my $proc = $s->{_proc};
    return unless $proc;
    my @files = map { $exmap->name_to_file($_) } $proc->filenames;
    @{$lw->{data}} = map {
	[
	 $_->name,
	 $proc->vm_size($_) / BYTES_PER_KBYTE,
	 $proc->mapped_size($_) / BYTES_PER_KBYTE,
	 $proc->effective_size($_)  / BYTES_PER_KBYTE,
	 ]
     } @files;
    return 1;
}


# ------------------------------------------------------------
package PerFileProcList;
use base qw/ListView/;

use constant BYTES_PER_KBYTE => 1024;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->_init_windows;
    return $s;
}

sub set_file
{
    my $s = shift;
    $s->{_file} = shift;
    $s->update_rows;
    return 1;
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

    my $file = $s->{_file};
    return unless $file;
    my @procs = $file->procs;;

    @{$lw->{data}} = map {
	[
	 $_->pid,
	 $_->exe_name,
	 $_->vm_size($file) / BYTES_PER_KBYTE,
	 $_->mapped_size($file) / BYTES_PER_KBYTE,
	 $_->effective_size($file)  / BYTES_PER_KBYTE,
	 ]
     } @procs;
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
