/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#include "jutil.hpp"
#include "Exmap.hpp"
#include "Elf.hpp"

#include <sstream>

#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>

using namespace Exmap;
using namespace std;
using namespace jutil;
using Elf::Address;

ostream &operator<<(ostream &os, const Exmap::Process &proc)
{
    proc.print(os);
    return os;
}

ostream &operator<<(ostream &os, const Exmap::Map &map)
{
    map.print(os);
    return os;
}


// ------------------------------------------------------------

Snapshot::Snapshot(SysInfoPtr &sys_info)
    : _page_pool(new PagePool),
      _file_pool(new FilePool),
      _sys_info(sys_info)
{
}

const list<ProcessPtr> Snapshot::procs()
{
    return map_values(_procs);
}


const list<pid_t> Snapshot::pids()
{
    return map_keys(_procs);
}	
	
const ProcessPtr Snapshot::proc(pid_t pid)
{
    ProcessPtr ret;
    map<pid_t, ProcessPtr>::const_iterator it;
    it = _procs.find(pid);
    if (it != _procs.end()) {
	ret = it->second;
    }
    return ret;
}

const list<FilePtr> Snapshot::files()
{
    return _file_pool->files();
}


const FilePtr Snapshot::file(const std::string &fname)
{
    return _file_pool->name_to_file(fname);
}


bool Snapshot::load()
{
    list<pid_t> pids = _sys_info->accessible_pids();

    if (!_sys_info->sanity_check()) {
	warn << "Can't get system info\n";
	exit(-1);
    }

    if (pids.empty()) {
	warn << "Snapshot::load - can't get pid list\n";
	return false;
    }

    if (!load_procs(pids)) {
	warn << "Snapshot::load - failed to load processes\n";
	return false;
    }
    
    if (!calculate_file_mappings()) {
    	warn << "Snapshot::load - failed to load calculate file mappings\n";
    	return false;
    }
    return true;
}

bool Snapshot::load_procs(const list<pid_t> &pids)
{
    list<pid_t>::const_iterator it;
    pid_t mypid = getpid();

    _procs.clear();
    _page_pool->clear();
    
    for (it = pids.begin(); it != pids.end(); ++it) {
	if (*it == mypid) {
	    // Don't monitor ourselves
	    continue;
	}

	ProcessPtr proc(new Process(_page_pool, *it));
	proc->selfptr(proc);
	if (!proc->load(_sys_info)) {
	    warn << "Snapshot::load_procs - can't load pid " << *it << "\n";
	    continue;
	}

	if (proc->has_mm()) {
	    _procs[*it] = proc;
	}
    }
    
    return !_procs.empty();
}

bool Snapshot::calculate_file_mappings()
{
    list<ProcessPtr>::iterator it;
    list<ProcessPtr> processes = procs();

    for (it = processes.begin(); it != processes.end(); ++it) {
	if (!(*it)->calc_vma_maps(_file_pool)) {
	    warn << "Failed to process maps for pid " << (*it)->pid() << "\n";
	}
    }
    return !_file_pool->files().empty();
}


// ------------------------------------------------------------

void PagePool::clear()
{
    _counts.clear();
}

int PagePool::count(const Page &page)
{
    return _counts[page.cookie()];
}

void PagePool::inc_page_count(const Page &page)
{
    _counts[page.cookie()] = _counts[page.cookie()]++;
}

void PagePool::inc_pages_count(const std::list<Page> &pages)
{
    list<Page>::const_iterator it;
    for (it = pages.begin(); it != pages.end(); ++it) {
	inc_page_count(*it);
    }
}

// ------------------------------------------------------------

void FilePool::clear()
{
    _files.clear();
}

FilePtr FilePool::name_to_file(const string &name)
{
    return _files[name];
}

FilePtr FilePool::get_or_make_file(const string &name)
{
    map<string, FilePtr>::iterator it;

    it = _files.find(name);
    if (it == _files.end()) {
	FilePtr f(new File(name));
	_files[name] = f;
	it = _files.find(name);
    }
    return it->second;
}

list<FilePtr> FilePool::files()
{
    return map_values(_files);
}

// ------------------------------------------------------------

Process::Process(const PagePoolPtr &page_pool,
		 pid_t pid)
    : _pid(pid),
      _page_pool(page_pool)
{ }

const PagePoolPtr &Process::page_pool()
{
    return _page_pool;
}

boost::shared_ptr<Process> Process::selfptr()
{
    return _selfptr.lock();
}

void Process::selfptr(boost::shared_ptr<Process> &p)
{
    _selfptr = p;
}

bool Process::load(SysInfoPtr &sys_info)
{
    _cmdline = sys_info->read_cmdline(_pid);
    if (_cmdline.empty()) {
	_cmdline = "[nocmdline]";
    }

    if (!sys_info->read_vmas(_page_pool, _pid, _vmas)) {
	warn << "Process::load - can't load vmas: " << _pid << "\n";
	return false;
    }

    // Can't load pages if we don't have any...
    if (!has_mm()) { return true; }
    
    if (!load_page_info(sys_info)) {
	warn << "Process::load - can't load page info: " << _pid << "\n";
	return false;
    }

    remove_vdso_if_nopages();

    return true;
}

void Process::remove_vdso_if_nopages()
{
    list<VmaPtr>::iterator it;
    
    for (it = _vmas.begin(); it != _vmas.end(); ++it) {
	if ((*it)->is_vdso() && (*it)->num_pages() == 0) {
	    _vmas.erase(it);
	    return;
	}
    }
}

string Process::cmdline()
{
    return _cmdline;
}

list<FilePtr> Process::files()
{
    list<FilePtr> files(_files.begin(), _files.end());
    return files;
}

pid_t Process::pid()
{
    return _pid;
}

SizesPtr Process::sizes()
{
    return Map::sum_sizes(_page_pool, _maps);
}

SizesPtr Process::sizes(const FilePtr &file)
{
    list<MapPtr> maps;
    maps = restrict_maps_to_file(file);
    return Map::sum_sizes(_page_pool, maps);
}

SizesPtr Process::sizes(const FilePtr &file,
			const RangePtr &elf_range)
{
    list<MapPtr> maps;
    list<MapPtr>::iterator it;
    maps = restrict_maps_to_file(file);
    SizesPtr sizes(new Sizes);

    for (it = maps.begin(); it != maps.end(); ++it) {
	RangePtr map_elf_range = (*it)->elf_range();
	if (map_elf_range) {
	    RangePtr subrange = elf_range->intersect(*map_elf_range);
	    if (subrange) {
		RangePtr mem_range = (*it)->elf_to_mem_range(subrange);
		SizesPtr subsizes = (*it)->sizes_for_mem_range(_page_pool,
							       mem_range);
		if (subsizes) {
		    sizes->add(subsizes);
		}
	    }
	}
    }
    return sizes;
}

list<MapPtr> Process::restrict_maps_to_file(const FilePtr &file)
{
    list<MapPtr> result;
    list<MapPtr> file_maps = file->maps();
    list<MapPtr> proc_maps = _maps;

    file_maps.sort();
    proc_maps.sort();
    
    while (!file_maps.empty() && !proc_maps.empty()) {
	if (file_maps.front() == proc_maps.front()) {
	    result.push_back(proc_maps.front());
	    proc_maps.pop_front();
	    file_maps.pop_front();
	}
	else if (file_maps.front() < proc_maps.front()) {
	    file_maps.pop_front();
	}
	else {
	    proc_maps.pop_front();
	}
    }

    if (result.size() == 0) {
	warn << _pid << ": empty restriction to file " << file->name() << "\n";
    }
    return result;
}



void Process::add_file(const FilePtr &file)
{
    _files.insert(file);
}

bool Process::calc_vma_maps(FilePoolPtr &file_pool)
{
    list<MapPtr> vma_maps;
    list<VmaPtr>::iterator vma_it;

    _maps.clear();
    
    VmaPtr previous_vma;
    FilePtr previous_file;
    FilePtr file;
    for (vma_it = _vmas.begin(); vma_it != _vmas.end(); ++vma_it) {
	file = file_pool->get_or_make_file((*vma_it)->fname());
	file->add_proc(selfptr());
	vma_maps = (*vma_it)->calc_maps(file,
					previous_vma,
					previous_file,
					pid());

	if (vma_maps.empty()) {
	    warn << pid() << ": can't calc maps for vma " << hex
		 << (*vma_it)->start() << dec << " : " << file->name() << "\n";
	}

	_maps.insert(_maps.end(), vma_maps.begin(), vma_maps.end());
	add_file(file);
	previous_vma = *vma_it;
	previous_file = file;
    }

    list<RangePtr> ranges;
    list<MapPtr>::iterator map_it;
    for (map_it = _maps.begin(); map_it != _maps.end(); ++map_it) {
	ranges.push_back((*map_it)->mem_range());
    }
    if (Range::any_overlap(ranges)) {
	warn << pid() << ": overlapping vma ranges\n";
	return false;
    }

    return !_maps.empty();
}



bool Process::load_page_info(SysInfoPtr &sys_info)
{
    map<Address, list<Page> > page_info;

    if (!sys_info->read_page_info(_pid, page_info)) {
	warn << "Can't read page info for " << _pid;
	return false;
    }

    map<Address, list<Page> >::iterator vma_it;
    for (vma_it = page_info.begin(); vma_it != page_info.end(); ++vma_it) {
	Address start_address = vma_it->first;
	VmaPtr vma;
	if (!find_vma_by_addr(start_address, vma)) {
	    // This can happen, a process can alloc whilst we are
	    // running
	    warn << "Process::load_page_info - can't find vma at "
		 << hex << start_address << dec << ": " << _pid << "\n";
	    continue;
	}
	
	vma->add_pages(vma_it->second);
	_page_pool->inc_pages_count(vma_it->second);
    }

    return true;
}


bool Process::has_mm()
{
    return _vmas.size() > 0;
}

bool Process::find_vma_by_addr(Address start,
				VmaPtr &vma)
{
    list<VmaPtr>::const_iterator it;

    for (it = _vmas.begin(); it != _vmas.end(); ++it) {
	dbg << "vma " << (*it)->start() << " s " << start << "\n";
	if ((*it)->start() == start) {
	    vma = *it;
	    return true;
	}
    }
    return false;
}


void Process::print(ostream &os) const
{
    os << "PID: " << _pid << "\n";
    std::list<MapPtr>::const_iterator map_it;

    for (map_it = _maps.begin(); map_it != _maps.end(); ++map_it) {
	os << **map_it << "\n";
    }
}



// ------------------------------------------------------------

Vma::Vma(Elf::Address start,
	 Elf::Address end,
	 off_t offset,
	 const std::string &fname)
    : _offset(offset),
      _fname(fname)
{
    _range = RangePtr(new Range(start, end));
}

int Vma::num_pages()
{
    return _pages.size();
}

boost::shared_ptr<Vma> Vma::selfptr()
{
    return _selfptr.lock();
}

void Vma::selfptr(boost::shared_ptr<Vma> &p)
{
    _selfptr = p;
}

    
void Vma::add_pages(const list<Page> &pages)
{
    list<Page>::const_iterator it;
    for (it = pages.begin(); it != pages.end(); ++it) {
	_pages.push_back(*it);
    }
}

bool Vma::is_vdso()
{
    return fname() == "[vdso]";
}

bool Vma::is_file_backed()
{
    string::size_type pos = _fname.length();
    if (pos == 0) {
	warn << "Zero length file name in vma\n";
	return false;
    }
    // Names like [vdso], [anon], [stack] etc
    return !(_fname[0] == '[' && _fname[pos-1] == ']');
}


bool Vma::addr_to_pgnum(Address addr, unsigned int &pgnum)
{
    if (addr > end()) {
	warn << addr << " is beyond vma end " << end() << "\n";
	return false;
    }
    Address a = Elf::page_align_down(addr);
    if (a < start()) {
	warn << addr << " is before vma start " << start() << "\n";
	return false;
    }
    a -= start();
    pgnum = a / Elf::PAGE_SIZE;
    return true;
}

bool Vma::get_pages_for_range(const RangePtr &mrange,
			      std::list<PartialPageInfo> &ppinfo)
{
    struct PartialPageInfo ppi;
    ppinfo.clear();
    
    if (mrange->size() <= 0) {
	warn << "Vma::get_pages_for_range - invalid range\n";
	return false;
    }

    unsigned int start_pgnum, end_pgnum;
    if (!addr_to_pgnum(mrange->start(), start_pgnum)) {
	warn << "Vma::get_pages_for_range - can't get start pgnum\n";
	return false;
    }
    if (!addr_to_pgnum(mrange->end() - 1, end_pgnum)) {
	warn << "Vma::get_pages_for_range - can't get end pgnum\n";
	return false;
    }
    if (start_pgnum > end_pgnum) {
	warn << "start_pgnum greater than end: "
	     << start_pgnum << ", " << end_pgnum << "\n";
	return false;
    }
    if (start_pgnum >= _pages.size()) {
	warn << "Vma::get_pages_for_range - start pgnum out of range: "
	     << start_pgnum << ", " << _pages.size() << " " << _fname << "\n";
	return false;
    }
    if (end_pgnum >= _pages.size()) {
	warn << "Vma::get_pages_for_range - end pgnum out of range: "
	     << end_pgnum << ", " << _pages.size() << "\n";
	return false;
    }

    if (start_pgnum == end_pgnum) {
	ppi.page = _pages[start_pgnum];
	ppi.bytes = mrange->size();
	ppinfo.push_back(ppi);
	return true;
    }

    ppi.page = _pages[start_pgnum];
    ppi.bytes = Elf::PAGE_SIZE
	- (mrange->start() - Elf::page_align_down(mrange->start()));
    ppinfo.push_back(ppi);

    ppi.page = _pages[end_pgnum];
    ppi.bytes = mrange->end() - Elf::page_align_down(mrange->end() - 1);
    if (ppi.bytes > 0) {
	ppinfo.push_back(ppi);
    }

    ppi.bytes = Elf::PAGE_SIZE;
    for (unsigned int pgnum = start_pgnum + 1; pgnum < end_pgnum; ++pgnum) {
	ppi.page = _pages[pgnum];
	ppinfo.push_back(ppi);
    }

    return true;
}
	

string Vma::to_string() const
{
    stringstream sstr;
    sstr << hex;
    sstr << _range << ": " << _fname;
    return sstr.str();
}



// Helper comparison function for sort in calc_maps
static bool mapptr_starts_before(const MapPtr &a,
			      const MapPtr &b)
{
    return a->mem_range()->start() < b->mem_range()->start();
}

// Come up with a list of elf maps which exactly cover this vma.  We
// can refer back to the previous vma & file to clarify the various
// cases.
list<MapPtr> Vma::calc_maps(const FilePtr &file,
			    const VmaPtr &previous_vma,
			    const FilePtr &previous_file,
			    pid_t pid)
{
    list<MapPtr> maps;
    list<MapPtr>::iterator map_it;
    list<Elf::SegmentPtr> segs;
    RangePtr null_range;

    if (file->is_elf()) {

	// Case 1: This is an elf backed vma.  Then for each loadable
	// segment, we calculate the elf address->vma address offset
	// and work out which how much of the segment overlaps our
	// address space. We turn those into maps and any holes into
	// anon maps.

	segs = file->elf()->loadable_segments();
	list<Elf::SegmentPtr>::iterator it;
	list<RangePtr> mem_ranges;
	for (it = segs.begin(); it != segs.end(); ++it) {
	    Address seg_to_mem;
	    if (!get_seg_to_mem(*it, seg_to_mem)) {
		warn << pid << ": can't get seg_to_mem\n";
		continue;
	    }
	    RangePtr seg_mem_range = (*it)->mem_range()->add(seg_to_mem);
	    RangePtr subrange = seg_mem_range->intersect(*_range);
	    if (!subrange || subrange->size() <= 0) {
		continue;
	    }
	    RangePtr elf_subrange = subrange->subtract(seg_to_mem);
	    MapPtr map(new Map(selfptr(), subrange, elf_subrange));
	    maps.push_back(map);
	    file->add_map(map);
	    mem_ranges.push_back(map->mem_range());
	}

	// Add holes
	list<RangePtr> hole_ranges = _range->invert_list(mem_ranges);
	list<RangePtr>::iterator range_it;
	for (range_it = hole_ranges.begin();
	     range_it != hole_ranges.end();
	     ++range_it) {
	    MapPtr hmap(new Map(selfptr(), *range_it, null_range));
	    maps.push_back(hmap);
	}
    }
    else {

	// Cases 2a and 2b
	MapPtr non_elf_map;
	if (previous_vma
	    && previous_file
	    && previous_vma->is_file_backed()
	    && previous_file->is_elf()
	    && _range->start() == previous_vma->_range->end()) {

	    // Case 2a: This is not a file backed vma
	    // AND previous is file backed
	    // AND previous file is elf
	    // AND this vma is contiguous with the previous
	    //
	    // In which case we take the previous vma's segments and
	    // offsets and see if we have an overlap. We assert that
	    // the overlap will be at the beginning of this vma.  Any
	    // remainder gets added turned into an anon map
	    
	    list<Elf::SegmentPtr> prevsegs
		= previous_file->elf()->loadable_segments();
	    Address non_elf_start = start();

	    list<Elf::SegmentPtr>::iterator it;
	    for (it = prevsegs.begin(); it != prevsegs.end(); ++it) {
		Address seg_to_mem;
		if (!previous_vma->get_seg_to_mem(*it, seg_to_mem)) {
		    warn << pid << ": can't get seg_to_mem on previous vma\n";
		    continue;
		}
		RangePtr seg_mem_range = (*it)->mem_range()->add(seg_to_mem);
		RangePtr subrange = seg_mem_range->intersect(*_range);
		if (!subrange || subrange->size() <= 0) {
		    continue;
		}
		RangePtr elf_subrange = subrange->subtract(seg_to_mem);
		MapPtr map(new Map(selfptr(), subrange, elf_subrange));
		non_elf_start = subrange->end();
		maps.push_back(map);
		previous_file->add_map(map);
	    }

	    if (non_elf_start < end()) {
		RangePtr non_elf_range(new Range(non_elf_start, end()));
		non_elf_map.reset(new Map(selfptr(),
					  non_elf_range,
					  null_range));
	    }
	}
	else {

	    // (2b) This is not a file backed vma and some or all of
	    // the previous conditions do not hold. In which case we
	    // turn cover the entire vma with a non-elf map.
	    
	    non_elf_map.reset(new Map(selfptr(), _range, null_range));
	}
	if (non_elf_map) {
	    maps.push_back(non_elf_map);
	    file->add_map(non_elf_map);
	}
    }


    // We may not have the maps in ascending order of start address,
    // since we added the 'holes' at the end of case 1. Ensure by
    // sorting here.
    maps.sort(mapptr_starts_before);

    if (maps.empty()) {
	warn << pid << ": empty maps\n";
	return maps;
    }
    list<MapPtr> empty_maps;
    if (maps.front()->mem_range()->start() != _range->start()) {
	warn << pid << ": first map is not at start: "
	     << maps.front()->mem_range() << " : "
	     << _range << "\n";
	return empty_maps;
    }
    if (maps.back()->mem_range()->end() != _range->end()) {
	warn << pid << ": last map is not at end\n";
	return empty_maps;
    }
    
    RangePtr last_range;
    for (map_it = maps.begin(); map_it != maps.end(); ++map_it) {
	RangePtr range = (*map_it)->mem_range();
	if (range->size() <= 0) {
	    warn << pid << ": zero length map: " << range << "\n";
	}
	if (last_range &&
	    last_range->end() != range->start()) {
	    warn << pid << ": invalid map list: "
		 << last_range << " - " << range << "\n";
	    return empty_maps;
	}
    }
    
    return maps;
}
    
bool Vma::get_seg_to_mem(const Elf::SegmentPtr &seg,
			 Address &seg_to_mem)
{
    if (!is_file_backed()) {
	warn << "get_seg_to_mem called on non-file backed vma\n";
	return false;
    }

    Address segmem_base = seg->mem_range()->start() - seg->offset();
    Address vmamem_base = start() - offset();
    seg_to_mem = vmamem_base - segmem_base;
    return true;
}

// ------------------------------------------------------------

bool Page::parse_line(const string &line)
{
    stringstream sstr(line);

    int i;

    sstr >> i;
    _resident = i;
    sstr >> i;
    _writable = i;

    sstr >> hex >> _cookie;

    return true;
}

PageCookie Page::cookie() const
{
    return _cookie;
}

bool Page::is_mapped() const
{
    return _cookie != 0;
}

bool Page::is_resident() const
{
    return _resident;
}

bool Page::is_writable() const
{
    return _writable;
}

void Page::print(ostream &os) const
{
    os << "(" << _cookie << ":" << _resident << ":" << _writable << ")";
}

// ------------------------------------------------------------

Sizes::Sizes()
{
    for (int i = 0; i < NUM_SIZES; ++i) {
	_values[i] = 0;
    }
}

unsigned long Sizes::val(int which)
{
    return _values[which];
}

float Sizes::sval(int which)
{
    return (float) _values[which] / (float) _scale_factor;
}

void Sizes::increase(enum Measure which, unsigned long amount)
{
    _values[which] += amount;
}

void Sizes::add(const SizesPtr &other)
{
    for (int i = 0; i < NUM_SIZES; ++i) {
	_values[i] += other->_values[i];
    }
}

string Sizes::size_name(int which)
{
    string name = names[which];
    if (!_scale_name.empty()) {
	name += " (" + _scale_name + ")";
    }
    return name;
}

void Sizes::scale_units()
{
    _scale_factor = 1;
    _scale_name = "";
}

void Sizes::scale_kbytes()
{
    _scale_factor = 1024;
    _scale_name = "K";
}

void Sizes::scale_mbytes()
{
    _scale_factor = 1024 * 1024;
    _scale_name = "M";
}

const string Sizes::names[] = {
    "Effective Resident",
    "Effective Mapped",
    "Writable",
    "VM",
    "Sole Mapped",
    "Mapped",
    "Resident",
    "Num Sizes"
};

int Sizes::_scale_factor = 1;
string Sizes::_scale_name;

// ------------------------------------------------------------

File::File(const string &fname)
    : _fname(fname)
{
    if (file_exists(fname)) {
	_elf.reset(new Elf::File);
	if (!_elf->load(_fname, false)) {
	    _elf.reset((Elf::File *) 0);
	}
    }
}

string File::name()
{
    return _fname;
}

list<ProcessPtr> File::procs()
{
    list<ProcessPtr> result(_procs.begin(), _procs.end());
    return result;
}

Elf::FilePtr File::elf()
{
    return _elf;
}

bool File::is_elf()
{
    return _elf != 0;
}

list<MapPtr> File::maps()
{
    return _maps;
}

SizesPtr File::sizes()
{
    ProcessPtr proc = *(_procs.begin());
    return Map::sum_sizes(proc->page_pool(),
			  _maps);
}

void File::add_map(const MapPtr &map)
{
    _maps.push_back(map);
}

void File::add_proc(const ProcessPtr &proc)
{
    _procs.insert(proc);
}

// ------------------------------------------------------------

Map::Map(const VmaPtr &vma,
	 const RangePtr &mem_range,
	 const RangePtr &elf_range)
    : _vma(vma), _mem_range(mem_range), _elf_range(elf_range)
{ }

RangePtr Map::mem_range()
{
    return _mem_range;
}

RangePtr Map::elf_range()
{
    return _elf_range;
}

Address Map::elf_to_mem_offset()
{
    return _mem_range->start() - _elf_range->start();
}


RangePtr Map::elf_to_mem_range(const RangePtr &elf_range)
{
    if (!_elf_range->contains(*elf_range)) {
	warn << "Range " << elf_range->to_string()
	     << " not contained within " << _elf_range->to_string() << "\n";
	return RangePtr((Range *) 0);
    }

    return elf_range->add(elf_to_mem_offset());
}

string Map::to_string() const
{
    stringstream sstr;

    sstr << "MAP: MEM " << _mem_range->to_string()
	 << " ELF " << (_elf_range ? _elf_range->to_string() : "undef")
	 << " FILE " << _vma->fname();
    return sstr.str();
}

SizesPtr Map::sizes_for_mem_range(const PagePoolPtr &pp)
{
    return sizes_for_mem_range(pp, _mem_range);
}

SizesPtr Map::sizes_for_mem_range(const PagePoolPtr &pp,
				  const RangePtr &mrange)
{
    SizesPtr null_sizes;
    SizesPtr sizes(new Sizes);
    RangePtr subrange = _mem_range->intersect(*mrange);

    if (!subrange) {
	warn << "Non-overlapping range: " << *mrange
	     << " not within " << _mem_range << "\n";
	return null_sizes;
    }
    
    if (subrange->size() == 0) { return sizes; }
    
    std::list<Vma::PartialPageInfo> ppi_info;
    if (!_vma->get_pages_for_range(subrange, ppi_info)) {
	warn << "Can't get pages for range\n";
	return null_sizes;
    }

    std::list<Vma::PartialPageInfo>::iterator it;

    // Do the effectiveness calculations as floats to help minimise
    // rounding error (which starts to get significant for heavily
    // shared pages)
    float effective_resident = 0.0;
    float effective_mapped = 0.0;
    
    for (it = ppi_info.begin(); it != ppi_info.end(); ++it) {
	Page page((*it).page);
	int count = pp->count(page);
	int bytes = (*it).bytes;

	if (count <= 0) {
	    warn << "Invalid count for page\n";
	    continue;
	}

	sizes->increase(Sizes::VM, bytes);

	if (page.is_mapped()) {
	    sizes->increase(Sizes::MAPPED, bytes);
	    //	    sizes->increase(Sizes::EFFECTIVE_MAPPED, bytes / count);
	    effective_mapped += (float) bytes / count;
	    if (count == 1) {
		sizes->increase(Sizes::SOLE_MAPPED, bytes);
	    }

	    if (page.is_resident()) {
		sizes->increase(Sizes::RESIDENT, bytes);
		//sizes->increase(Sizes::EFFECTIVE_RESIDENT, bytes / count);
		effective_resident += (float) bytes / count;

		if (page.is_writable()) {
		    sizes->increase(Sizes::WRITABLE, bytes);
		}
	    }
	}
    }
    
    sizes->increase(Sizes::EFFECTIVE_MAPPED,
		    (unsigned long) effective_mapped);
    sizes->increase(Sizes::EFFECTIVE_RESIDENT,
		    (unsigned long) effective_resident);

    if (sizes->val(Sizes::VM) != subrange->size()) {
	warn << "Size mismatch: vm size " << sizes->val(Sizes::VM)
	     << " range " << subrange->to_string() << "\n";
	return null_sizes;
    }

    return sizes;
}

void Map::print(std::ostream &os) const
{
    os << to_string();
}

SizesPtr Map::sum_sizes(const PagePoolPtr &pp,
			const list<MapPtr> &maps)
{
    SizesPtr sizes(new Sizes);
    list<MapPtr>::const_iterator map_it;
    for (map_it = maps.begin(); map_it != maps.end(); ++map_it) {
	SizesPtr subsizes = (*map_it)->sizes_for_mem_range(pp);
	sizes->add(subsizes);
    }
    return sizes;
}

// ------------------------------------------------------------

SysInfo::~SysInfo()
{ }

LinuxSysInfo::~LinuxSysInfo()
{ }

const std::string LinuxSysInfo::EXMAP_FILE("/proc/exmap");

list<pid_t> LinuxSysInfo::accessible_pids()
{
    list<pid_t> pids;
    pid_t pid;
    list<string> fnames;
    list<string>::iterator it;
    
    if (!read_directory("/proc", fnames)) {
	warn << "accessible_pids - can't read /proc\n";
	return pids; // empty return
    }

    for (it = fnames.begin(); it != fnames.end(); ++it) {
        if (isdigit((*it)[0])) {
	    pid = atoi(it->c_str());
	    string mapfile = proc_map_file(pid);
	    if (file_readable(mapfile)) {
	      pids.push_back(pid);
	    }
	}
    }

    return pids;
}

bool LinuxSysInfo::sanity_check()
{
    if (!file_exists(EXMAP_FILE)) {
	warn << "Can't find file " << EXMAP_FILE
	     << ": please check kernel module is loaded\n";
	return false;
    }

    return true;
}

bool LinuxSysInfo::read_page_info(pid_t pid,
				  map<Address, list<Page> > &page_info)
{
    list<string> lines;
    page_info.clear();

    stringstream sstr;
    sstr << pid << "\n";
    if (!overwrite_textfile(EXMAP_FILE, sstr.str())) {
	warn << "read_page_info - can't write to exmap: "
	     << pid <<"\n";
	return false;
    }

    if (!read_textfile(EXMAP_FILE, lines)) {
	warn << "read_page_info - can't read exmap: " << pid << "\n";
	return false;
    }

    list<Page> empty_pagelist;
    list<Page> *current_pagelist = NULL;
    
    list<string>::const_iterator it;
    for (it = lines.begin(); it != lines.end(); ++it) {
        // Lines are either:
        // Start a new VMA:
        // VMA 0xdeadbeef <npages>
        // or
        // Page info
        // <pfn> <writable:bool> <swap_entry>
	if (it->length() < 3) {
	    warn << "read_page_info - short line: " << *it << "\n";
	    continue;
	}

	if (it->substr(0, 3) == "VMA") {
	    Address start_addr;
	    string token;
	    sstr.str(it->substr(6)); // "VMA 0xdeadbeef"
	    dbg << "line is " << (*it);
	    sstr >> hex >> start_addr;
	    dbg << "start is " << start_addr << "\n";

	    // Put a (copy of) the empty pagelist in for this address
	    page_info[start_addr] = empty_pagelist;

	    // And remember where to put the pages
	    current_pagelist = &(page_info[start_addr]);
	}
	else {
	    Page page;
	    if (page.parse_line(*it)) {
		if (current_pagelist == NULL) {
		    warn << "read_page_info: page info line before VMA\n";
		    continue;
		}
		current_pagelist->push_back(page);
	    }
	    else {
		warn << "read_page_info - bad page line:" << pid << "\n";
		continue;
	    }
	}
    }
    
    return true;
}

std::string LinuxSysInfo::read_cmdline(pid_t pid)
{
    stringstream fname;
    list<string> lines;
    string cmdline;

    fname << "/proc/" << pid << "/cmdline";
    if (read_textfile(fname.str(), lines)
	&& lines.size() == 1) {
	cmdline = lines.front();
    }
    

    return cmdline;
}

bool LinuxSysInfo::read_vmas(const PagePoolPtr &pp,
			     pid_t pid,
			     list<VmaPtr> &vmas)
{
    vmas.clear();
    string mapfile = proc_map_file(pid);

    list<string> lines;

    if (!read_textfile(mapfile, lines)) {
	warn << "read_vmas - can't load maps for: " << pid << "\n";
	return false;
    }

    list<string>::iterator it;
    for(it = lines.begin(); it != lines.end(); ++it) {
	VmaPtr vma = parse_vma_line(*it);
	if (!vma) {
	    warn << "read_vmas - can't parse line\n";
	    continue;
	}
	vma->selfptr(vma);
	vmas.push_back(vma);
    }
    return true;
}



VmaPtr LinuxSysInfo::parse_vma_line(const string &line_arg)
{
    const string ANON_NAME("[anon]");
    string line(line_arg);

    string perms;
    Elf::Address start, end;
    off_t offset;
    std::string fname;

    // Break the range into two hex numbers
    line[8] = ' ';
    
    stringstream sstr(line);
    sstr >> hex >> start;
    sstr >> end;
    sstr >> perms;
    sstr >> offset;
    if (line.length() >= 49) {
	fname = line.substr(49);
	chomp(fname);
    }
    else {
	fname = ANON_NAME;
    }

    VmaPtr vma(new Vma(start, end, offset, fname));

    return vma;
}

string LinuxSysInfo::proc_map_file(pid_t pid)
{
    stringstream sstr;
    sstr << "/proc/" << pid << "/maps";
    return sstr.str();
}
