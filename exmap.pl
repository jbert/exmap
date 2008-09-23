#!/usr/bin/perl -w
#
# (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
#
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
    my $doquit = shift;
    
    my $exmap = Exmap->new;
    unless ($exmap) {
	die("Can't initialise exmap data");
    }

    my $progress = Progress->new;
    $exmap->load($progress)
	or die("Can't load exmap process information");

    print "Calculating...\n";
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

    return 0 if $doquit;
    print "Running\n";
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
    my $totals = Exmap::Sizes->new;
    $totals->scale_mbytes;
    foreach my $proc (@procs) {
	my $sizes = $proc->sizes;
	$totals->add($sizes);
    }
    my $text = sprintf ("Number of Procs: %d Number of Files: %d\n",
			scalar @procs,
			scalar($exmap->files));

    $text .= join( "|", map {
	$totals->key_name($_) . " " . $totals->sval($_);
    } $totals->keys);

    $bottombar->pack_start(Gtk2::Label->new($text), 0, 0, 0);

    my $quit_button = Gtk2::Button->new("Quit");
    $quit_button->signal_connect(clicked => sub { Gtk2::main_quit; });
    $bottombar->pack_end($quit_button, 0, 0, 0);

    return $bottombar;
}

# ------------------------------------------------------------
package Progress;
use base qw/Exmap::Progress/;

sub number_of_ticks
{
    my $s = shift;
    my $nticks = shift;
    print "Number of procs: $nticks\n";
    return 1;
}

sub tick
{
    my $s = shift;
    my $text = shift;
    print $text, "\n";
    return 1;
}

sub finished
{
    my $s = shift;
    print "Finished loading\n";
    return 1;
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

    my @cols = $s->_columns;
    my $start_sort_col = (scalar @cols) / 2;
    my $sizes = Exmap::Sizes->new;
    $sizes->scale_kbytes;
    push @cols,
	map { $_ => 'text' } $sizes->key_names;
	
    my $listwin = Gtk2::SimpleList->new(@cols);
    $s->list_window($listwin);
    $s->_make_all_sortable;
    $s->_set_all_col_sortfunc;

    my $model = $s->list_window->get_model;
    $model->set_sort_column_id($start_sort_col, 'descending');

    $s->_make_all_resizable;

    my $scr_list = Gtk2::ScrolledWindow->new;
    $scr_list->set_policy('automatic', 'automatic');
    $scr_list->add($listwin);

    $s->window($scr_list);

    my $frame_text = $s->_frame_name;
    if ($frame_text) {
	my $frame = Gtk2::Frame->new($frame_text);
	$frame->add($s->window);
	$s->window($frame);
    }
    
    return 1;
}

sub _frame_name
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

sub list_window
{
    my $s = shift;
    my $win = shift;
    if ($win) {
	$s->{_list_window} = $win;
    }
    return $s->{_list_window};
}

sub _set_all_col_sortfunc
{
    my $s = shift;

    # Do a numeric sort on all numeric strings, and string sort on others
    my $sort_func = sub {
	my $model = shift;
	my $a = shift;
	my $b = shift;
	my $colid = shift;
	$a = lc $model->get_value($a, $colid);
	$b = lc $model->get_value($b, $colid);
	
	return 0 if (!defined $a) && (!defined $b);
	return +1 if not defined $a;
	return -1 if not defined $b;

        if ($a =~ /^[\d\.]+$/ && $b =~ /^[\d\.]+$/) {
	    $a <=> $b;
	}
	else {
	    $a cmp $b;
	}
    };

    return $s->_foreach_column( sub {
        my $s = shift;
	my $colid = shift;
	my $col = shift;
	$s->list_window->get_model->set_sort_func($colid, $sort_func, $colid);
    });
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

sub _columns
{
    return (PID => 'int',
	    Cmdline => 'text');
}

sub update_view
{
    my $s = shift;
    my $lw = $s->list_window;

    my @data;
    foreach my $proc (@{$s->{_data}}) {
	my $sizes = $proc->sizes;
	$sizes->scale_kbytes;
	push @data, [ $proc->pid,
		      $proc->cmdline,
		      $sizes->all_svals,
		      ];
    }
    @{$lw->{data}} = @data;
    return 1;
}

# ------------------------------------------------------------
package FileList;
use base qw/ListView/;

sub _columns
{
    return ('File Name' => 'text',
	    'Num Procs' => 'int',
	   );
}

sub update_view
{
    my $s = shift;
    my $lw = $s->list_window;

    my @data;
    foreach my $file (@{$s->{_data}}) {
	my $sizes = $file->sizes;
	$sizes->scale_kbytes;
	push @data, [ $file->name,
		      scalar($file->procs),
		      $sizes->all_svals,
		      ];
    }
    @{$lw->{data}} = @data;

    return 1;
}


# ------------------------------------------------------------
package PerProcFileList;
use base qw/ListView/;

sub _frame_name
{
    return "Files per process";
}

sub _columns
{
    return ('File Name' => 'text',
	   );
}

sub update_view
{
    my $s = shift;
    
    my $lw = $s->list_window;
    my $proc = $s->{_data}->[0];
    if (!$proc) {
	@{$lw->{data}} = (["No process selected"]);
	return;
    }

    my @data;
    foreach my $file ($proc->files) {
	my $sizes = $proc->sizes($file);
	$sizes->scale_kbytes;
	push @data, [
		     $file->name,
		     $sizes->all_svals,
		     ];
    }
    @{$lw->{data}} = @data;

    return 1;
}


# ------------------------------------------------------------
package PerFileProcList;
use base qw/ListView/;


sub _frame_name
{
    return "Processes per file";
}

sub _columns
{
    return (PID => 'int',
	    Cmdline => 'text',
	   );
}

sub update_view
{
    my $s = shift;
    my $lw = $s->list_window;

    my $file = $s->{_data}->[0];
    if (!$file) {
	@{$lw->{data}} = ([0, "No file selected"]);
	return;
    }

    my @data;
    foreach my $proc ($file->procs) {
	my $sizes = $proc->sizes($file);
	$sizes->scale_kbytes;
	push @data, [
		     $proc->pid,
		     $proc->cmdline,
		     $sizes->all_svals,
		     ];
    }

    @{$lw->{data}} = @data;
    return 1;
}

# ------------------------------------------------------------
package ElfSectionList;
use base qw/ListView/;

sub _frame_name
{
    return "ELF Sections";
}

sub _columns
{
    return ('Name' => 'text',
	    'File Offset' => 'text',
	   );
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
    unless ($file && $proc) {
	@{$lw->{data}} = (["No file and proc selected"]);
	return 1;
    }
    unless ($file->is_elf) {
	@{$lw->{data}} = ([$file->name . " is not an ELF file"]);
	return 1;
    }

    my @data;
    foreach my $section ($file->elf->mappable_sections) {
	my $sizes = $proc->sizes($file, $section->mem_range);
	$sizes->scale_kbytes;
	push @data, [$section->name,
		     $section->is_nobits ? "absent" : $section->offset,
		     $sizes->all_svals,
		    ];
    }
    @{$lw->{data}} = @data;

    return 1;
}


# ------------------------------------------------------------
package ElfSectionView;
use base qw/ListView/;
use constant PAGE_SIZE => 4096;

sub _frame_name
{
    return "ELF Symbols";
}

sub _columns
{
    return ('Symbol Name' => 'text',
	   );
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

    unless ($section_name) {
	@{$lw->{data}} = (["No ELF section selected"]);
	return 1;
    }
    unless ($file && $proc) {
	@{$lw->{data}} = (["No file and proc selected"]);
	return 1;
    }
    unless ($file->is_elf) {
	@{$lw->{data}} = ([$file->name . " is not an ELF file"]);
	return 1;
    }

    my $section = $file->elf->section_by_name($section_name);
    my @symbols = $file->elf->symbols_in_section($section);

    unless (@symbols) {
	@{$lw->{data}} = (["No symbols found in $section_name in " . $file->name]);
	return 1;
    }

    my @data;
    foreach my $sym (@symbols) {
	my $sizes = $proc->sizes($file, $sym->range);
	push @data, [
		     $sym->name,
		     $sym->size,
		     $sizes->all_svals,
		     ];
    }
    @{$lw->{data}} = @data;

    return 1;
}
