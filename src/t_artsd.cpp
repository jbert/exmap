/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#include <Trun.hpp>
#include "Exmap.hpp"

#include <sstream>

class TestSysInfo : public Exmap::LinuxSysInfo
{
public:
    struct pidinfo
    {
	std::string cmdline;
	std::list<std::string> vma_lines;
    };
    ~TestSysInfo();
    std::list<pid_t> TestSysInfo::accessible_pids();
    bool sanity_check();
    bool read_page_info(pid_t pid,
			    std::map<Elf::Address, std::list<Exmap::Page> >&pi);
    std::string read_cmdline(pid_t pid);
    bool read_vmas(const Exmap::PagePoolPtr &pp,
		       pid_t pid,
		       std::list<Exmap::VmaPtr> &vmas);
    
    void set_pid_info(const std::map<pid_t, struct pidinfo> &info);
private:
    void random_page_info(bool *resident,
			  bool *writable,
			  Exmap::PageCookie *cookie);
    std::map<pid_t, struct pidinfo> _info;
    std::map<pid_t, std::list<Exmap::VmaPtr> > _vmas;
};

typedef boost::shared_ptr<TestSysInfo> TestSysInfoPtr;

class ArtsdTest : public Test
{
public:
    bool setup();
    bool run();
private:
    static std::map<pid_t, struct TestSysInfo::pidinfo> info;
};

using namespace std;
using namespace Exmap;
using namespace jutil;

RUN_TEST_CLASS(ArtsdTest);

// ------------------------------------------------------------

TestSysInfo::~TestSysInfo()
{ }

list<pid_t> TestSysInfo::accessible_pids()
{
    return map_keys(_info);
}

void TestSysInfo::set_pid_info(const std::map<pid_t, struct pidinfo> &info)
{
    _info = info;
    map<pid_t, struct pidinfo>::const_iterator it;
    for (it = info.begin(); it != info.end(); ++it) {
	list<string>::const_iterator line_it;
	const list<string> &vma_lines = it->second.vma_lines;
	for (line_it = vma_lines.begin();
	     line_it != vma_lines.end();
	     ++line_it) {
	    VmaPtr vma = parse_vma_line(*line_it);
	    vma->selfptr(vma);
	    _vmas[it->first].push_back(vma);
	}
    }
}

bool TestSysInfo::sanity_check()
{
    return true;
}

// Make up some random page data
bool TestSysInfo::read_page_info(pid_t pid,
				 map<Elf::Address, list<Page> > &pi)
{
    list<VmaPtr>::iterator vma_it;
    stringstream sstr;
    pi.clear();

    list<VmaPtr> vmas = _vmas[pid];

    for(vma_it = vmas.begin(); vma_it != vmas.end(); ++vma_it) {
	Elf::Address addr;
	Elf::Address vma_start = (*vma_it)->start();
	Elf::Address vma_end = (*vma_it)->end();
	for (addr = vma_start; addr < vma_end; addr += Elf::PAGE_SIZE) {
	    sstr.str("");

	    bool resident, writable;
	    PageCookie cookie;
	    random_page_info(&resident, &writable, &cookie);

	    sstr << resident << " "
		 << writable << " "
		 << cookie << "\n";
	    Page p;
	    p.parse_line(sstr.str());

	    pi[vma_start].push_back(p);
	}
    }

    return true;
}

void TestSysInfo::random_page_info(bool *resident,
				   bool *writable,
				   PageCookie *cookie)
{
    *resident = false;
    *writable = false;
    *cookie = 0;
}

string TestSysInfo::read_cmdline(pid_t pid)
{
    return _info[pid].cmdline;
}

bool TestSysInfo::read_vmas(const PagePoolPtr &pp,
			    pid_t pid,
			    list<VmaPtr> &vmas)
{
    vmas.clear();
    vmas = _vmas[pid];
    return !vmas.empty();
}

// ------------------------------------------------------------

map<pid_t, struct TestSysInfo::pidinfo> ArtsdTest::info;

bool ArtsdTest::setup()
{
    plan(2);

    struct TestSysInfo::pidinfo pi;
    pi.cmdline = "./foo";
    info[1234] = pi;

    info[1234].vma_lines.push_back("08047000-08070000 r-xp 00000000 16:0a 29616899   ./munged-ls-threeloads");
    info[1234].vma_lines.push_back("08070000-08074000 rw-p 00028000 16:0a 29616899   ./munged-ls-threeloads");
    info[1234].vma_lines.push_back("08076000-080bf000 rw-p 0002b000 16:0a 29616899   ./munged-ls-threeloads");
    info[1234].vma_lines.push_back("080bf000-081b7000 rw-p 080bf000 00:00 0          [heap]");
    return true;
}

bool ArtsdTest::run()
{
    TestSysInfoPtr tsi(new TestSysInfo);

    tsi->set_pid_info(info);
    SysInfoPtr si(tsi);
    Snapshot snap(si);

    ok(snap.load(), "can load test");
    
    is(snap.num_procs(), 1, "check we've loaded the right number of procs");
    return true;
}

