/*
 * (c) John Berthels 2005 <jjberthels@gmail.com>. See COPYING for license.
 */
#ifndef _EXMAP_H
#define _EXMAP_H

#include <list>
#include <map>
#include <string>
#include <vector>
#include <set>

#include <boost/smart_ptr.hpp>

#include <sys/types.h>
#include "jutil.hpp"
#include "Elf.hpp"

namespace Exmap 
{
    typedef unsigned long PageCookie;

    class Vma;
    typedef boost::shared_ptr<Vma> VmaPtr;
    class Map;
    typedef boost::shared_ptr<Map> MapPtr;
    class Sizes;
    typedef boost::shared_ptr<Sizes> SizesPtr;
    class Process;
    typedef boost::shared_ptr<Process> ProcessPtr;
    class File;
    typedef boost::shared_ptr<File> FilePtr;
    class PagePool;
    typedef boost::shared_ptr<PagePool> PagePoolPtr;
    class Page;
    typedef boost::shared_ptr<Page> PagePtr;
    class SysInfo;
    typedef boost::shared_ptr<SysInfo> SysInfoPtr;

    /// This class is the interface to the system to query information
    /// about processes (pids, vmas, page info). It's abstract to
    /// allow plugging in mock objects for testing (and to help
    /// porting).
    class SysInfo
    {
    public:
	virtual ~SysInfo();

	/// Get a list of pids for which we can read info
	virtual std::list<pid_t> accessible_pids() = 0;

	/// Check that the system is in a good state (e.g. required
	/// kernel modules available, versions, etc).
	virtual bool sanity_check() = 0;

	/// Read the page info for a pid
	virtual bool read_page_info(pid_t pid,
				    std::map<Elf::Address, std::list<Page> > &pi) = 0;

	/// Read cmdline for pid
	virtual std::string read_cmdline(pid_t pid) = 0;

	/// Read vma list
	virtual bool read_vmas(const PagePoolPtr &pp,
			       pid_t pid,
			       std::list<VmaPtr> &vmas) = 0;

    private:
    };

    /// Concrete implementation of Sysinfo for a Linux system.
    class LinuxSysInfo : public SysInfo
    {
    public:
	virtual ~LinuxSysInfo();
	virtual std::list<pid_t> accessible_pids();
	virtual bool sanity_check();
	virtual bool read_page_info(pid_t pid,
			    std::map<Elf::Address, std::list<Page> > &pi);
	virtual std::string read_cmdline(pid_t pid);
	virtual bool read_vmas(const PagePoolPtr &pp,
			       pid_t pid,
			       std::list<VmaPtr> &vmas);
    protected:
	/// Parse a single /proc/xxx/maps line and instantiate a vma
	VmaPtr parse_vma_line(const std::string &line);
    private:

        std::string proc_map_file(pid_t pid);
	static const std::string EXMAP_FILE;
    };


    /// Holds the various measures we can make of a File, Process or
    /// ELF memory range and also handles scaling to different units.
    class Sizes
    {
    public:
	Sizes();
	/// These are the size measures we support. Keep in sync with the
	/// Sizes::names[] array.
	/// They are ordered by 'usefulness'.
	enum Measure {
	    EFFECTIVE_RESIDENT = 0,
	    EFFECTIVE_MAPPED,
	    WRITABLE,
	    VM,
	    SOLE_MAPPED,
	    MAPPED,
	    RESIDENT,
	    NUM_SIZES,
	};
	
	/// Get the value, scaled into the current units
	double sval(int which);

	/// Get the value
	double val(int which);

	/// Add to a value
	void increase(enum Measure which, double amount);

	/// Human readable name for the size
	static std::string size_name(int which);

	/// Add the values from another set of sizes.
	void add(const SizesPtr &other);

	/// Set scale factor to 1
	static void scale_units();
	
	/// Set scale factor to kbytes
	static void scale_kbytes();
	
	/// Set scale factor to mbytes
	static void scale_mbytes();
	
    private:
	/// These are the size names
	const static std::string names[];

	/// The measure values
	double _values[NUM_SIZES];

	/// Scale starts off as 1
	static int _scale_factor;

	/// Scale name starts off as empty
	static std::string _scale_name;
    };

    class Page
    {
    public:
	bool parse_line(const std::string &line);
	bool is_mapped() const;
	bool is_resident() const;
	bool is_writable() const;
	PageCookie cookie() const;
	void print(std::ostream &os) const;
    private:
	PageCookie _cookie;
	int _writable : 1;
	int _resident : 1;
    };
    

    /// Wrap the information about one process vma (i.e. one line from
    /// /proc/xxx/maps).
    class Vma
    {
    public:
	struct PartialPageInfo
	{
	    Page page;
	    Elf::Address bytes;
	};
	
	Vma(Elf::Address start,
	    Elf::Address end,
	    off_t offset,
	    const std::string &fname);

	PagePoolPtr &page_pool();
	
	/// Record that we own this page (called in order, first to last)
	void add_pages(const std::list<Page> &pages);

	/// The vma start address
	Elf::Address start();

	/// The vma end addres
	Elf::Address end();

	RangePtr range();
	
	/// The name of the underlying file (possibly [anon] or other
	/// if there is no real file)
	std::string fname();

	/// The offset of the vma start within the file
	/// (not a useful value unless the Vma is file backed)
	off_t offset();

	/// The length of the VM area
	Elf::Address vm_size();

	std::string to_string() const;

	/// True if the vma is the special page - we generally wish to
	/// ignore.
	bool is_vdso();
	
	/// True if there is a file backing this VMA (checks the
	/// filename for specials like [anon], etc
	bool is_file_backed();

	bool get_pages_for_range(const RangePtr &mrange,
				 std::list<PartialPageInfo> &info);
	
	std::list<MapPtr> calc_maps(const FilePtr &file,
				    const VmaPtr &previous_vma,
				    const FilePtr &previous_file,
				    pid_t pid);

	boost::shared_ptr<Vma> selfptr();
	void selfptr(boost::shared_ptr<Vma> &p);

	int num_pages();

    private:
	/// Get the pgnum (index into the page vector) of the given
	/// address.
	bool addr_to_pgnum(Elf::Address addr, unsigned int &pgnum);

	
	RangePtr _range;
	off_t _offset;
	std::string _fname;
	std::vector<Page> _pages;
	boost::weak_ptr<Vma> _selfptr;
    };

    /// A map represents a range of memory (mem_range) within a
    /// vma. The entire range may or may not represent a chunk of an
    /// ELF file. If it does, the elf_range will contain the elf
    /// 'virtual address'.
    ///
    /// A map is also the basic currency used to calculate the various
    /// sizes. Each proc and file has a list of associated maps.
    ///
    /// Even VMAs which are map files may contain maps with no elf
    /// range, e.g. the .bss
    class Map
    {
    public:
	Map(const VmaPtr &vma,
	    const RangePtr &mem_range,
	    const RangePtr &elf_range);
	RangePtr mem_range();
	RangePtr elf_range();
	RangePtr elf_to_mem_range(const RangePtr &elf_range);
	SizesPtr sizes_for_mem_range(const PagePoolPtr &pp);
	SizesPtr sizes_for_mem_range(const PagePoolPtr &pp,
				     const RangePtr &mrange);
	std::string to_string() const;
	void print(std::ostream &os) const;

	/// Add up all the sizes for a list of maps
	static SizesPtr sum_sizes(const PagePoolPtr &pp,
				  const std::list<MapPtr> &maps);
    private:
	Elf::Address elf_to_mem_offset();
	VmaPtr _vma;
	RangePtr _mem_range;
	RangePtr _elf_range;
    };
    
    /// Wrap the information about one file.
    class File
    {
    public:
	File(const std::string &fname);
	std::string name();
	std::list<ProcessPtr> procs();
	Elf::FilePtr elf();
	bool is_elf();
	std::list<MapPtr> maps();
	SizesPtr sizes();
	void add_map(const MapPtr &map);
	void add_maps(const std::list<MapPtr> &maps);
	void add_proc(const ProcessPtr &proc);
    private:
	std::string _fname;
	std::list<MapPtr> _maps;
	std::set<ProcessPtr> _procs;
	Elf::FilePtr _elf;
    };

    /// Hold information regarding each mapped file
    class FilePool
    {
    public:
	void clear();
	FilePtr name_to_file(const std::string &name);
	FilePtr get_or_make_file(const std::string &name);
	std::list<FilePtr> files();
    private:
	std::map<std::string, FilePtr> _files;
    };
    typedef boost::shared_ptr<FilePool> FilePoolPtr;

    /// Hold information regarding each page in use
    class PagePool
    {
    public:
	void clear();
	int count(const Page &page);
	void inc_page_count(const Page &page);
	void inc_pages_count(const std::list<Page> &pages);
    private:
	std::map<PageCookie, int> _counts;
    };

    
    /// Wrap the information about one process.
    class Process
    {
    public:
	Process(const PagePoolPtr &pp, pid_t pid);
	bool load(SysInfoPtr &sys_info);
	bool has_mm();
	pid_t pid();
	std::string cmdline();
	std::list<FilePtr> files();
	SizesPtr sizes();
	SizesPtr sizes(const FilePtr &file);
	SizesPtr sizes(const FilePtr &file,
		       const RangePtr &elf_range);
	bool calculate_maps(FilePoolPtr &file_pool);
	boost::shared_ptr<Process> selfptr();
	void selfptr(boost::shared_ptr<Process> &p);
	void add_file(const FilePtr &file);
	void print(std::ostream &os) const;
	const PagePoolPtr &page_pool();
    private:
	void remove_vdso_if_nopages();
	boost::weak_ptr<Process> _selfptr;
	bool load_page_info(SysInfoPtr &sys_info);
	bool find_vma_by_addr(Elf::Address start,
			       VmaPtr &current_vma);
	std::list<MapPtr> restrict_maps_to_file(const FilePtr &file);
	/// The ordered list of vmas read from /proc/xxx/maps.
	std::list<VmaPtr> _vmas;

	// Special handling for the one-page [vdso] vma. Depending on
	// distro and kernel version, this may or may not return a
	// page from /proc/exmap. It doesn't add any useful info, so
	// we discard the vma. However, we want to error-trap bad
	// addresses from /proc/exmap, so keep track of the discarded
	// address to suppress the error.
	Elf::Address _vdso_address;

	/// The pid of this process.
	pid_t _pid;

	/// Read from /proc/xxx/cmdline and processed
	std::string _cmdline;

	std::list<MapPtr> _maps;

	std::set<FilePtr> _files;

	const PagePoolPtr &_page_pool;
    };

    /// Wrap one complete memusage snapshot
    class Snapshot
    {
    public:
	Snapshot(SysInfoPtr &sys_info);
	
	/// Get a list of all processes in the snapshot.
	const std::list<ProcessPtr> procs();

	/// Get a list of pids of all processes in the snapshot.
	const std::list<pid_t> pids();
	
	/// Return number of procs in the snapshot.
	inline int num_procs() { return _procs.size(); }
	
	/// Get a process with a particular pid.
	const ProcessPtr proc(pid_t pid);

	/// Get a list of all files in the snapshot.
	const std::list<FilePtr> files();
	
	/// Get a particular file
	const FilePtr file(const std::string &fname);
	
	/// Load the snapshot
	bool load();
    private:

	// ----------------------------------------
	// Methods
	// ----------------------------------------

	/// Load the pid list as procs.
	bool load_procs(const std::list<pid_t> &pids);

	/// Calculate the ELF file->VMA mappings
	bool calculate_file_mappings();

	// ----------------------------------------
	// Data
	// ----------------------------------------
	
	/// Map pid -> process. Also store all our processes
	std::map<pid_t, ProcessPtr> _procs;
	
	/// Global per-page information
	PagePoolPtr _page_pool;

	/// Map name -> file. Also store all our files.
	FilePoolPtr _file_pool;

	/// Source of our information about processes
	SysInfoPtr &_sys_info;
    };
    typedef boost::shared_ptr<Snapshot> SnapshotPtr;

    /// Method class to wrap the (complex) calculation of maps within vmas
    class MapCalculator
    {
	public:
	    /// Init the calculator. The calculator is per-proc, per vma-list
	    MapCalculator(const std::list<VmaPtr> &vmas,
		    FilePoolPtr &file_pool,
		    const ProcessPtr &proc);

	    /// Do the calculation.
	    bool calc_maps(std::list<MapPtr> &maps);
	private:
	    /// Verify that there are no overlapping maps
	    bool check_no_overlaps(std::list<MapPtr> &maps);

	    /// Sort the maps and check they provide a contiguous cover of the
	    /// vma range.
	    bool check_file_maps(const std::list<MapPtr> &maps,
		    const std::list<VmaPtr> &consumed_vmas); 

	    /// Calculate offset from segment virtual addresses to vma addrs
	    Elf::Address get_seg_to_mem(const Elf::SegmentPtr &seg,
		    const VmaPtr &vma);

	    bool calc_seg_map(const Elf::SegmentPtr &seg,
		    const FilePtr &file,
		    const VmaPtr &vma,
		    std::list<MapPtr> &seg_maps,
		    RangePtr &overrun);

	    MapPtr calc_overrun_map(const VmaPtr &vma,
		    const VmaPtr &previous_vma,
		    const RangePtr &overrun_elf_range,
		    const Elf::SegmentPtr &seg);

	    /// Calculate maps for a single elf-file
	    bool calc_elf_file(const FilePtr &file,
		    std::list<MapPtr> &file_maps,
		    std::list<VmaPtr> &consumed_vmas);

	    /// Calculate maps for a single non-elf-file
	    bool calc_nonelf_file(const FilePtr &file,
		    std::list<MapPtr> &file_maps,
		    std::list<VmaPtr> &consumed_vmas);

	    /// Advance _covered_to address and consume vmas
	    bool consume_to(Elf::Address addr,
		    std::list<VmaPtr> &consumed_vmas);

	    Elf::Address _covered_to;
	    std::list<VmaPtr> _vmas;

	    FilePoolPtr _file_pool;
	    const ProcessPtr _proc;
    };

};


std::ostream &operator<<(std::ostream &os, const Exmap::Process &proc);
std::ostream &operator<<(std::ostream &os, const Exmap::Map &map);


#endif
