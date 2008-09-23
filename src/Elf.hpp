/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#ifndef _ELF_CPP_H
#define _ELF_CPP_H

#include <list>
#include <string>
#include <fstream>

#include <elf.h>

#include <boost/shared_ptr.hpp>

#include "Range.hpp"

namespace Elf
{
    static const int PAGE_SIZE = 4096;
    typedef unsigned long Address;
    Address page_align_down(const Address &addr);
    Address page_align_up(const Address &addr);

    class Section;
    typedef boost::shared_ptr<Section> SectionPtr;
    
    class Symbol
    {
    public:
	bool init(const std::string &data);
	int size();
	bool is_defined();
	bool is_func();
	bool is_data();
	bool is_file();
	bool is_section();
	std::string name();
	bool set_name(std::istream &is, const SectionPtr &string_table);
	RangePtr range();
    private:
	int type();
	Elf32_Sym _symstruct;
	std::string _name;
	RangePtr _range;
    };
    typedef boost::shared_ptr<Symbol> SymbolPtr;
    
    class Section
    {
    public:
	bool init(const std::string &buffer);
	std::string name();
	bool set_name(std::istream &is, const SectionPtr &string_table);
	Elf32_Word link();
	Elf32_Addr addr();
	Elf32_Word size();
	RangePtr file_range();
	RangePtr mem_range();
	/// Returns the sh_type value. See elf.h
	int section_type();
	bool is_null();
	bool is_string_table();
	bool is_symbol_table();
	bool is_nobits();
	std::list<SymbolPtr> symbols();
	std::list<SymbolPtr> find_symbols_in_mem_range(const RangePtr &mrange);
	bool load_symbols(std::istream &is,
			  const SectionPtr &string_table);
	std::string find_string(std::istream &is, int index);
    private:
	Elf32_Shdr _secthdr;
	RangePtr _mem_range;
	RangePtr _file_range;
	std::list<SymbolPtr> _symbols;
	std::string _name;
    };

    class Segment
    {
    public:
	bool init(const std::string &buffer);
	RangePtr mem_range();
	RangePtr file_range();
	Address offset();
	bool is_load();
	bool is_readable();
	bool is_writable();
	bool is_executable();
    private:
	RangePtr _mem_range;
	RangePtr _file_range;
	bool flag_is_set(int flag);
	Elf32_Phdr _seghdr;
    };
    typedef boost::shared_ptr<Segment> SegmentPtr;
    
    class File
    {
    public:
	File();
	bool load(const std::string &fname, bool warn_if_non_elf = true);
	std::list<SymbolPtr> find_symbols_in_mem_range(const RangePtr &mrange);
	std::list<SymbolPtr> defined_symbols();
	std::list<SymbolPtr> all_symbols();
	SymbolPtr symbol(const std::string &symname);
	std::list<SymbolPtr> symbols_in_section(const SectionPtr &section);
	std::string filename();
	int num_sections();
	int num_segments();
	std::list<SegmentPtr> segments();
	std::list<SegmentPtr> loadable_segments();
	SectionPtr section(int i);
	SectionPtr section(const std::string &name);
	std::list<SectionPtr> mappable_sections();
	std::list<SectionPtr> sections();
	/// Returns the e_type value. See elf.h
	int elf_file_type();
	/// Syntactic sugar for elf_file_type() == ET_EXEC
	bool is_executable();
	/// Syntactic sugar for elf_file_type() == ET_DYN
	bool is_shared_object();
	static bool load_table(std::istream &is,
			       const std::string &table_name,
			       Elf32_Off offset,
			       Elf32_Half num_chunks,
			       Elf32_Half chunksize,
			       std::list<std::string> &entries);
    private:
	bool lazy_load_sections();
	void unload();
	bool open_file();
	bool correlate_string_sections();
	bool load_file_header();
	bool load_sections();
	bool load_segments();

	bool _lazy_load_sections;
	std::ifstream _ifs;
	std::string _fname;
	Elf32_Ehdr _header;
	std::list<Elf::SegmentPtr> _segments;
	std::list<Elf::SectionPtr> _sections;
	SectionPtr _symbol_table_section;
    };
    typedef boost::shared_ptr<File> FilePtr;
};

#endif
