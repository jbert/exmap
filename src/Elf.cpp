/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#include "Elf.hpp"
#include "jutil.hpp"

#include <sstream>

using namespace std;
using namespace jutil;
using namespace Elf;




Address Elf::page_align_down(const Address &addr)
{
    return addr & ~(PAGE_SIZE-1);
}

Address Elf::page_align_up(const Address &addr)
{
    return page_align_down(addr + PAGE_SIZE - 1);
}

// ------------------------------------------------------------

File::File()
    : _lazy_load_sections(false)
{ }

bool File::load(const string &fname, bool warn_if_non_elf)
{
    unload();
    _fname = fname;

    if (!is_regular_file(fname)) {
	return false;
    }
    
    if (!open_file()) {
	warn << "File::load - can't open file: " << fname << "\n";
	return false;
    }
    if (!load_file_header()) {
	if (warn_if_non_elf) {
	    warn << "File::load - failed to load header: " << fname << "\n";
	}
	return false;
    }
    if (!load_segments()) {
	warn << "File::load - failed to load segment info: " << fname << "\n";
	return false;
    }
    return true;
}
     
list<SymbolPtr> File::find_symbols_in_mem_range(const RangePtr &mrange)
{
    if (!lazy_load_sections() || !_symbol_table_section) {
	list<SymbolPtr> empty;
	return empty;
    }
    return _symbol_table_section->find_symbols_in_mem_range(mrange);
}

list<SymbolPtr> File::defined_symbols()
{
    list<SymbolPtr> syms;
    list<SymbolPtr> result;
    list<SymbolPtr>::iterator it;
    
    syms = all_symbols();
    for (it = syms.begin(); it != syms.end(); ++it) {
	if ((*it)->is_defined()) {
	    result.push_back(*it);
	}
    }
    return result;
}

list<SymbolPtr> File::all_symbols()
{
    if (!lazy_load_sections()
	|| !_symbol_table_section) {
	list<SymbolPtr> empty;
	return empty;
    }
    return _symbol_table_section->symbols();
}

SymbolPtr File::symbol(const string &symname)
{
    list<SymbolPtr> syms = all_symbols();
    list<SymbolPtr>::iterator it;
    for (it = syms.begin(); it != syms.end(); ++it) {
	if ((*it)->name() == symname) {
	    return *it;
	}
    }
    return SymbolPtr((Symbol *) 0);
}

list<SymbolPtr> File::symbols_in_section(const SectionPtr &section)
{
    list<SymbolPtr> result;
    if (!lazy_load_sections() || !section) {
	list<SymbolPtr> empty;
	return empty;
    }

    return find_symbols_in_mem_range(section->mem_range());
}

string File::filename()
{
    return _fname;
}

int File::num_sections()
{
    return sections().size();
}

int File::num_segments()
{
    return segments().size();
}

list<SegmentPtr> File::segments()
{
    return _segments;
}

list<SegmentPtr> File::loadable_segments()
{
    list<SegmentPtr> result;
    list<SegmentPtr>::iterator it;
    for (it = _segments.begin(); it != _segments.end(); ++it) {
	if ((*it)->is_load()) {
	    result.push_back(*it);
	}
    }
    return result;
}

SectionPtr File::section(int i)
{
    list<SectionPtr>::iterator it;

    if (lazy_load_sections()) {
	for (it = _sections.begin(); it != _sections.end(); ++it) {
	    if (i-- == 0) {
		return *it;
	    }
	}
    }
    
    return SectionPtr((Section *) 0);
}

SectionPtr File::section(const string &name)
{
    list<SectionPtr>::iterator it;
    
    if (lazy_load_sections()) {
	for (it = _sections.begin(); it != _sections.end(); ++it) {
	    if ((*it)->name() == name) {
		return *it;
	    }
	}
    }
    
    return SectionPtr((Section *) 0);
}

list<SectionPtr> File::mappable_sections()
{
    list<SectionPtr> result;
    list<SectionPtr>::iterator it;

    if (!lazy_load_sections()) {
	list<SectionPtr> empty;
	return empty;
    }

    for (it = _sections.begin(); it != _sections.end(); ++it) {
	if ((*it)->addr() != 0) {
	    result.push_back(*it);
	}
    }
    return result;
}

list<SectionPtr> File::sections()
{
    if (!lazy_load_sections()) {
	list<SectionPtr> empty;
	return empty;
    }
    return _sections;
}

int File::elf_file_type()
{
    return _header.e_type;
}

bool File::is_executable()
{
    return elf_file_type() == ET_EXEC;
}

bool File::is_shared_object()
{
    return elf_file_type() == ET_DYN;
}



bool File::lazy_load_sections()
{
    // Stop possible recursion
    if (_lazy_load_sections) {
	return true;
    }
    _lazy_load_sections = true;
    if (!load_sections()) { return false; }
    return correlate_string_sections();
}

void File::unload()
{
    _lazy_load_sections = false;
    _ifs.clear();
    _ifs.close();
    _fname.clear();
    memset(&_header, 0, sizeof(_header));
    _segments.clear();
    _sections.clear();
    _symbol_table_section.reset();
}

bool File::open_file()
{
    uid_t new_euid = 0, orig_euid = 0;

    // If we are root, become the file owner. Otherwise, we might not
    // be able to open the file (e.g. a file on an NFS mount with root
    // squash).
    orig_euid = geteuid();
    if (orig_euid == 0
	&& file_owner(_fname, new_euid)
	&& new_euid != 0) {
	seteuid(new_euid);
    }
    
    if (_ifs.is_open()) {
	_ifs.close();
    }
    _ifs.clear();
    _ifs.open(_fname.c_str());
    if (!_ifs.is_open()) {
	warn << "Can't open file: " << _fname << "\n";
    }

    if (new_euid != 0) {
	seteuid(orig_euid); // ok, it will always be 0...
    }
    
    return _ifs.is_open();
}

bool File::correlate_string_sections()
{
    list<SectionPtr>::iterator it;
    SectionPtr string_table;
    int string_table_index = _header.e_shstrndx;
    if (string_table_index >= num_sections()) {
	warn << "correlate_string_sections - invalid string index\n";
	return false;
    }
    string_table = section(string_table_index);
    if (!string_table->is_string_table()) {
	warn << "correlate_string_sections - invalid string section\n";
	return false;
    }

    for (it = _sections.begin(); it != _sections.end(); ++it) {
	(*it)->set_name(_ifs, string_table);
	if ((*it)->is_symbol_table()) {
	    _symbol_table_section = *it;
	    SectionPtr string_table = section((*it)->link());
	    (*it)->load_symbols(_ifs,
				string_table);
	}
    }
    return true;
}

bool File::load_file_header()
{
    memset(&_header, '\0', sizeof(_header));
    
    binary_read(_ifs, _header);

    if (memcmp(_header.e_ident, "\x7f\x45\x4c\x46", 4) != 0) {
	return false;
    }
    return _ifs.good();
}

bool File::load_sections()
{
    list<string> entries;
    list<string>::iterator it;
    
    if (!load_table(_ifs,
		    "section header table",
		    _header.e_shoff,
		    _header.e_shnum,
		    _header.e_shentsize,
		    entries)
	|| entries.empty()) {
	return false;
    }

    for (it = entries.begin(); it != entries.end(); ++it) {
	SectionPtr s(new Section);
	if (s->init(*it)) {
	    _sections.push_back(s);
	}
	else {
	    warn << "Failed to init section\n";
	}
    }
    return !_sections.empty();
}

bool File::load_segments()
{
    list<string> entries;
    list<string>::iterator it;
    
    if (!load_table(_ifs,
		    "segment header table",
		    _header.e_phoff,
		    _header.e_phnum,
		    _header.e_phentsize,
		    entries)
	|| entries.empty()) {
	return false;
    }

    for (it = entries.begin(); it != entries.end(); ++it) {
	SegmentPtr s(new Segment);
	if (s->init(*it)) {
	    _segments.push_back(s);
	}
	else {
	    warn << "Failed to init segment\n";
	}
    }
    return !_segments.empty();
}

bool File::load_table(istream &is,
		      const std::string &table_name,
		      Elf32_Off offset,
		      Elf32_Half num_chunks,
		      Elf32_Half chunksize,
		      list<string> &entries)
{
    entries.clear();
    
    if (offset == 0) {
	warn << "No " << table_name << " present\n";
	return false;
    }
    if (num_chunks < 1) {
	warn << "Invalid number of chunks " << num_chunks
	     << " in " << table_name << "\n";
	return false;
    }

    is.seekg(offset, ios_base::beg);
    if (!is.good()) {
	warn << "Can't seek to offset: " << offset << "\n";
	return false;
    }

    unsigned long read_size = num_chunks * chunksize;
    char *buffer = (char *) alloca(read_size);
    is.read(buffer, read_size);
    if (!is.good()) {
	warn << "Can't read " << read_size << " bytes\n";
	return false;
    }

    for (unsigned int i = 0; i < read_size; i += chunksize) {
	string s(buffer + i, chunksize);
	entries.push_back(s);
    }

    return true;
}

// ------------------------------------------------------------

/*
typedef struct
{
  Elf32_Word	p_type;			// Segment type
  Elf32_Off	p_offset;		// Segment file offset
  Elf32_Addr	p_vaddr;		// Segment virtual address
  Elf32_Addr	p_paddr;		// Segment physical address
  Elf32_Word	p_filesz;		// Segment size in file
  Elf32_Word	p_memsz;		// Segment size in memory
  Elf32_Word	p_flags;		// Segment flags
  Elf32_Word	p_align;		// Segment alignment
} Elf32_Phdr;
*/
bool Segment::init(const string &buffer)
{
    memcpy(&_seghdr, buffer.data(), sizeof(_seghdr));
    _mem_range.reset(new Range(_seghdr.p_vaddr,
			       _seghdr.p_vaddr + _seghdr.p_memsz));
    _file_range.reset(new Range(_seghdr.p_offset,
				_seghdr.p_offset + _seghdr.p_filesz));
    return true;
}

RangePtr Segment::mem_range()
{
    return _mem_range;
}

RangePtr Segment::file_range()
{
    return _file_range;
}

Address Segment::offset()
{
    return _seghdr.p_offset;
}

bool Segment::is_load()
{
    return _seghdr.p_type == PT_LOAD;
}

bool Segment::is_readable()
{
    return flag_is_set(PF_R);
}

bool Segment::is_writable()
{
    return flag_is_set(PF_W);
}

bool Segment::is_executable()
{
    return flag_is_set(PF_X);
}

bool Segment::flag_is_set(int flag)
{
    return _seghdr.p_flags & flag;
}

// ------------------------------------------------------------

/*
typedef struct
{
  Elf32_Word	sh_name;		// Section name (string tbl index)
  Elf32_Word	sh_type;		// Section type
  Elf32_Word	sh_flags;		// Section flags
  Elf32_Addr	sh_addr;		// Section virtual addr at execution
  Elf32_Off	sh_offset;		// Section file offset
  Elf32_Word	sh_size;		// Section size in bytes
  Elf32_Word	sh_link;		// Link to another section
  Elf32_Word	sh_info;		// Additional section information
  Elf32_Word	sh_addralign;		// Section alignment
  Elf32_Word	sh_entsize;		// Entry size if section holds table
} Elf32_Shdr;
*/

bool Section::init(const string &buffer)
{
    memcpy(&_secthdr, buffer.data(), sizeof(_secthdr));
    _file_range.reset(new Range(_secthdr.sh_offset,
				_secthdr.sh_offset + _secthdr.sh_size));
    _mem_range.reset(new Range(_secthdr.sh_addr,
			       _secthdr.sh_addr + _secthdr.sh_size));
    return true;
}

string Section::name()
{
    return _name;
}

bool Section::set_name(istream &is, const SectionPtr &string_table)
{
    int name_index = _secthdr.sh_name;
    _name = string_table->find_string(is, name_index);
    return is.good();
}

Elf32_Word Section::link()
{
    return _secthdr.sh_link;
}

Elf32_Addr Section::addr()
{
    return _secthdr.sh_addr;
}

Elf32_Word Section::size()
{
    return _secthdr.sh_size;
}

RangePtr Section::file_range()
{
    return _file_range;
}

RangePtr Section::mem_range()
{
    return _mem_range;
}

int Section::section_type()
{
    return _secthdr.sh_type;
}

bool Section::is_null()
{
    return section_type() == SHT_NULL;
}

bool Section::is_string_table()
{
    return section_type() == SHT_STRTAB;
}

bool Section::is_symbol_table()
{
    return section_type() == SHT_SYMTAB;
}

bool Section::is_nobits()
{
    return section_type() == SHT_NOBITS;
}

std::string Section::find_string(istream &is, int index)
{
    if (!is_string_table() || index < 0) {
	return false;
    }

    int offset = _secthdr.sh_offset + index;
    is.seekg(offset, ios_base::beg);
    // Symbols are null-terminated.
    const int MAX_SYMBOL_SIZE = 1024;
    char *buf = (char *)alloca(MAX_SYMBOL_SIZE);
    is.getline(buf, MAX_SYMBOL_SIZE, '\0');
    string result(buf);
    if (!is) {
	result.clear();
    }
    return result;
}

std::list<SymbolPtr> Section::symbols()
{
    return _symbols;
}

std::list<SymbolPtr> Section::find_symbols_in_mem_range(const RangePtr &mrange)
{
    list<SymbolPtr> result;
    list<SymbolPtr>::iterator it;

    for (it = _symbols.begin(); it != _symbols.end(); ++it) {
	if ((*it)->is_defined() && mrange->overlaps(*(*it)->range())) {
	    result.push_back(*it);
	}
    }
    return result;
}

bool Section::load_symbols(std::istream &is,
			   const SectionPtr &string_table)
{
    if (!is_symbol_table()) {
	return false;
    }
    unsigned int symentry_size = _secthdr.sh_entsize;
    if (symentry_size <= 0) {
	warn << "Invalid symbol entry table size " << symentry_size << "\n";
	return false;
    }

    int total_size = _secthdr.sh_size;
    if (total_size % symentry_size != 0) {
	warn << "Section::load_symbols - size mismatch " << symentry_size
	     << ", " << total_size << "\n";
	return false;
    }

    list<string> entries;
    if (!File::load_table(is,
			  "symbol table",
			  _secthdr.sh_offset,
			  (total_size / symentry_size),
			  symentry_size,
			  entries)
	|| entries.empty()) {
	return false;
    }
    
    _symbols.clear();
    list<string>::iterator it;
    for(it = entries.begin(); it != entries.end(); ++it) {
	SymbolPtr symbol(new Symbol);
	if (symbol->init(*it)) {
	    symbol->set_name(is, string_table);
	    _symbols.push_back(symbol);
	}
	else {
	    warn << "Failed to init symbol from buffer\n";
	}
    }

    return true;
}
			   

// ------------------------------------------------------------

bool Symbol::init(const string &buffer)
{
    memcpy(&_symstruct, buffer.data(), sizeof(_symstruct));
    _range.reset(new Range(_symstruct.st_value,
			   _symstruct.st_value + _symstruct.st_size));
    return true;
}

bool Symbol::set_name(istream &is, const SectionPtr &string_table)
{
    int name_index = _symstruct.st_name;
    _name = string_table->find_string(is, name_index);
    return is.good();
}

string Symbol::name()
{
    return _name;
}

int Symbol::size()
{
    return _symstruct.st_size;
}

RangePtr Symbol::range()
{
    return _range;
}

bool Symbol::is_defined()
{
    return _symstruct.st_value != 0 && !_name.empty();
}

int Symbol::type()
{
    return _symstruct.st_info & 0xf;
}

bool Symbol::is_func()
{
    return type() == STT_FUNC;
}

bool Symbol::is_data()
{
    return type() == STT_OBJECT;
}

bool Symbol::is_file()
{
    return type() == STT_FILE;
}

bool Symbol::is_section()
{
    return type() == STT_SECTION;
}

