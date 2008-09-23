/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#include <Trun.hpp>
#include "Elf.hpp"
#include "jutil.hpp"
#include "Pcre.hpp"

#include <list>
#include <string>

class ElfTest : public Test
{
public:
    bool setup();
    bool run();
    bool teardown();
private:
    struct testdat
    {
	int type;
	bool is_exe;
    };
    std::map<std::string, struct testdat> _testdat;
};

using namespace std;
using namespace Pcre;

bool ElfTest::run()
{
    plan(110);

    Elf::File e;

    notok(e.load(string("./doesnotexist")), "can't load missing file");
    const string nonelf("/etc/motd");
    ok(jutil::file_exists(nonelf), "nonelf file exists");
    notok(e.load(nonelf), "can't load existing non-elf file");
    const string exist_but_noprivs("/etc/shadow");
    ok(jutil::file_exists(exist_but_noprivs), "file exists");
    notok(e.load(exist_but_noprivs), "can't load existing non-privs file");
       
    map<string, struct testdat>::iterator it;
    for (it = _testdat.begin(); it != _testdat.end(); ++it) {
	struct testdat *td = &(it->second);
	string fname = it->first;
	list<string> section_lines, lines;
	
	ok(e.load(fname), "can load file " + fname);
	is(e.filename(), fname, "has correct name");
	is(e.elf_file_type(), td->type, "file " + fname + " correct elf type");
	is(e.is_executable(), td->is_exe, "file is_exe correct");

	ok(jutil::read_proc_output("readelf -S " + fname,
				   section_lines), "can readelf the file");

	Regexp re;
	ok(re.compile("^\\s*\\[ *\\d+]"), "can compile regexp");
	re.grep(section_lines);
	is (e.num_sections(), (int) section_lines.size(),
	    "number of sections match readelf");

	ok(e.section(".text"), "can find text section");
	ok(e.section(".data"), "can find data section");
	ok(e.section(".bss"), "can find bss section");

	int num_segments = e.num_segments();
	ok(num_segments > 0, "can find segments");

	ok(jutil::read_proc_output("readelf -l " + fname,
				   lines), "can readelf segments");
	re.compile("There are (\\d+) program headers");
	re.grep(lines);
	is((int) lines.size(), 1, "found right line in readelf output");
	list<string> captures;
	re.match_capture(lines.front(), captures);
	is((int) captures.size(), 1, "got my one capture");
	int readelf_numsegs = atoi(captures.front().c_str());
	is(num_segments, readelf_numsegs, "number of segments matches readelf");

	list<Elf::SegmentPtr> segs = e.segments();
	is((int) segs.size(), num_segments, "sanity check num of segs");
	segs = e.loadable_segments();
	is((int) segs.size(), 2, "two loadable segs for each elf");

	ok(segs.front()->is_readable(), "first is readable");
	notok(segs.front()->is_writable(), "first is not writable");
	ok(segs.front()->is_executable(), "first is executable");
	
	ok(segs.back()->is_readable(), "second is readable");
	ok(segs.back()->is_writable(), "second is writable");
	notok(segs.back()->is_executable(), "second is not executable");


	// get the same readelf lines again
	ok(jutil::read_proc_output("readelf -l " + fname,
				   lines), "can readelf segments");
	bool all_sections_match = true;
	list<Elf::SectionPtr> sections = e.sections();
	is((int) sections.size(), e.num_sections(),
	   "sanity check num sections");
	list<Elf::SectionPtr>::iterator it;
	int isection = 0;
	for (it = sections.begin(); it != sections.end(); ++it) {
	    if (section_lines.empty()) {
		all_sections_match = false;
		break;
	    }
	    string line = section_lines.front();
	    section_lines.pop_front();
	    string section_name = line.substr(7, 17);
	    jutil::trim(section_name);
	    Elf::SectionPtr sect = e.section(isection++);
	    if (section_name != sect->name()) {
		all_sections_match = false;
		break;
	    }
	}
	ok(all_sections_match, "section names and order match readelf");

	int readelf_num_symbols = 0;
	ok(jutil::read_proc_output("readelf -s " + fname, lines),
	   "can readelf symbol info from " + fname);
	re.compile("^Symbol table '.symtab' contains (\\d+) entries");
	re.grep(lines);
	if (!lines.empty()) {
	    is((int) lines.size(), 1, "readelf has .symtab line and only one");
	    re.match_capture(lines.front(), captures);
	    is((int) captures.size(), 1, "got my one capture");
	    readelf_num_symbols = atoi(captures.front().c_str());
	}
	
	list<Elf::SymbolPtr> syms;
	syms = e.all_symbols();
	is(readelf_num_symbols, (int) syms.size(), "correct number of symbols");

	if (td->type == ET_EXEC && readelf_num_symbols > 0) {

	    Elf::SymbolPtr mainsym = e.symbol("main");
	    ok(mainsym, "can find main symbol in exe");
	    is(mainsym->name(), string("main"), "symbol has correct name");
	    ok(mainsym->size() > 0, "main has non-zero size");
	    ok(mainsym->is_func(), "main symbol is a function");

	    syms = e.symbols_in_section(e.section(".text"));
	    list<Elf::SymbolPtr>::iterator sym_it;
	    for (sym_it = syms.begin(); sym_it != syms.end(); ++sym_it) {
		if ((*sym_it)->name() == "main") {
		    break;
		}
	    }
	    ok(sym_it != syms.end(), "found main in text section");

	    syms = e.symbols_in_section(e.section(".data"));
	    for (sym_it = syms.begin(); sym_it != syms.end(); ++sym_it) {
		if ((*sym_it)->name() == "main") {
		    break;
		}
	    }
	    ok(sym_it == syms.end(), "didn't find main in text section");
	}
    }

    ok(Elf::PAGE_SIZE > 0, "check we have a sane page size");
    is((int) Elf::page_align_down(Elf::PAGE_SIZE), Elf::PAGE_SIZE, "pad 1");
    is((int) Elf::page_align_down(Elf::PAGE_SIZE-1), 0, "pad 2");
    is((int) Elf::page_align_down(Elf::PAGE_SIZE+1), Elf::PAGE_SIZE, "pad 3");
    is((int) Elf::page_align_down(2*Elf::PAGE_SIZE), 2*Elf::PAGE_SIZE, "pad 4");
    is((int) Elf::page_align_down(2*Elf::PAGE_SIZE-1), Elf::PAGE_SIZE, "pad 5");
    is((int) Elf::page_align_down(2*Elf::PAGE_SIZE+1), 2*Elf::PAGE_SIZE,
	    "pad 6");
    
    is((int) Elf::page_align_up(Elf::PAGE_SIZE), Elf::PAGE_SIZE, "pad 1");
    is((int) Elf::page_align_up(Elf::PAGE_SIZE-1), Elf::PAGE_SIZE, "pad 2");
    is((int) Elf::page_align_up(Elf::PAGE_SIZE+1), 2*Elf::PAGE_SIZE, "pad 3");
    is((int) Elf::page_align_up(2*Elf::PAGE_SIZE), 2*Elf::PAGE_SIZE, "pad 4");
    is((int) Elf::page_align_up(2*Elf::PAGE_SIZE-1), 2*Elf::PAGE_SIZE, "pad 5");
    is((int) Elf::page_align_up(2*Elf::PAGE_SIZE+1), 3*Elf::PAGE_SIZE, "pad 6");
    
    return true;
}

bool ElfTest::setup()
{
    struct testdat td;

    td.type = ET_EXEC;
    td.is_exe = true;
    _testdat["/bin/ls"] = td;
    
    td.type = ET_DYN;
    td.is_exe = false;
    _testdat["/usr/lib/libz.so"] = td;

    td.type = ET_EXEC;
    td.is_exe = true;
    _testdat["./t_elf"] = td;
    return true;
}

bool ElfTest::teardown()
{
    return false;
}


RUN_TEST_CLASS(ElfTest);

