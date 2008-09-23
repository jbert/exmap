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
	warn << "Snapshot::load - failed to load: processe\n";
	return false;
    }
    
    if (!calculate_file_mappings()) {
    	warn << "Snapshot::load - failed to load: calculate file mappings\n";
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
	if (!(*it)->calculate_maps(_file_pool)) {
	    warn << "Failed to process maps for pid " << (*it)->pid() << "\n";
	    return false;
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
    dbg << _pid << ": " << "rmtf " << file->name() << ": "
	<< proc_maps.size() << " procs "
	<< file_maps.size() << " files\n";
    
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

bool Process::calculate_maps(FilePoolPtr &file_pool)
{
    MapCalculator mc(_vmas, file_pool, selfptr());

    return mc.calc_maps(_maps) && !_maps.empty();
}



bool Process::load_page_info(SysInfoPtr &sys_info)
{
    map<Address, list<Page> > page_info;

    if (!sys_info->read_page_info(_pid, page_info)) {
	warn << "Can't read page info for " << _pid;
	return false;
    }

    map<Address, list<Page> >::iterator pi_it;
    for (pi_it = page_info.begin(); pi_it != page_info.end(); ++pi_it) {
	Address start_address = pi_it->first;
	VmaPtr vma;
	if (!find_vma_by_addr(start_address, vma)) {
	    // This can happen, a process can alloc whilst we are
	    // running
	    warn << "Process::load_page_info - can't find vma at "
		 << hex << start_address << dec << ": " << _pid << "\n";
	    continue;
	}
	
	vma->add_pages(pi_it->second);
	_page_pool->inc_pages_count(pi_it->second);
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

Elf::Address Vma::start() { return _range->start(); }

Elf::Address Vma::end() { return _range->end(); }

RangePtr Vma::range() { return _range; }

std::string Vma::fname() { return _fname; }

off_t Vma::offset() { return _offset; }

Elf::Address Vma::vm_size() { return _range->size(); }

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
	warn << hex << addr << " is beyond vma end " << end() << dec << "\n";
	return false;
    }
    Address a = Elf::page_align_down(addr);
    if (a < start()) {
	warn << hex << addr << " is before vma start "
	    << start() << dec << "\n";
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
    if (!range()->contains(*mrange)) {
	warn << "Vma::get_pages_for_range - range "
	    << mrange->to_string() << " outside vma " << to_string() << "\n";
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
    sstr << _range << ": " << _offset << " " << _fname;
    return sstr.str();
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
	_values[i] = 0.0;
    }
}

double Sizes::val(int which)
{
    return _values[which];
}

double Sizes::sval(int which)
{
    return _values[which] / (double)_scale_factor;
}

void Sizes::increase(enum Measure which, double amount)
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

void File::add_maps(const list<MapPtr> &maps)
{
    _maps.insert(_maps.end(), maps.begin(), maps.end());
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
	warn << "sizes_for_mem_range: Can't get pages for range "
	    << subrange->to_string() << "\n";
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
	    sstr >> hex >> start_addr;

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



    
MapCalculator::MapCalculator(const list<VmaPtr> &vmas,
			     FilePoolPtr &file_pool,
			     const ProcessPtr &proc)
: _vmas(vmas), _file_pool(file_pool), _proc(proc)
{
    _covered_to = _vmas.front()->start();
}

bool MapCalculator::calc_maps(list<MapPtr> &maps)
{
    list<MapPtr> file_maps;
    list<VmaPtr>::const_iterator vma_it;

    maps.clear();

    FilePtr file;

    // We need to continue until we've covered all the vmas with
    // maps. Since methods will consume vmas unpredictably, we'll while()
    // loop, so that sub-functions can consume as they go.
    while (!_vmas.empty()) {

	file = _file_pool->get_or_make_file(_vmas.front()->fname());

	file->add_proc(_proc);
	_proc->add_file(file);

	list<VmaPtr> consumed_vmas;
	file_maps.clear();

	if (file->is_elf()) {
	    if (!calc_elf_file(file, file_maps, consumed_vmas)) {
		warn << "calc_maps: calc_elf_file failed\n";
		return false;
	    }
	}
	else {
	    if (!calc_nonelf_file(file, file_maps, consumed_vmas)) {
		warn << "calc_maps: cal_nonelf_file failed\n";
		return false;
	    }
	}
	if(!check_file_maps(file_maps, consumed_vmas)) {
	    warn << _proc->pid() << ": error in vma maps\n";
	    return false;
	}
	file->add_maps(file_maps);
	maps.insert(maps.end(), file_maps.begin(), file_maps.end());
    }
    return check_no_overlaps(maps);
}

bool MapCalculator::calc_elf_file(const FilePtr &file,
				  list<MapPtr> &file_maps,
				  list<VmaPtr> &consumed_vmas)
{
    list<Elf::SegmentPtr> segs = file->elf()->loadable_segments();
    list<Elf::SegmentPtr>::const_iterator seg_it;

    stringstream prefix;
    prefix << _proc->pid() << "calc_elf_file: ";

    RangePtr overrun;
    for (seg_it = segs.begin(); seg_it != segs.end(); ++seg_it) {
	VmaPtr vma = _vmas.front();

	if (!calc_seg_map(*seg_it, file, vma, file_maps, overrun)) {
	    warn << prefix.str() << "calc_seg_map failed\n";
	    return false;
	}
	if (!consume_to(vma->end(), consumed_vmas)) {
	    warn << prefix.str() << "can't consume to seg map end\n";
	    return false;
	}

	if (overrun) {
	    if (_vmas.empty()) {
		warn << prefix.str() << "no vma for overrun\n";
		return false;
	    }
	    VmaPtr previous_vma = vma;
	    vma = _vmas.front();
	    MapPtr overrun_map = calc_overrun_map(vma,
		    previous_vma,
		    overrun,
		    *seg_it);
	    if (!overrun_map) {
		warn << prefix.str() << "failed to calculate overrun map\n";
		return false;
	    }
	    if (!consume_to(overrun_map->mem_range()->end(), consumed_vmas)) {
		warn << prefix.str() << "can't consume to overrun end\n";
		return false;
	    }
	    file_maps.push_back(overrun_map);
	}
    }

    return true;
}

bool MapCalculator::calc_seg_map(const Elf::SegmentPtr &seg,
	const FilePtr &file,
	const VmaPtr &vma,
	list<MapPtr> &seg_maps,
	RangePtr &overrun)
{
    RangePtr null_range;
    stringstream prefix;
    prefix << _proc->pid() << " calc_seg_map: "; 

    dbg << prefix.str() << "VMA " << vma->to_string() << "\n";

    RangePtr working_range = vma->range()->truncate_below(_covered_to);
    if (working_range->size() <= 0) {
	warn << prefix.str() << "start address mismatch : " << vma->to_string()
	    << ", " << hex << _covered_to << dec << "\n";
	return false;
    }

    overrun.reset();
    Address seg_to_mem = get_seg_to_mem(seg, vma);
    RangePtr seg_mem_range = seg->mem_range()->add(seg_to_mem);

    if (seg_mem_range->start() < working_range->start()) {
	warn << prefix.str() << "segment and start mismatch: "
	    << hex << seg->mem_range() << ", "
	    << working_range << ", " << seg_to_mem << dec << "\n";
	return false;
    }

    // Note - segment may extend beyond vma, but should cover it (modulo
    // page size)
    if (Elf::page_align_up(seg_mem_range->end()) < working_range->end()) {
	warn << prefix.str() << "segment and vma end mismatch: "
	    << hex << seg->mem_range() << ", "
	    << working_range << ", " << seg_to_mem << dec << "\n";
	return false;
    }

    // Cover any gap at the beginning
    RangePtr start_hole(new Range(working_range->start(), seg_mem_range->start()));
    if (start_hole->size() > 0) {
	MapPtr start_hole_map(new Map(vma, start_hole, null_range));
	seg_maps.push_front(start_hole_map);
	dbg << prefix.str() << "start hole " << start_hole_map->to_string() << "\n";
    }

    // Handle the middle
    RangePtr subrange = seg_mem_range->intersect(*working_range);
    if (!subrange || subrange->size() <= 0) {
	warn << prefix.str() << "empty subrange "
	    << vma->to_string() << ", "
	    << working_range << ", "
	    << seg->mem_range()->to_string() << ", "
	    << hex << seg_to_mem << dec << "\n";
	return false;
    }
    RangePtr elf_subrange = subrange->subtract(seg_to_mem);
    MapPtr map(new Map(vma, subrange, elf_subrange));
    seg_maps.push_back(map);
    dbg << prefix.str() << "adding elf map " << map->to_string() << "\n";

    // Cover any gap at the end of the vma
    if (map->mem_range()->end() < vma->end()) {
	RangePtr end_hole(new Range(map->mem_range()->end(), vma->end()));
	MapPtr end_hole_map(new Map(vma, end_hole, null_range));
	seg_maps.push_back(end_hole_map);
	dbg << prefix.str() << "end hole " << end_hole_map->to_string() << "\n";
    }

    // Cool. VMA is covered.
    //
    // Only thing to worry about now is if the seg extends into the next vma.

    if (map->elf_range()->end() > seg->mem_range()->end()) {
	warn << prefix.str() << "map elf range extends beyond seg\n";
    }
    if (map->elf_range()->end() < seg->mem_range()->end()) {
	// We've got some over-run into the next vma. Record the elf range.
	overrun.reset(new Range(map->elf_range()->end(),
				seg->mem_range()->end()));
    }
    
    return true;
}

bool MapCalculator::consume_to(Address addr, list<VmaPtr> &consumed_vmas)
{
    stringstream prefix;
    prefix << _proc->pid() << ": consume_to (" << hex << addr << dec <<"): ";
    if (addr <= _covered_to) {
	warn << prefix.str() << "addr <= covered_to "
	    << hex << _covered_to << dec << "\n";
	return false;
    }

    if (_vmas.empty()) {
	warn << prefix.str() << "no vmas to consume\n";
	return false;
    }

    VmaPtr &vma = _vmas.front();
    _covered_to = addr;

    if (addr < vma->end()) {
	// Not finished with this vma
	return true;
    }
    else {
	dbg << prefix.str() << "consuming vma: " << vma->to_string() << "\n";
	consumed_vmas.push_back(vma);
	_vmas.pop_front();
	if (addr == vma->end()) {
	    return true;
	}
	else {
	    if (_vmas.empty()) {
		warn << prefix.str() << "addr beyond last vma\n";
		return false;
	    }
	    if (addr >= _vmas.front()->end()) {
		warn << prefix.str() << "consuming two vmas\n";
		return false;
	    }
	}
    }
    return true;
}

MapPtr MapCalculator::calc_overrun_map(const VmaPtr &vma,
	const VmaPtr &previous_vma,
	const RangePtr &overrun_elf_range,
	const Elf::SegmentPtr &seg)
{
    MapPtr result; // null return is failure
    stringstream prefix;
    prefix << _proc->pid() << ": calc_overrun_map: ";

    RangePtr null_range;
    if (vma->start() != previous_vma->end()) {
	warn << prefix.str() << "non-contiguous vmas: " << vma
	    << ", " << previous_vma << "\n";
	return result;
    }

    Address seg_to_mem = get_seg_to_mem(seg, previous_vma);
    RangePtr overrun_mem_range = overrun_elf_range->add(seg_to_mem);

    result.reset(new Map(vma,
		overrun_mem_range,
		overrun_elf_range));

    dbg << prefix.str() << "adding overrun map " << result->to_string() << "\n";

    return result;
}

bool MapCalculator::calc_nonelf_file(const FilePtr &file,
	list<MapPtr> &file_maps,
	list<VmaPtr> &consumed_vmas)
{
    stringstream prefix;
    prefix << _proc->pid() << " calc_nonelf_file: "; 

    // We'll just cover the whole vma (from _covered_to upwards)
    // This will either cover the whole vma or just the end, depending on
    // whether we have 'overflow' from the previous vma or not.
    VmaPtr vma = _vmas.front();

    dbg << prefix.str() << "VMA " << vma->to_string() << "\n";

    RangePtr null_range;
    RangePtr working_range = vma->range()->truncate_below(_covered_to);
    dbg << prefix.str() << vma->to_string()
	<< " range "<< working_range->to_string() << "\n";
    if (working_range->size() <= 0) {
	warn << prefix.str() << "invalid nonelf working range: "
	    << vma->range()->to_string()
	    << hex << " covered to " << _covered_to << dec << "\n";
	return false;
    }
    if (!consume_to(working_range->end(), consumed_vmas)) {
	warn << prefix.str() << "failed to consume vma\n";
	return false;
    }

    MapPtr non_elf_map(new Map(vma, working_range, null_range));
    file_maps.push_back(non_elf_map);

    return true;
}

bool MapCalculator::check_file_maps(const std::list<MapPtr> &maps,
	const std::list<VmaPtr> &consumed_vmas)
{
    stringstream prefix;
    prefix << _proc->pid() << " check_file_maps: "; 

    if (consumed_vmas.empty()) {
	warn << prefix.str() << "no vmas consumed\n";
	return false;
    }

    if (maps.empty()) {
	warn << prefix.str() << "empty maps list\n";
	return false;
    }

    const VmaPtr &first_vma = consumed_vmas.front();
    if (maps.front()->mem_range()->start() < first_vma->start()) {
	warn << prefix.str() << "first map is not after start: "
	     << maps.front()->mem_range() << " : "
	     << first_vma->to_string() << "\n";
	return false;
    }

    // Last may not equal end since we could have overrun partially into next vma
    // and so not consumed it.
    const VmaPtr &last_vma = consumed_vmas.back();
    if (maps.back()->mem_range()->end() < last_vma->end()) {
	warn << prefix.str() << "last map is not before consumed end: "
	     << maps.back()->mem_range() << " : "
	     << last_vma->to_string() << "\n";
	return false;
    }
    
    RangePtr last_range;
    list<MapPtr>::const_iterator map_it;
    for (map_it = maps.begin(); map_it != maps.end(); ++map_it) {
	RangePtr range = (*map_it)->mem_range();
	if (range->size() <= 0) {
	    warn << prefix.str() << "zero length map: " << range << "\n";
	}
	if (last_range &&
	    last_range->end() != range->start()) {
	    warn << prefix.str() << "invalid map list: "
		 << last_range << " - " << range << "\n";
	    return false;
	}
    }
    
    return true;
}

bool MapCalculator::check_no_overlaps(std::list<MapPtr> &maps)
{
    list<RangePtr> ranges;
    list<MapPtr>::iterator map_it;

    for (map_it = maps.begin(); map_it != maps.end(); ++map_it) {
	ranges.push_back((*map_it)->mem_range());
    }

    if (Range::any_overlap(ranges)) {
	warn << _proc->pid() << ": overlapping vma ranges\n";
	dbg << "overlapping ranges:\n";
	dbg << ranges << "\n";
	return false;
    }

    return true;
}

Elf::Address MapCalculator::get_seg_to_mem(const Elf::SegmentPtr &seg,
	const VmaPtr &vma)
{
    Address segmem_base = seg->mem_range()->start() - seg->offset();
    Address vmamem_base = vma->start() - vma->offset();
    return vmamem_base - segmem_base;
}
