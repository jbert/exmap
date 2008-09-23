#!/usr/bin/perl -w
use strict;
use Exmap;
use Gtk2;
use Gtk2::SimpleList;

# There must be a better way to arrange windows?
use constant WIDTH => 800;
use constant HEIGHT => 600;

main(@ARGV);
exit 0;

sub main
{
    my $exmap = Exmap->new;
    unless ($exmap) {
	die("Can't initialise exmap data");
    }
    $exmap->load
	or die("Can't load exmap process information");

    Gtk2->init;
    my $mw = Gtk2::Window->new("toplevel");

    # Why is this necessary?
    $mw->set_default_size(WIDTH, HEIGHT);
    $mw->signal_connect(destroy => sub { Gtk2->main_quit; });

    my $tabwin = Gtk2::Notebook->new;

    my $sectionview = ElfSectionView->new;

    my ($proctab, $sectionlist1) = make_proctab($exmap);
    $tabwin->append_page($proctab, "Processes");
    my ($filetab, $sectionlist2) = make_filetab($exmap);
    $tabwin->append_page($filetab, "Files");

    foreach my $sectionlist ($sectionlist1, $sectionlist2) {
	$sectionview->register_section_list($sectionlist);
    }

    my $bottombar = make_bottombar($exmap);

    my $hpane = Gtk2::HPaned->new;
    $hpane->pack1($tabwin, 1, 1);
    $hpane->pack2($sectionview->window, 1, 1);

    my $vbox = Gtk2::VBox->new;
    $vbox->add($hpane);
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
    my $filelistwin = $filelist_for_current_proc->list_window;
    my $sectionlist = ElfSectionList->new;

    $vbox->add($proclist->window);
    $vbox->add($filelist_for_current_proc->window);
    $vbox->add($sectionlist->window);

    # Update the filelist when the proc selection changes
    $proclistwin->get_selection
	->signal_connect(changed => sub {
			   my $selection = shift;
			   my $model = $proclistwin->get_model;
			   my $iter = $selection->get_selected;
			   return undef unless $iter;
			   my $pid = $model->get_value($iter, 0);
			   my $proc = $exmap->pid_to_proc($pid);
			   $filelist_for_current_proc->set_data($proc);

			   # Also clear section list
			   $sectionlist->set_data();
		       });

    # Update the section list when the file selection changes
    $filelistwin->get_selection
	->signal_connect(changed => sub {
			     my $selection = shift;
			     my $model = $filelistwin->get_model;
			     my $iter = $selection->get_selected;
			     return undef unless $iter;
			     my $fname = $model->get_value($iter, 0);
			     my $file = $exmap->file_pool->name_to_file($fname);
			     my $proc_iter = $proclistwin->get_selection->get_selected;
			     my $pid = $proclistwin->get_model->get_value($proc_iter, 0);
			     my $proc = $exmap->pid_to_proc($pid);
			     $sectionlist->set_data($file, $proc)
			 });

    return ($vbox, $sectionlist);
}

sub make_filetab
{
    my $exmap = shift;

    my $vbox = Gtk2::VBox->new;
    my $filelist = FileList->new($exmap->files);
    my $filelistwin = $filelist->list_window;
    my $proclist_for_current_file = PerFileProcList->new;
    my $proclistwin = $proclist_for_current_file->list_window;
    my $sectionlist = ElfSectionList->new;
    
    $vbox->add($filelist->window);
    $vbox->add($proclist_for_current_file->window);
    $vbox->add($sectionlist->window);

    # Update the proclist when the file selection changes
    $filelistwin->get_selection
	->signal_connect(changed => sub {
			     my $selection = shift;
			     my $model = $filelistwin->get_model;
			     my $iter = $selection->get_selected;
			     return undef unless $iter;
			     my $fname = $model->get_value($iter, 0);
			     my $file = $exmap->file_pool->name_to_file($fname);
			     $proclist_for_current_file->set_data($file);
			     # Also clear section list
			     $sectionlist->set_data();
			 });
    
    # Update the section list when the proc selection changes
    $proclistwin->get_selection
	->signal_connect(changed => sub {
			     my $selection = shift;
			     my $model = $proclistwin->get_model;
			     my $iter = $selection->get_selected;
			     return undef unless $iter;
			     my $pid = $model->get_value($iter, 0);
			     my $proc = $exmap->pid_to_proc($pid);

			     my $file_iter = $filelistwin->get_selection->get_selected;
			     my $fname = $filelistwin->get_model->get_value($file_iter, 0);
			     my $file = $exmap->file_pool->name_to_file($fname);
	
			     $sectionlist->set_data($file, $proc);
			 });
    
    
    return ($vbox, $sectionlist);
}

sub make_bottombar
{
    my $exmap = shift;
    my $bottombar = Gtk2::HBox->new;

    my @procs = $exmap->procs;
    my $total_effective = 0;
    my $total_mapped = 0;
    foreach my $proc (@procs) {
	$total_effective += $proc->effective_size;
	$total_mapped += $proc->mapped_size;
    }
    my $text = sprintf ("Number of Procs: %d Number of Files: %d Total Mapped: %d Kbytes Total Effective: %d Kbytes",
			scalar @procs,
			scalar($exmap->files),
			$total_mapped / 1024,
			$total_effective / 1024);

    $bottombar->pack_start(Gtk2::Label->new($text), 0, 0, 0);

    my $quit_button = Gtk2::Button->new("Quit");
    $quit_button->signal_connect(clicked => sub { Gtk2::main_quit; });
    $bottombar->pack_end($quit_button, 0, 0, 0);

    return $bottombar;
}

# ------------------------------------------------------------
package View;

sub new
{
    my $c = shift;
    $c = ref $c if ref $c;
    my $s = {};
    bless $s, $c;
    $s->_init_windows;
    $s->set_data(@_);
    return $s;
}

sub window
{
    my $s = shift;
    my $win = shift;
    if ($win) {
	$s->{_window} = $win;
    }
    return $s->{_window};
}

sub set_data
{
    my $s = shift;
    $s->{_data} = \@_;
    $s->update_view;
    return 1;
}

sub _to_hex_string
{
    my $s = shift;
    my $val = shift;
    return sprintf("0x%08x", $val);
}

# ------------------------------------------------------------
package ListView;
use base qw/View/;

my $ONCE = 0;

sub _init_windows
{
    my $s = shift;

    if (!$ONCE) {
	$s->_register_hidden_column;
	$ONCE = 1;
    }
    
    my $listwin = Gtk2::SimpleList->new($s->_columns);
    $s->list_window($listwin);
    $s->_make_all_sortable;
    my $ssc = $s->_start_sort_col;
    if (defined $ssc) {
	my $model = $s->list_window->get_model;
	$model->set_sort_column_id($ssc, 'descending');
    }
    $s->_make_all_resizable;
    # Can't make it work :-(
#    $s->_add_tooltips;

    my $scr_list = Gtk2::ScrolledWindow->new;
    $scr_list->set_policy('automatic', 'automatic');
    $scr_list->add($listwin);

    $s->window($scr_list);

    return 1;
}

sub _start_sort_col
{
    return undef;
}

sub _register_hidden_column
{
    Gtk2::SimpleList->add_column_type('hidden',
				      type => 'Glib::Scalar',
				      renderer => 'Gtk2::CellRendererText',
				      attr => 'hidden');
    return 1;
}

sub _columns { die "_columns called in listview" };
sub _tooltips { return (); }

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
    my $s = shift;
    return $s->_foreach_column( sub {
        my $s = shift;
	my $colid = shift;
	my $col = shift;
	$s->list_window->get_column($colid)->set_sort_column_id($colid);
    });
}

sub _make_all_resizable
{
    my $s = shift;
    return $s->_foreach_column( sub {
        my $s = shift;
	my $colid = shift;
	my $col = shift;
	$s->list_window->get_column($colid)->set_resizable(1);
    });
}

# sub _add_tooltips
# {
#     my $s = shift;

#     my @tiptext = $s->_tooltips;
#     return 1 unless @tiptext;

#     $s->{_tooltips} = Gtk2::Tooltips->new;
#     return $s->_foreach_column( sub {
#         my $s = shift;
# 	my $colid = shift;
# 	my $col = shift;
# 	my $text = shift @tiptext;
# 	unless ($text) {
# 	    warn("Wrong number of tooltips in class " . ref $s);
# 	    return undef;
# 	}
# 	$s->list_window->get_column($colid)->set_widget(undef);
# 	my $widget = $s->list_window->get_column($colid)->get_widget;
# 	print "widget is ", (defined $widget ? "" : "not "), " defined\n";
# 	$s->{_tooltips}->set_tip($widget, $text);
# 	print "set tt to $text\n";
#     });
#     $s->{_tooltips}->enable;
# }

sub _foreach_column
{
    my $s = shift;
    my $subref = shift;
    
    my $win = $s->list_window;
    my @cols = $win->get_columns;
    my $colid = 0;

    foreach my $col (@cols) {
	$subref->($s, $colid, $col);
	++$colid;
    }
    return;
}


# ------------------------------------------------------------
package ProcList;
use base qw/ListView/;

use constant BYTES_PER_KBYTE => 1024;

sub _columns
{
    return (PID => 'int',
	    Cmdline => 'text',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	   );
}

sub _start_sort_col
{
    return 4;
}

# Per-column tooltip text, in order. Needs to be kept in sync with _columns
sub _tooltips
{
    return ("Process ID",
	    "Exuctable name",
	    "Total virtual memory size for process",
	    "Total physical memory in use by process (includes shared)",
	    "Total Physical memory usage, with shared usage split fairly amongst all users",
	    );
}

sub update_view
{
    my $s = shift;
    my $lw = $s->list_window;

    @{$lw->{data}} = map {
	[
	 $_->pid,
	 $_->cmdline,
	 $_->vm_size / BYTES_PER_KBYTE,
	 $_->mapped_size / BYTES_PER_KBYTE,
	 $_->effective_size  / BYTES_PER_KBYTE,
	]
    } @{$s->{_data}};
    return 1;
}

# ------------------------------------------------------------
package FileList;
use base qw/ListView/;
use constant BYTES_PER_KBYTE => 1024;


sub _columns
{
    return ('File Name' => 'text',
	    'Num Procs' => 'int',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	   );
}

sub _start_sort_col
{
    return 4;
}

sub update_view
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
    } @{$s->{_data}};
    return 1;
}


# ------------------------------------------------------------
package PerProcFileList;
use base qw/ListView/;
use constant BYTES_PER_KBYTE => 1024;

sub _columns
{
    return ('File Name' => 'text',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	   );
}

sub _start_sort_col
{
    return 3;
}

sub update_view
{
    my $s = shift;

    
    my $lw = $s->list_window;

    my $proc = $s->{_data}->[0];
    return unless $proc;
    @{$lw->{data}} = map {
	[
	 $_->name,
	 $proc->vm_size($_) / BYTES_PER_KBYTE,
	 $proc->mapped_size($_) / BYTES_PER_KBYTE,
	 $proc->effective_size($_)  / BYTES_PER_KBYTE,
	]
    } $proc->files;
    return 1;
}


# ------------------------------------------------------------
package PerFileProcList;
use base qw/ListView/;

use constant BYTES_PER_KBYTE => 1024;

sub _columns
{
    return (PID => 'int',
	    Cmdline => 'text',
	    Exe => 'text',
	    'Size (K)' => 'double',
	    'Mapped (K)' => 'double',
	    'Effective (K)' => 'double',
	   );
}

sub _start_sort_col
{
    return 5;
}

sub update_view
{
    my $s = shift;
    my $lw = $s->list_window;

    my $file = $s->{_data}->[0];
    return unless $file;
    my @procs = $file->procs;;

    @{$lw->{data}} = map {
	[
	 $_->pid,
	 $_->cmdline,
	 $_->exe_name,
	 $_->vm_size($file) / BYTES_PER_KBYTE,
	 $_->mapped_size($file) / BYTES_PER_KBYTE,
	 $_->effective_size($file)  / BYTES_PER_KBYTE,
	]
    } @procs;
    return 1;
}

# ------------------------------------------------------------
package ElfSectionList;
use base qw/ListView/;

sub _init_windows
{
    my $s = shift;
    $s->SUPER::_init_windows;
    my $frame = Gtk2::Frame->new("ELF Sections");
    $frame->add($s->window);
    $s->window($frame);
    return 1;
}


sub _columns
{
    return ('Name' => 'text',
	    'File Offset' => 'text',
	    'Size' => 'int',
	    'Mapped Size' => 'int',
	    'Effective Size' => 'int',
	   );
}

sub _start_sort_col
{
    return 4;
}

sub file
{
    my $s = shift;
    return $s->{_data}->[0];
}

sub proc
{
    my $s = shift;
    return $s->{_data}->[1];
}

sub update_view
{
    my $s = shift;
    
    my $lw = $s->list_window;
    my $file = $s->file;
    my $proc = $s->proc;
    unless ($file && $proc && $file->is_elf) {
	@{$lw->{data}} = ();
	return 1;
    }

    @{$lw->{data}} = map {
	[
	 $_->name,
	 $_->is_nobits ? "absent" : $_->offset,
	 $_->size,
	 $proc->mapped_size($file, $_->mem_range),
	 $proc->effective_size($file, $_->mem_range),
	]
    } $file->elf->mappable_sections;
    return 1;
}


# ------------------------------------------------------------
package ElfSectionView;
use base qw/ListView/;
use constant BYTES_PER_KBYTE => 1024;
use constant PAGE_SIZE => 4096;

sub _columns
{
    return ('Symbol Name' => 'text',
	    'Size' => 'int',
	    'Mapped Size' => 'int',
	    'Effective Size' => 'int',
	   );
}

sub _start_sort_col
{
    return 3;
}

sub register_section_list
{
    my $s = shift;
    my $sectionlist = shift;

    my $lw = $sectionlist->list_window;

    $lw->get_selection
	->signal_connect(changed => sub {
			     my $selection = shift;
			     my $model = $lw->get_model;
			     my $iter = $selection->get_selected;
			     return undef unless $iter;
			     my $section_name = $model->get_value($iter, 0);
			     $s->set_data($sectionlist->file,
					  $sectionlist->proc,
					  $section_name);
			 });
}

sub file
{
    my $s = shift;
    return $s->{_data}->[0];
}

sub proc
{
    my $s = shift;
    return $s->{_data}->[1];
}

sub section_name
{
    my $s = shift;
    return $s->{_data}->[2];
}

sub update_view
{
    my $s = shift;
    my $lw = $s->list_window;

    my $file = $s->file;
    my $proc = $s->proc;
    my $section_name = $s->section_name;

    unless ($file && $proc && $section_name && $file->is_elf) {
	@{$lw->{data}} = ();
	return 1;
    }

    my $section = $file->elf->section_by_name($section_name);
    my @symbols = $file->elf->symbols_in_section($section);

    @{$lw->{data}} = map {
	[ $_->name,
	  $_->size,
	  $proc->mapped_size($file, $_->range),
	  $proc->effective_size($file, $_->range),
	  ]
    } @symbols;

    return 1;
}
